#include "../cpu/runtime.h"
#include "file-services.h"
#include "font-services.h"
#include "game.h"
#include "gnm-services.h"
#ifdef DCP_NATIVE_LIBC_BULK
#include "libc-bulk.h"
#endif
#include "modules.h"
#include "offline-services.h"
#include "render/capture.h"
#include "thread-services.h"
#include "traces.h"
#ifdef DCP_MOBILE_PLATFORM
#include "../platform/platform.h"
#endif
#include <algorithm>
#include <cfenv>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>

namespace {
struct HostFloatingEnvironment {
  std::fenv_t value;
  bool enabled;
  explicit HostFloatingEnvironment(bool preserve = true) : enabled(preserve) {
    if (enabled)
      std::fegetenv(&value);
  }
  ~HostFloatingEnvironment() {
    if (enabled)
      std::fesetenv(&value);
  }
};
struct GuestThreadExit : std::exception {
  uint64_t value;
  explicit GuestThreadExit(uint64_t result) : value(result) {}
  const char *what() const noexcept override { return "guest thread exit"; }
};
struct Module {
  uint64_t canonical;
  std::vector<uint8_t> data;
};
struct Mapping {
  uint8_t *data;
  size_t size;
  ~Mapping() { munmap(data, size); }
};
struct Engine {
  DcpGameHostHooks host{};
  std::vector<Module> modules;
  std::vector<std::unique_ptr<Mapping>> allocations;
  std::vector<std::unique_ptr<std::recursive_mutex>> mutexes;
  std::vector<std::string> events;
  std::map<std::string, uint64_t> eventCounts;
  std::atomic<unsigned> workersStarted{0};
  std::mutex metadata;
  std::mutex diagnosticLock;
  std::mutex eventLock;
  std::atomic<bool> stop{false};
  std::atomic<bool> userStop{false};
  std::atomic<bool> paused{false};
  std::mutex pauseMutex;
  std::condition_variable pauseChanged;
  std::chrono::steady_clock::time_point pausedAt;
  std::chrono::nanoseconds pausedTime{0};
  std::atomic<bool> interactive{false};
  std::atomic<uint64_t> steps{0};
  std::atomic<uint64_t> vblanks{0};
  uint64_t directCursor = 0;
  bool userLogged = false;
  std::atomic<uint64_t> libcChecks{0};
  std::atomic<int64_t> libcResult{-1};
  std::atomic<bool> enteredEboot{false};
  std::atomic<bool> gnmReady{false};
  std::atomic<bool> titleSeen{false};
  std::atomic<bool> booted{false};
  std::atomic<uint64_t> titleVblank{0};
  std::atomic<uint64_t> titleFlip{0};
  uint64_t heapTrace = 0;
  std::chrono::steady_clock::time_point epoch =
      std::chrono::steady_clock::now();
  std::string firstError;
  std::string currentScene;
  Memory primary;
};
std::atomic<Engine *> activeEngine{nullptr};
std::mutex engineLifetime;
std::atomic<bool> requestedPause{false};
DcpGameHostHooks configuredHost{};
bool configuredWaiting = false, configuredCancelled = false;
thread_local Engine *currentEngine = nullptr;
thread_local Memory *currentMemory = nullptr;
thread_local uint64_t rootStack = 0;
thread_local uint64_t tlsBlocks[2]{};
thread_local uint64_t errorSlot = 0;
thread_local uint64_t mainThread = 0;
thread_local std::vector<std::recursive_mutex *> heldMutexes;
Engine &engine() { return *currentEngine; }
bool pollRuntime() {
  auto cancelled = [] {
    return currentEngine && !currentEngine->stop.load() &&
           !dcp_thread_shutdown_requested() && dcp_thread_current_cancelled() &&
           (!currentMemory || !currentMemory->atomicLock.owns_lock());
  };
  if (cancelled())
    throw GuestThreadExit(kDcpThreadCanceledResult);
  if (currentEngine && currentEngine->paused.load() &&
      (!currentMemory || !currentMemory->atomicLock.owns_lock())) {
    std::unique_lock<std::mutex> lock(currentEngine->pauseMutex);
    currentEngine->pauseChanged.wait(lock, [] {
      return !currentEngine->paused.load() || currentEngine->stop.load() ||
             dcp_thread_stopping();
    });
  }
  if (cancelled())
    throw GuestThreadExit(kDcpThreadCanceledResult);
  return (currentEngine && currentEngine->stop.load()) ||
         dcp_thread_shutdown_requested();
}
void stopEngine(Engine &target) {
  {
    std::lock_guard<std::mutex> lock(target.pauseMutex);
    target.stop = true;
  }
  target.pauseChanged.notify_all();
}
uint64_t elapsedNanoseconds() {
  std::lock_guard<std::mutex> lock(engine().pauseMutex);
  auto now = engine().paused.load() ? engine().pausedAt
                                    : std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             now - engine().epoch - engine().pausedTime)
      .count();
}
void recordFailure(const std::string &value) {
  std::lock_guard<std::mutex> lock(engine().diagnosticLock);
  if (!engine().booted.load() && engine().firstError.empty() &&
      value.find("bootstrap stopped") == std::string::npos)
    engine().firstError = value;
}
void recordEvent(const std::string &value) {
  std::lock_guard<std::mutex> lock(engine().eventLock);
  if (++engine().eventCounts[value] == 1)
    engine().events.push_back(value);
}
constexpr uint64_t hleBase = 0x70000000;
constexpr uint64_t exitPC = 0x7fff0000;

std::string hex(uint64_t value) {
  std::ostringstream out;
  out << std::hex << value;
  return out.str();
}
std::string jsonString(const std::string &value) {
  std::ostringstream result;
  result << '"';
  for (unsigned char byte : value) {
    if (byte == '"' || byte == '\\')
      result << '\\' << char(byte);
    else if (byte < 32)
      result << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
             << unsigned(byte) << std::dec;
    else
      result << char(byte);
  }
  result << '"';
  return result.str();
}
uint64_t canonical(uint64_t address) {
  for (auto &module : engine().modules) {
    auto base = reinterpret_cast<uint64_t>(module.data.data());
    if (address >= base && address - base < module.data.size())
      return module.canonical + address - base;
  }
  return address;
}
uint64_t actual(uint64_t pc) {
  for (auto &module : engine().modules)
    if (pc >= module.canonical && pc - module.canonical < module.data.size())
      return reinterpret_cast<uint64_t>(module.data.data()) + pc -
             module.canonical;
  return pc;
}
void returnToGuest(State &state, Memory &memory, uint64_t value) {
  state.gpr.rax.qword = value;
  state.gpr.rip.qword = read64(memory, state.gpr.rsp.qword);
  state.gpr.rsp.qword += 8;
}
uint64_t allocate(Memory &memory, size_t size, size_t alignment = 16) {
  const size_t page = getpagesize();
  alignment = std::max(alignment, page);
  if (!size || (alignment & (alignment - 1)) ||
      size > SIZE_MAX - page - alignment)
    throw std::runtime_error("invalid guest allocation size or alignment");
  size = (size + page - 1) & ~(page - 1);
  const size_t extent = size + alignment;
  void *raw = mmap(nullptr, extent, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
  if (raw == MAP_FAILED)
    throw std::runtime_error("guest mapping failed");
  const uint64_t base = reinterpret_cast<uint64_t>(raw);
  const uint64_t address = (base + alignment - 1) & ~uint64_t(alignment - 1);
  const size_t before = address - base;
  const size_t after = extent - before - size;
  if (before)
    munmap(raw, before);
  if (after)
    munmap(reinterpret_cast<void *>(address + size), after);
  std::lock_guard<std::mutex> lock(engine().metadata);
  engine().allocations.push_back(std::unique_ptr<Mapping>(
      new Mapping{reinterpret_cast<uint8_t *>(address), size}));
  addRegion(memory, reinterpret_cast<uint8_t *>(address), size);
  return address;
}
Memory *service(State &state, uint64_t pc, Memory *memory) {
  auto index = (pc - hleBase) / 16;
  if (index >= std::size(imports))
    throw std::runtime_error("invalid HLE import");
  std::string name = imports[index];
  recordEvent(name);
  auto &r = state.gpr;
  uint64_t arguments[9] = {r.rdi.qword, r.rsi.qword, r.rdx.qword,
                           r.rcx.qword, r.r8.qword,  r.r9.qword};
  uint64_t gnmResult;
  if (name == "scePthreadExit")
    throw GuestThreadExit(r.rdi.qword);
  uint64_t offlineResult;
  if (dcp_offline_service(name.c_str(), arguments, &offlineResult)) {
    returnToGuest(state, *memory, offlineResult);
    return memory;
  }
  const float floating[2] = {state.vec[0].xmm.floats.elems[0],
                             state.vec[1].xmm.floats.elems[0]};
  if (name == "sceGnmSubmitAndFlipCommandBuffers" ||
      name == "sceGnmSubmitAndFlipCommandBuffersForWorkload")
    for (size_t i = 6; i < 9; ++i)
      arguments[i] = read64(*memory, r.rsp.qword + 8 + (i - 6) * 8);
  uint64_t fontResult;
  if (dcp_font_service(name.c_str(), arguments, floating, &fontResult)) {
    returnToGuest(state, *memory, fontResult);
    return memory;
  }
  uint64_t fileResult;
  if (dcp_file_service(name.c_str(), arguments, &fileResult)) {
    returnToGuest(state, *memory, fileResult);
    return memory;
  }
  uint64_t threadResult;
  if (dcp_thread_service(name.c_str(), arguments, &threadResult)) {
    returnToGuest(state, *memory, threadResult);
    return memory;
  }
  if (dcp_gnm_service(name.c_str(), arguments, &gnmResult)) {
    if (dcp_gnm_stats().rejected_accesses)
      throw std::runtime_error("GNM rejected a guest memory access");
    returnToGuest(state, *memory, gnmResult);
    return memory;
  }
  if (name == "sceKernelGetProcessTimeCounterFrequency") {
    returnToGuest(state, *memory, 1000000000);
  } else if (name == "sceKernelGetProcessTimeCounter" ||
             name == "sceKernelGetProcessTime") {
    uint64_t elapsed = elapsedNanoseconds();
    returnToGuest(state, *memory,
                  name == "sceKernelGetProcessTime" ? elapsed / 1000 : elapsed);
  } else if (name == "sceKernelUsleep") {
    auto end = std::chrono::steady_clock::now() +
               std::chrono::microseconds(uint32_t(r.rdi.qword));
    while (std::chrono::steady_clock::now() < end) {
      if (pollRuntime())
        throw std::runtime_error("bootstrap stopped while sleeping");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    returnToGuest(state, *memory, 0);
  } else if (name == "sceSystemServiceGetStatus") {
    // Same foreground/no-pending-system-events status as the desktop adapter.
    // SceSystemServiceStatus is eventNum plus flags/reserved bytes (0x84).
    if (r.rdi.qword)
      guestFill(*memory, r.rdi.qword, 0, 0x84);
    returnToGuest(state, *memory, 0);
  } else if (name == "sceNetCtlGetState" || name == "sceNpGetState") {
    // Offline mobile release: no address, no PSN sign-in, no network callbacks.
    uint64_t output = name == "sceNetCtlGetState" ? r.rdi.qword : r.rsi.qword;
    uint32_t status = name == "sceNetCtlGetState" ? 0 : 1;
    guestWrite(*memory, output, &status, sizeof(status));
    returnToGuest(state, *memory, 0);
  } else if (name == "sceNetCtlGetInfo") {
    uint8_t information[16]{};
    const uint8_t address[6] = {0x02, 0xdc, 0x50, 0, 0, 1};
    if (r.rdi.qword == 2)
      std::memcpy(information, address, 6);
    guestWrite(*memory, r.rsi.qword, information, r.rdi.qword == 2 ? 6 : 16);
    returnToGuest(state, *memory, 0);
  } else if (name == "sceNetCtlRegisterCallback") {
    uint32_t id = 1;
    if (r.rdx.qword)
      guestWrite(*memory, r.rdx.qword, &id, 4);
    returnToGuest(state, *memory, 0);
  } else if (name == "sceNpRegisterStateCallbackA" ||
             name == "sceNpRegisterPlusEventCallback") {
    returnToGuest(state, *memory, 1);
  } else if (name == "sceNpCheckCallback" || name == "sceNetCtlCheckCallback" ||
             name == "sceNetCtlUnregisterCallback" ||
             name == "sceNpUnregisterStateCallbackA" ||
             name == "sceNpUnregisterPlusEventCallback") {
    returnToGuest(state, *memory, 0);
  } else if (name == "sceUserServiceInitialize" ||
             name == "sceUserServiceTerminate" || name == "scePadInit" ||
             name == "sceCommonDialogInitialize" ||
             name == "sceSaveDataInitialize3" || name == "sceNetInit" ||
             name == "sceSysmoduleIsLoaded" || name == "sceNpSetNpTitleId" ||
             name == "sceNpSetContentRestriction" ||
             name == "sceSystemServiceHideSplashScreen") {
    returnToGuest(state, *memory, 0);
  } else if (name == "sceUserServiceGetUserName") {
    if (r.rsi.qword && r.rdx.qword)
      std::snprintf(reinterpret_cast<char *>(
                        guestPointer(*memory, r.rsi.qword, r.rdx.qword)),
                    r.rdx.qword, "Player %u",
                    r.rdi.qword >= 1000 && r.rdi.qword < 1004
                        ? unsigned(r.rdi.qword - 999)
                        : 1);
    returnToGuest(state, *memory, 0);
  } else if (name == "sceUserServiceGetInitialUser") {
    uint32_t user = 1000;
    guestWrite(*memory, r.rdi.qword, &user, 4);
    returnToGuest(state, *memory, 0);
  } else if (name == "sceUserServiceGetLoginUserIdList") {
    const int32_t users[4] = {1000, -1, -1, -1};
    guestWrite(*memory, r.rdi.qword, users, sizeof(users));
    returnToGuest(state, *memory, 0);
  } else if (name == "sceUserServiceGetEvent") {
    if (!engine().userLogged) {
      const uint32_t event[2] = {0, 1000};
      guestWrite(*memory, r.rdi.qword, event, sizeof(event));
      engine().userLogged = true;
      returnToGuest(state, *memory, 0);
    } else
      returnToGuest(state, *memory, 0x80960007);
  } else if (name == "sceSystemServiceGetDisplaySafeAreaInfo") {
    auto output = guestPointer(*memory, r.rdi.qword, 0x84);
    std::memset(output, 0, 0x84);
    float ratio = 1.0f;
    std::memcpy(output, &ratio, 4);
    returnToGuest(state, *memory, 0);
  } else if (name == "sceSystemServiceParamGetInt") {
    uint32_t value =
        (r.rdi.qword == 1 || r.rdi.qword == 2 || r.rdi.qword == 1000) ? 1 : 0;
    guestWrite(*memory, r.rsi.qword, &value, 4);
    returnToGuest(state, *memory, 0);
  } else if (name == "scePadOpen") {
    returnToGuest(state, *memory, r.rdi.qword == 1000 ? 1 : 0x80920001);
  } else if (name == "sceNetPoolCreate" || name == "sceSslInit" ||
             name == "sceHttpInit") {
    returnToGuest(state, *memory, 1);
  } else if (name == "sceKernelIsNeoMode") {
    returnToGuest(state, *memory, 0);
  } else if (name == "sceVideoOutOpen") {
    returnToGuest(state, *memory, 1);
  } else if (name == "sceVideoOutGetResolutionStatus") {
    auto output = guestPointer(*memory, r.rsi.qword, 40);
    std::memset(output, 0, 40);
    const uint32_t dims[4] = {1920, 1080, 1920, 1080};
    std::memcpy(output, dims, 16);
    write64(*memory, r.rsi.qword + 16, 3);
    float inches = 50;
    std::memcpy(output + 24, &inches, 4);
    returnToGuest(state, *memory, 0);
  } else if (name == "sceVideoOutWaitVblank") {
    std::this_thread::sleep_for(std::chrono::microseconds(16667));
    const uint64_t frame = ++engine().vblanks;
    if (!engine().interactive && engine().titleSeen.load() &&
        frame >= engine().titleVblank.load() + 120 &&
        dcp_gnm_stats().flips >= engine().titleFlip.load() + 120) {
      engine().booted = true;
      throw std::runtime_error("boot checkpoint complete");
    }
    if (!engine().interactive && frame > 1800)
      throw std::runtime_error("vblank checkpoint budget reached");
    returnToGuest(state, *memory, 0);
  } else if (name == "sceVideoOutSetBufferAttribute") {
    uint32_t values[6] = {
        uint32_t(r.rsi.qword), uint32_t(r.rdx.qword),
        uint32_t(r.rcx.qword), uint32_t(r.r8.qword),
        uint32_t(r.r9.qword),  uint32_t(read64(*memory, r.rsp.qword + 8))};
    auto output = guestPointer(*memory, r.rdi.qword, 40);
    std::memset(output, 0, 40);
    std::memcpy(output, values, sizeof(values));
    returnToGuest(state, *memory, 0);
  } else if (name == "sceVideoOutRegisterBuffers") {
    const int32_t start = int32_t(r.rsi.qword), count = int32_t(r.rcx.qword);
    if (start < 0 || count < 0 || start > 16 || count > 16 - start)
      throw std::runtime_error("invalid display buffer range");
    std::vector<void *> buffers(size_t(count), nullptr);
    guestRead(*memory, r.rdx.qword, buffers.data(),
              buffers.size() * sizeof(void *));
    dcp::render::set_display_buffers(buffers.data(), start, count);
    returnToGuest(state, *memory, 0);
  } else if (name == "sceVideoOutClose" || name == "sceVideoOutAddFlipEvent" ||
             name == "sceVideoOutAddVblankEvent" ||
             name == "sceVideoOutSetFlipRate") {
    returnToGuest(state, *memory, 0);
  } else if (name == "sceKernelGetDirectMemorySize") {
    returnToGuest(state, *memory, 0x13c000000);
  } else if (name == "sceKernelAllocateDirectMemory") {
    auto align = r.rcx.qword ? r.rcx.qword : 1;
    auto start = std::max(engine().directCursor, r.rdi.qword);
    if (align & (align - 1))
      throw std::runtime_error("invalid direct memory alignment");
    auto offset = (start + align - 1) & ~(align - 1);
    if (!r.rdx.qword || offset + r.rdx.qword < offset ||
        offset + r.rdx.qword > r.rsi.qword)
      throw std::runtime_error("direct memory allocation outside range");
    engine().directCursor = offset + r.rdx.qword;
    write64(*memory, r.r9.qword, offset);
    returnToGuest(state, *memory, 0);
  } else if (name == "sceKernelMapDirectMemory") {
    auto address =
        allocate(*memory, r.rsi.qword, std::max<uint64_t>(65536, r.r9.qword));
    if (address >= (uint64_t(1) << 40))
      throw std::runtime_error("direct memory exceeds guest GPU address width");
    write64(*memory, r.rdi.qword, address);
    returnToGuest(state, *memory, 0);
  } else if (name == "sceKernelMapNamedFlexibleMemory" ||
             name == "sceKernelMapFlexibleMemory") {
    auto address = allocate(*memory, r.rsi.qword, 16384);
    write64(*memory, r.rdi.qword, address);
    returnToGuest(state, *memory, 0);
  } else if (name == "sceLibcHeapGetTraceInfo") {
    if (!engine().heapTrace)
      engine().heapTrace = allocate(*memory, 520);
    uint32_t zero = 0;
    std::memcpy(guestPointer(*memory, r.rdi.qword + 12, 4), &zero, 4);
    write64(*memory, r.rdi.qword + 16, engine().heapTrace);
    write64(*memory, r.rdi.qword + 24, engine().heapTrace + 8);
    returnToGuest(state, *memory, 0);
  } else if (name == "sceKernelGetProcParam") {
    returnToGuest(state, *memory, actual(procParam));
  } else if (name == "__error") {
    if (!errorSlot)
      errorSlot = allocate(*memory, 8);
    returnToGuest(state, *memory, errorSlot);
  } else if (name == "__tls_get_addr") {
    auto mod = read64(*memory, r.rdi.qword);
    auto offset = read64(*memory, r.rdi.qword + 8);
    if (mod < 1 || mod > 2 || offset >= tlsTemplates[mod - 1][2])
      throw std::runtime_error("invalid TLS index");
    if (!tlsBlocks[mod - 1]) {
      const auto &info = tlsTemplates[mod - 1];
      tlsBlocks[mod - 1] = allocate(*memory, info[2]);
      std::memcpy(guestPointer(*memory, tlsBlocks[mod - 1], info[1]),
                  guestPointer(*memory, actual(info[0]), info[1]), info[1]);
    }
    returnToGuest(state, *memory, tlsBlocks[mod - 1] + offset);
  } else if (name == "scePthreadSelf") {
    if (!mainThread)
      mainThread = dcp_thread_current_handle();
    if (!mainThread)
      mainThread = allocate(*memory, 128);
    returnToGuest(state, *memory, mainThread);
  } else if (name == "scePthreadAttrGet") {
    uint8_t workerAttributes[32];
    const bool worker = dcp_thread_attributes(r.rdi.qword, workerAttributes);
    if (!worker && r.rdi.qword != mainThread)
      throw std::runtime_error("unknown guest thread");
    auto attribute = read64(*memory, r.rsi.qword);
    if (!attribute) {
      attribute = allocate(*memory, 32);
      write64(*memory, r.rsi.qword, attribute);
    }
    write64(*memory, attribute, 0x100000);
    write64(*memory, attribute + 8, uint64_t(1) << 32);
    write64(*memory, attribute + 16, (uint64_t(4) << 32) | 700);
    write64(*memory, attribute + 24, 0x7f);
    if (worker)
      guestWrite(*memory, attribute, workerAttributes, 32);
    returnToGuest(state, *memory, 0);
  } else if (name == "scePthreadAttrGetaffinity" ||
             name == "scePthreadAttrGetdetachstate") {
    auto attribute = read64(*memory, r.rdi.qword);
    auto offset = name == "scePthreadAttrGetaffinity" ? 24 : 8;
    auto size = name == "scePthreadAttrGetaffinity" ? 8 : 4;
    std::memcpy(guestPointer(*memory, r.rsi.qword, size),
                guestPointer(*memory, attribute + offset, size), size);
    returnToGuest(state, *memory, 0);
  } else if (name == "scePthreadAttrInit") {
    auto attribute = allocate(*memory, 32);
    write64(*memory, attribute, 0x100000);
    write64(*memory, attribute + 8, uint64_t(1) << 32);
    write64(*memory, attribute + 16, (uint64_t(4) << 32) | 700);
    write64(*memory, attribute + 24, 0x7f);
    write64(*memory, r.rdi.qword, attribute);
    returnToGuest(state, *memory, 0);
  } else if (name == "scePthreadAttrDestroy") {
    write64(*memory, r.rdi.qword, 0);
    returnToGuest(state, *memory, 0);
  } else if (name == "scePthreadAttrSetstacksize" ||
             name == "scePthreadAttrSetaffinity") {
    auto attribute = read64(*memory, r.rdi.qword);
    write64(*memory, attribute + (name == "scePthreadAttrSetaffinity" ? 24 : 0),
            r.rsi.qword);
    returnToGuest(state, *memory, 0);
  } else if (name == "scePthreadAttrSetdetachstate" ||
             name == "scePthreadAttrSetschedpolicy" ||
             name == "scePthreadAttrSetschedparam" ||
             name == "scePthreadAttrSetinheritsched") {
    auto attribute = read64(*memory, r.rdi.qword);
    unsigned offset = name == "scePthreadAttrSetdetachstate"   ? 8
                      : name == "scePthreadAttrSetschedpolicy" ? 12
                      : name == "scePthreadAttrSetschedparam"  ? 16
                                                               : 20;
    uint32_t value = uint32_t(r.rsi.qword);
    if (name == "scePthreadAttrSetschedparam")
      std::memcpy(&value, guestPointer(*memory, r.rsi.qword, 4), 4);
    std::memcpy(guestPointer(*memory, attribute + offset, 4), &value, 4);
    returnToGuest(state, *memory, 0);
  } else if (name == "scePthreadMutexattrInit") {
    auto address = allocate(*memory, 8);
    write64(*memory, address, 1);
    write64(*memory, r.rdi.qword, address);
    returnToGuest(state, *memory, 0);
  } else if (name == "scePthreadMutexattrSettype" ||
             name == "scePthreadMutexattrSetprotocol") {
    auto address = read64(*memory, r.rdi.qword);
    uint32_t value = uint32_t(r.rsi.qword);
    std::memcpy(
        guestPointer(
            *memory,
            address + (name == "scePthreadMutexattrSetprotocol" ? 4 : 0), 4),
        &value, 4);
    returnToGuest(state, *memory, 0);
  } else if (name == "scePthreadMutexattrDestroy") {
    write64(*memory, r.rdi.qword, 0);
    returnToGuest(state, *memory, 0);
  } else if (name == "scePthreadMutexInit" || name == "scePthreadMutexLock" ||
             name == "scePthreadMutexUnlock") {
    uint64_t handle =
        name == "scePthreadMutexInit" ? 0 : read64(*memory, r.rdi.qword);
    std::recursive_mutex *native = nullptr;
    {
      std::lock_guard<std::mutex> lock(engine().metadata);
      if (handle < 2) {
        engine().mutexes.push_back(std::make_unique<std::recursive_mutex>());
        handle = reinterpret_cast<uint64_t>(engine().mutexes.back().get());
        addRegion(*memory, reinterpret_cast<uint8_t *>(handle),
                  sizeof(std::recursive_mutex));
        write64(*memory, r.rdi.qword, handle);
      }
      auto found = std::find_if(engine().mutexes.begin(),
                                engine().mutexes.end(), [&](const auto &entry) {
                                  return reinterpret_cast<uint64_t>(
                                             entry.get()) == handle;
                                });
      if (found == engine().mutexes.end())
        throw std::runtime_error("invalid mutex handle");
      native = found->get();
    }
    if (name == "scePthreadMutexLock") {
      while (!native->try_lock()) {
        if (dcp_thread_stopping())
          throw std::runtime_error("bootstrap stopped while locking mutex");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      heldMutexes.push_back(native);
    }
    if (name == "scePthreadMutexUnlock") {
      auto found = std::find(heldMutexes.rbegin(), heldMutexes.rend(), native);
      if (found == heldMutexes.rend())
        throw std::runtime_error("guest unlocked an unowned mutex");
      heldMutexes.erase(std::next(found).base());
      native->unlock();
    }
    returnToGuest(state, *memory, 0);
  } else if (name == "sceSysmoduleLoadModule" ||
             name == "sceSysmoduleUnloadModule" ||
             name == "_sceKernelRtldSetApplicationHeapAPI") {
    returnToGuest(state, *memory, 0);
  } else {
    bool verifiedStub = false;
    for (const char *stub : offlineStubs)
      if (name == stub)
        verifiedStub = true;
    if (!verifiedStub)
      throw std::runtime_error("missing HLE: " + name);
    recordEvent("reference stub: " + name);
    returnToGuest(state, *memory, 0);
  }
  return memory;
}
uint64_t invoke(uint64_t pc, Memory &memory, uint64_t a = 0, uint64_t b = 0,
                uint64_t c = 0) {
  State state{};
  const uint32_t rounding = __remill_fpu_get_rounding();
  state.x87.fxsave.cwd.flat = uint16_t(0x037f | (rounding << 10));
  state.x87.fxsave.mxcsr.flat = 0x1f80 | (rounding << 13);
  state.gpr.rdi.qword = a;
  state.gpr.rsi.qword = b;
  state.gpr.rdx.qword = c;
  if (!rootStack)
    rootStack = allocate(memory, 2 * 1024 * 1024, 16384);
  state.gpr.rsp.qword = rootStack + 2 * 1024 * 1024 - 8;
  write64(memory, state.gpr.rsp.qword, exitPC);
  try {
    dispatch(state, actual(pc), &memory);
  } catch (const std::runtime_error &error) {
    throw std::runtime_error(std::string(error.what()) + " at PC " +
                             hex(canonical(state.gpr.rip.qword)));
  }
  if (!memory.fault.empty())
    throw std::runtime_error(memory.fault);
  if (state.gpr.rip.qword != exitPC)
    throw std::runtime_error("unexpected guest return PC");
  recordEvent("return " + hex(pc) + " = " +
              std::to_string(state.gpr.rax.qword));
  return state.gpr.rax.qword;
}
void releaseHeldMutexes() {
  while (!heldMutexes.empty()) {
    heldMutexes.back()->unlock();
    heldMutexes.pop_back();
  }
}
uint64_t runWorker(void *context, uint64_t pc, uint64_t argument) {
  auto &target = *static_cast<Engine *>(context);
  const bool nested = currentEngine == &target && currentMemory;
  HostFloatingEnvironment floatingEnvironment(!nested);
  Memory local;
  local.regions = target.primary.regions;
  local.stopping = &target.stop;
  local.cancelled = pollRuntime;
  local.stopOnFault = true;
  auto *previousEngine = currentEngine;
  auto *previousMemory = currentMemory;
  const auto previousStack = rootStack;
  currentEngine = &target;
  currentMemory = nested ? previousMemory : &local;
  rootStack = 0;
  if (!nested) {
    const uint64_t identity = dcp_thread_current_handle();
    if (mainThread != identity) {
      tlsBlocks[0] = tlsBlocks[1] = errorSlot = 0;
      mainThread = identity;
      ++engine().workersStarted;
    }
    heldMutexes.clear();
  }
  try {
    uint64_t value;
    try {
      value = invoke(canonical(pc), *currentMemory, argument);
    } catch (const GuestThreadExit &exit) {
      value = exit.value;
    }
    if (!nested)
      releaseHeldMutexes();
    currentEngine = previousEngine;
    currentMemory = previousMemory;
    rootStack = previousStack;
    return value;
  } catch (...) {
    if (currentMemory->atomicLock.owns_lock())
      currentMemory->atomicLock.unlock();
    try {
      throw;
    } catch (const std::exception &error) {
      recordFailure(error.what());
    } catch (...) {
      recordFailure("guest worker failed");
    }
    stopEngine(target);
    releaseHeldMutexes();
    currentEngine = previousEngine;
    currentMemory = previousMemory;
    rootStack = previousStack;
    throw;
  }
}
} // namespace
Memory *dispatch(State &state, uint64_t pc, Memory *memory) {
  if (pollRuntime())
    throw std::runtime_error("bootstrap stopped");
  uint64_t key = canonical(pc);
  if (key == exitPC)
    return memory;
  if (key >= hleBase && key < hleBase + std::size(imports) * 16)
    [[clang::musttail]] return service(state, key, memory);
  const auto trace = std::lower_bound(
      std::begin(traces), std::end(traces), key,
      [](const Trace &entry, uint64_t address) { return entry.pc < address; });
  if (trace != std::end(traces) && trace->pc == key) {
    if (key == ebootEntry)
      engine().enteredEboot = true;
    [[clang::musttail]] return trace->function(state, pc, memory);
  }
  throw std::runtime_error("missing trace: " + hex(key));
}
extern "C" const char *dcp_cpu_report() {
  HostFloatingEnvironment floatingEnvironment;
  static std::string report;
  static std::unique_ptr<Engine> retained;
  auto owner = std::make_unique<Engine>();
  {
    std::lock_guard<std::mutex> lock(engineLifetime);
    if (activeEngine.load())
      return "{\"error\":\"a game session is already running or stopping\"}";
    if (retained)
      return "{\"error\":\"previous guest workers are still stopping\"}";
    currentEngine = owner.get();
    activeEngine = owner.get();
    if (configuredWaiting && configuredCancelled) {
      engine().userStop = true;
      engine().stop = true;
    }
    configuredWaiting = false;
    engine().host = configuredHost;
    engine().paused = requestedPause.load();
    engine().pausedAt = engine().epoch;
  }
  engine().interactive = std::getenv("DCP_GAME_MODE") &&
                         std::string(std::getenv("DCP_GAME_MODE")) == "play";
  engine().modules.clear();
  engine().allocations.clear();
  engine().events.clear();
  engine().steps = 0;
  engine().userLogged = false;
  engine().directCursor = 0;
  engine().vblanks = 0;
  rootStack = 0;
  engine().libcChecks = 0;
  engine().libcResult = -1;
#ifdef DCP_NATIVE_LIBC_BULK
  dcp_libc_bulk_reset();
#endif
  engine().enteredEboot = false;
  tlsBlocks[0] = tlsBlocks[1] = errorSlot = engine().heapTrace = mainThread = 0;
  Memory &memory = engine().primary;
  memory.stopping = &engine().stop;
  memory.stopOnFault = true;
  memory.cancelled = pollRuntime;
  currentMemory = &memory;
  dcp_thread_init(
      {owner.get(), runWorker,
       [](void *context) {
         return static_cast<Engine *>(context)->stop.load();
       },
       [](void *context, uint64_t address, size_t size, bool) -> void * {
         return guestPointer(static_cast<Engine *>(context)->primary, address,
                             size);
       },
       [](void *context, void *address, size_t size) {
         addRegion(static_cast<Engine *>(context)->primary,
                   static_cast<uint8_t *>(address), size);
       },
       [](void *context) {
         auto &target = *static_cast<Engine *>(context);
         std::lock_guard<std::mutex> lock(target.pauseMutex);
         target.pauseChanged.notify_all();
       }});
  dcp_gnm_init(
      {&memory,
       [](void *context, uint64_t address, void *output, size_t bytes) {
         guestRead(*static_cast<Memory *>(context), address, output, bytes);
         return true;
       },
       [](void *context, uint64_t address, const void *input, size_t bytes) {
         guestWrite(*static_cast<Memory *>(context), address, input, bytes);
         return true;
       }},
      {owner.get(),
       [](void *, uint64_t command, uint32_t words, uint32_t count,
          uint64_t indices, bool indexed) {
         dcp::render::on_draw(reinterpret_cast<uint32_t *>(command), words,
                              count, indices, indexed);
       },
       [](void *, const uint32_t *payload, uint32_t words) {
         dcp::render::on_marker(payload, words);
       },
       [](void *, uint32_t index) { dcp::render::on_flip(index); }});
  engine().gnmReady = true;
  std::string stage = "load", error;
  try {
    DcpFileHooks files;
    files.context = owner.get();
    files.pointer = [](void *context, uint64_t address, size_t size,
                       bool) -> void * {
      return guestPointer(static_cast<Engine *>(context)->primary, address,
                          size);
    };
    files.set_errno = [](void *, int32_t error) {
      if (!errorSlot)
        errorSlot = allocate(*currentMemory, 8);
      guestWrite(*currentMemory, errorSlot, &error, 4);
    };
    files.game_root = std::getenv("DCP_GAME_ROOT")
                          ? std::getenv("DCP_GAME_ROOT")
                          : defaultGameRoot;
    files.save_root = std::getenv("DCP_SAVE_ROOT")
                          ? std::getenv("DCP_SAVE_ROOT")
                          : defaultSaveRoot;
    DcpOfflineHooks offline;
    offline.context = owner.get();
    offline.pointer = files.pointer;
    offline.vblank_count = [](void *context) {
      return static_cast<Engine *>(context)->vblanks.load();
    };
    offline.stopping = [](void *context) {
      return static_cast<Engine *>(context)->stop.load();
    };
    offline.save_root = files.save_root;
    offline.scripted_input = !engine().interactive;
#ifdef DCP_MOBILE_PLATFORM
    if (engine().interactive) {
      offline.input_read = [](void *, unsigned slot, DcpPlatformPad *pad) {
        dcp_platform_input_read(slot, pad);
      };
      offline.audio_start = [](void *) { return dcp_platform_audio_start(); };
      offline.audio_stop = [](void *) { dcp_platform_audio_stop(); };
      offline.audio_submit = [](void *, const float *samples, size_t frames,
                                uint32_t timeout) {
        return dcp_platform_audio_submit(samples, frames, timeout);
      };
    }
#endif
    if (engine().host.save_transaction) {
      offline.savedata_transaction = [](void *context, bool begin) {
        auto *target = static_cast<Engine *>(context);
        struct Operation {
          Engine *target;
          bool begin;
        } operation{target, begin};
        return dcp_file_with_savedata_quiescent(
            [](void *opaque) {
              auto &work = *static_cast<Operation *>(opaque);
              return work.target->host.save_transaction(
                         work.target->host.context, work.begin) != 0;
            },
            &operation);
      };
    }
    dcp_offline_init(offline);
    files.savedata_access = [](void *, bool writing) {
      return dcp_offline_savedata_access(writing);
    };
    files.savedata_directory = [](void *) {
      return dcp_offline_savedata_directory();
    };
    dcp_file_init(files);
    DcpFontHooks font;
    font.context = owner.get();
    font.pointer = files.pointer;
    font.register_range = [](void *context, void *address, size_t size) {
      addRegion(static_cast<Engine *>(context)->primary,
                static_cast<uint8_t *>(address), size);
    };
    dcp_font_init(font);
    for (const auto &info : moduleData) {
      engine().modules.push_back({info.canonical, {info.begin, info.end}});
      auto &data = engine().modules.back().data;
      addRegion(memory, data.data(), data.size());
    }
    if (!dcp::render::initialize_aot_capture(
            engine().modules[0].data.data(), engine().modules[0].data.size(),
            {owner.get(),
             [](void *context, uint64_t address, size_t bytes, bool) {
               try {
                 return guestPointer(static_cast<Engine *>(context)->primary,
                                     address, bytes) != nullptr;
               } catch (const std::exception &) {
                 return false;
               }
             },
             [](void *context, uint64_t address, void *destination,
                size_t bytes) {
               try {
                 guestRead(static_cast<Engine *>(context)->primary, address,
                           destination, bytes);
                 return true;
               } catch (const std::exception &) {
                 return false;
               }
             }}))
      throw std::runtime_error("AOT capture initialization failed");
    auto guard = allocate(memory, 8);
    write64(memory, guard, 0x517a6b2f);
    for (unsigned index = 0; index < std::size(moduleData); ++index) {
      const auto &info = moduleData[index];
      for (const auto *fixup = info.fixups; fixup < info.fixupsEnd; ++fixup) {
        uint64_t value = fixup->value;
        if (fixup->module == 3)
          value = guard;
        if (fixup->module < 2)
          value += reinterpret_cast<uint64_t>(
              engine().modules[fixup->module].data.data());
        std::memcpy(engine().modules[index].data.data() + fixup->offset, &value,
                    8);
      }
    }
    stage = "libc_strlen";
    auto text = allocate(memory, 256);
    for (size_t length : {0, 1, 7, 8, 15, 16, 31, 127}) {
      for (size_t offset = 0; offset < 8; ++offset) {
        std::memset(guestPointer(memory, text, 256), 0xa5, 256);
        *guestPointer(memory, text + offset + length, 1) = 0;
        if (invoke(libcStrlen, memory, text + offset) != length)
          throw std::runtime_error("libc strlen mismatch");
        ++engine().libcChecks;
      }
    }
    stage = "libc_module_start";
    engine().libcResult = invoke(0x30000000, memory);
    if (engine().libcResult != 0)
      throw std::runtime_error("libc module_start failed");
    stage = "eboot_entry";
    auto params = allocate(memory, 32);
    auto name = allocate(memory, 32);
    std::memcpy(guestPointer(memory, name, 10), "eboot.bin", 10);
    write64(memory, params, 1);
    write64(memory, params + 8, name);
    invoke(ebootEntry, memory, params, exitPC);
    stage = "entry_returned";
  } catch (const std::exception &exception) {
    error = exception.what();
    if (memory.atomicLock.owns_lock())
      memory.atomicLock.unlock();
    recordFailure(error);
  }
  stopEngine(engine());
  if (memory.atomicLock.owns_lock())
    memory.atomicLock.unlock();
  releaseHeldMutexes();
  const bool joined = dcp_thread_stop_and_join(5000);
  if (engine().host.will_unload)
    engine().host.will_unload(engine().host.context);
  std::string firstError, currentScene;
  {
    std::lock_guard<std::mutex> lock(engine().diagnosticLock);
    firstError = engine().firstError;
    currentScene = engine().currentScene;
  }
  if (!firstError.empty())
    error = firstError;
  else if (*dcp_thread_error())
    error = dcp_thread_error();
  if (engine().booted.load() && firstError.empty()) {
    stage = "title";
    error.clear();
  }
  if (engine().userStop.load() && joined) {
    stage = "stopped";
    error.clear();
  }
  if (!joined)
    error = "guest workers failed to stop within timeout";
  else {
    dcp_file_reset();
    dcp_font_reset();
    dcp::render::shutdown_aot_capture();
  }
  std::ostringstream out;
  out << "{\"backend\":\"Remill AOT real modules\",\"stage\":\"" << stage
      << "\",\"steps\":" << engine().steps
      << ",\"booted\":" << (engine().booted.load() ? "true" : "false")
      << ",\"game_code_executed\":"
      << (engine().enteredEboot ? "true" : "false")
      << ",\"guest_workers\":" << engine().workersStarted
      << ",\"libc_strlen_checks\":" << engine().libcChecks
      << ",\"libc_start_result\":" << engine().libcResult
      << ",\"title_seen\":" << (engine().titleSeen.load() ? "true" : "false")
      << ",\"checkpoint_complete\":"
      << (engine().booted.load() ? "true" : "false")
      << ",\"interactive\":" << (engine().interactive.load() ? "true" : "false")
      << ",\"current_scene\":" << jsonString(currentScene)
      << ",\"trace_hash\":\"" << traceHash << "\"";
#ifdef DCP_NATIVE_LIBC_BULK
  const auto bulk = dcp_libc_bulk_stats();
  out << ",\"native_libc_bulk\":{\"enabled\":"
      << (DCP_NATIVE_LIBC_BULK ? "true" : "false")
      << ",\"copy_calls\":" << bulk.copy_calls
      << ",\"move_calls\":" << bulk.move_calls
      << ",\"fill_calls\":" << bulk.fill_calls
      << ",\"bytes\":" << bulk.bytes << "}";
#endif
  out << ",\"error\":" << jsonString(error) << ",\"events\":[";
  bool first = true;
  std::lock_guard<std::mutex> eventsLock(engine().eventLock);
  for (const auto &event : engine().events) {
    if (!first)
      out << ',';
    first = false;
    out << jsonString(event);
  }
  const auto gnm = dcp_gnm_stats();
  out << "],\"submits\":" << gnm.submits << ",\"flips\":" << gnm.flips
      << ",\"label_writes\":" << gnm.label_writes
      << ",\"vblanks\":" << engine().vblanks << "}";
  report = out.str();
  if (joined) {
    std::lock_guard<std::mutex> lock(engineLifetime);
    activeEngine = nullptr;
  }
  currentEngine = nullptr;
  currentMemory = nullptr;
  if (!joined)
    retained = std::move(owner);
  return report.c_str();
}
extern "C" Memory *__remill_error(State &, uint64_t pc, Memory *) {
  throw std::runtime_error("guest instruction error: " + hex(canonical(pc)));
}
extern "C" Memory *__remill_async_hyper_call(State &, uint64_t pc, Memory *) {
  throw std::runtime_error("guest trap: " + hex(canonical(pc)));
}
extern "C" Memory *__remill_sync_hyper_call(State &state, Memory *,
                                            uint32_t kind) {
  throw std::runtime_error("unsupported instruction " + std::to_string(kind) +
                           " at " + hex(canonical(state.gpr.rip.qword)));
}

extern "C" void dcp_trace_enter(State &state, uint64_t pc) {
  if (pollRuntime())
    throw std::runtime_error("bootstrap stopped");
  if (++engine().steps > 100000000 && !engine().interactive)
    throw std::runtime_error("guest trace budget exceeded");
  const uint64_t observedPC = canonical(pc);
  if (observedPC >= 0x20000000 && observedPC < 0x30000000) {
    const auto &r = state.gpr;
    const uint64_t args[6] = {r.rdi.qword, r.rsi.qword, r.rdx.qword,
                              r.rcx.qword, r.r8.qword,  r.r9.qword};
    dcp::render::observe_aot_entry(observedPC - 0x20000000, args);
  }
  if (observedPC == 0x20068840) {
    char name[16]{};
    try {
      for (size_t i = 0; i < 15; ++i) {
        guestRead(*currentMemory, state.gpr.rdi.qword + i, &name[i], 1);
        if (!name[i])
          break;
      }
    } catch (const std::runtime_error &) {
      return;
    }
    bool ascii = name[0] != 0;
    for (size_t i = 0; name[i] && i < sizeof(name) - 1; ++i)
      if (static_cast<unsigned char>(name[i]) < 32 ||
          static_cast<unsigned char>(name[i]) > 126)
        ascii = false;
    if (ascii) {
      bool changed;
      {
        std::lock_guard<std::mutex> lock(engine().diagnosticLock);
        changed = engine().currentScene != name;
        engine().currentScene = name;
      }
      if (changed)
        recordEvent(std::string("scene ") + name);
    }
    if (!std::strcmp(name, "TITLE") && !engine().titleSeen.exchange(true)) {
      engine().titleVblank = engine().vblanks.load();
      engine().titleFlip = dcp_gnm_stats().flips;
    }
  }
}

namespace dcp::hle {
uint64_t vblank_count_public() {
  std::lock_guard<std::mutex> lifetime(engineLifetime);
  auto *owner = activeEngine.load();
  return owner ? owner->vblanks.load() : 0;
}
} // namespace dcp::hle

extern "C" void dcp_game_set_paused(int paused) {
  requestedPause = paused != 0;
  std::lock_guard<std::mutex> lifetime(engineLifetime);
  auto *target = activeEngine.load();
  if (!target)
    return;
  std::lock_guard<std::mutex> lock(target->pauseMutex);
  const auto now = std::chrono::steady_clock::now();
  if (paused && !target->paused.exchange(true))
    target->pausedAt = now;
  else if (!paused && target->paused.exchange(false))
    target->pausedTime += now - target->pausedAt;
  target->pauseChanged.notify_all();
}
extern "C" void dcp_game_request_stop() {
  std::lock_guard<std::mutex> lifetime(engineLifetime);
  auto *target = activeEngine.load();
  if (!target) {
    if (configuredWaiting)
      configuredCancelled = true;
    return;
  }
  target->userStop = true;
  stopEngine(*target);
}

extern "C" int dcp_game_configure(DcpGameHostHooks hooks) {
  std::lock_guard<std::mutex> lock(engineLifetime);
  if (activeEngine.load())
    return 0;
  configuredHost = hooks;
  configuredWaiting = true;
  configuredCancelled = false;
  return 1;
}

extern "C" int dcp_game_is_running() { return activeEngine.load() != nullptr; }

extern "C" int dcp_game_snapshot(DcpGameStats *output) {
  if (!output)
    return 0;
  *output = DcpGameStats{};
  output->libc_result = -1;
  std::lock_guard<std::mutex> lifetime(engineLifetime);
  auto *target = activeEngine.load();
  if (!target)
    return 0;
  output->running = 1;
  output->steps = target->steps.load();
  output->vblanks = target->vblanks.load();
  output->workers = target->workersStarted.load();
  output->libc_checks = target->libcChecks.load();
  output->libc_result = target->libcResult.load();
  output->paused = target->paused.load();
  output->stopping = target->stop.load();
  output->interactive = target->interactive.load();
  output->entered_game = target->enteredEboot.load();
  output->title_seen = target->titleSeen.load();
  output->checkpoint_complete = target->booted.load();
  if (target->gnmReady.load()) {
    const auto gnm = dcp_gnm_stats();
    output->submits = gnm.submits;
    output->flips = gnm.flips;
    output->label_writes = gnm.label_writes;
  }
  {
    std::lock_guard<std::mutex> metadata(target->diagnosticLock);
    std::snprintf(output->current_scene, sizeof(output->current_scene), "%s",
                  target->currentScene.c_str());
    std::snprintf(output->first_error, sizeof(output->first_error), "%s",
                  target->firstError.c_str());
  }
  return 1;
}
