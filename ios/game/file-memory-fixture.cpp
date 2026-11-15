// Exercise the real file adapter through checked memory copies, not raw output
// pointers, including native publication versus an original guest LOCK.
#include "../cpu/runtime.h"
#include "file-services.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>

Memory *dispatch(State &, uint64_t, Memory *memory) { return memory; }
using namespace std::chrono_literals;
namespace fs = std::filesystem;
namespace {
unsigned passed = 0;
void check(bool okay, const char *message) {
  if (!okay)
    throw std::runtime_error(message);
  ++passed;
}
uint64_t fault() { return uint64_t(int64_t(int32_t(0x8002000e))); }
uint64_t service(const char *name, uint64_t a = 0, uint64_t b = 0,
                 uint64_t c = 0, uint64_t d = 0) {
  const uint64_t args[6] = {a, b, c, d};
  uint64_t result;
  check(dcp_file_service(name, args, &result), "unhandled fixture service");
  return result;
}
struct Context {
  Memory memory;
  unsigned pointers = 0, reads = 0, writes = 0, validations = 0;
  size_t largest_copy = 0;
  bool reject_read = false, reject_write = false, reject_validation = false;
  bool unsafe_publication = false; // Functional negative control, fixture only.
  int guest_errno = 0;
  std::atomic<bool> write_entered{false};
  std::atomic<bool> write_released{false};
  bool pause_publication = false;
  bool valid(uint64_t address, size_t size) {
    return address >= Memory::base && size <= memory.bytes.size() &&
           address - Memory::base <= memory.bytes.size() - size;
  }
  uint64_t text(size_t offset, const char *value) {
    const uint64_t address = Memory::base + offset;
    guestWrite(memory, address, value, std::strlen(value) + 1);
    return address;
  }
};
DcpFileHooks callbacks(Context &state, const fs::path &root, bool copying) {
  DcpFileHooks hooks;
  hooks.context = &state;
  hooks.game_root = (root / "game").string();
  hooks.save_root = (root / "saves").string();
  hooks.savedata_access = [](void *, bool) { return true; };
  hooks.savedata_directory = [](void *) { return std::string("slot"); };
  hooks.pointer = [](void *context, uint64_t address, size_t size,
                     bool) -> void * {
    auto &state = *static_cast<Context *>(context);
    ++state.pointers;
    return state.valid(address, size)
               ? guestPointer(state.memory, address, size)
               : nullptr;
  };
  hooks.set_errno = [](void *context, int32_t value) {
    static_cast<Context *>(context)->guest_errno = value;
  };
  if (copying) {
    hooks.validate_memory = [](void *context, uint64_t address, size_t size,
                               bool) {
      auto &state = *static_cast<Context *>(context);
      ++state.validations;
      if (state.reject_validation || !state.valid(address, size))
        return false;
      guestValidate(state.memory, address, size);
      return true;
    };
    hooks.read_memory = [](void *context, uint64_t address, void *output,
                           size_t size) {
      auto &state = *static_cast<Context *>(context);
      ++state.reads;
      state.largest_copy = std::max(state.largest_copy, size);
      if (state.reject_read || !state.valid(address, size))
        return false;
      guestRead(state.memory, address, output, size);
      return true;
    };
    hooks.write_memory = [](void *context, uint64_t address, const void *input,
                            size_t size) {
      auto &state = *static_cast<Context *>(context);
      ++state.writes;
      state.largest_copy = std::max(state.largest_copy, size);
      state.write_entered = true;
      if (state.reject_write || !state.valid(address, size))
        return false;
      if (state.pause_publication)
        while (!state.write_released.load())
          std::this_thread::yield();
      if (state.unsafe_publication)
        std::memcpy(state.memory.bytes.data() + address - Memory::base, input,
                    size);
      else
        guestWrite(state.memory, address, input, size);
      return true;
    };
  }
  return hooks;
}
std::vector<uint8_t> contents(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), {}};
}
void exercise(const fs::path &root, bool copying, bool unsafe) {
  fs::create_directories(root / "game");
  fs::create_directories(root / "saves/slot");
  std::vector<uint8_t> data(600 * 1024 + 73);
  for (size_t i = 0; i < data.size(); ++i)
    data[i] = uint8_t((i * 131) ^ (i >> 8));
  {
    std::ofstream file(root / "game/data.bin", std::ios::binary);
    file.write(reinterpret_cast<const char *>(data.data()), data.size());
  }
  Context state;
  state.unsafe_publication = unsafe;
  state.memory.bytes.resize(1024 * 1024, 0xa5);
  auto hooks = callbacks(state, root, copying);
  dcp_file_init(hooks);
  const auto path = state.text(0, "/app0/data.bin");
  const auto destination = Memory::base + 4096;
  auto fd = service("sceKernelOpen", path, 0);
  check(int64_t(fd) >= 3, "read-only open failed");
  check(service("sceKernelRead", fd, destination, data.size() + 19) ==
            data.size(),
        "chunked EOF read length");
  check(std::equal(data.begin(), data.end(), state.memory.bytes.begin() + 4096),
        "chunked read contents");
  check(std::all_of(state.memory.bytes.begin() + 4096 + data.size(),
                    state.memory.bytes.begin() + 4096 + data.size() + 19,
                    [](uint8_t value) { return value == 0xa5; }),
        "short read overwrote tail");
  const auto writes = state.writes;
  check(service("sceKernelRead", fd, destination, 16) == 0 &&
            state.writes == writes,
        "EOF published uninitialized bytes");
  check(service("sceKernelPread", fd, destination, 701, 123) == 701,
        "positioned read length");
  check(std::equal(data.begin() + 123, data.begin() + 824,
                   state.memory.bytes.begin() + 4096),
        "positioned read contents");
  check(service("sceKernelLseek", fd, 0, SEEK_CUR) == data.size(),
        "pread changed file offset");
  check(service("sceKernelLseek", fd, 0, SEEK_SET) == 0, "rewind");
  check(service("sceKernelRead", fd,
                Memory::base + state.memory.bytes.size() - 16, 32) == fault(),
        "out-of-range read accepted");
  check(service("sceKernelLseek", fd, 0, SEEK_CUR) == 0,
        "invalid validation advanced offset");
  check(service("sceKernelRead", fd, UINT64_MAX - 1, 4) == fault(),
        "overflowing output accepted");
  check(service("sceKernelRead", fd, UINT64_MAX, 0) == 0,
        "zero-length read touched pointer");
  check(service("sceKernelFstat", fd, Memory::base + 1024) == 0, "fstat");
  uint64_t length = 0;
  guestRead(state.memory, Memory::base + 1024 + 0x48, &length, sizeof(length));
  check(length == data.size(), "fstat size");
  check(service("sceKernelStat", path, Memory::base + 2048) == 0, "stat");
  check(std::memcmp(state.memory.bytes.data() + 1024,
                    state.memory.bytes.data() + 2048, 0x78) == 0,
        "stat and fstat disagree");
  check(service("sceKernelFstat", fd,
                Memory::base + state.memory.bytes.size() - 1) == fault(),
        "invalid stat output accepted");
  const auto output_path = state.text(128, "/savedata0/result.bin");
  auto output = service("sceKernelOpen", output_path, 2 | 0x200 | 0x400);
  check(int64_t(output) >= 3, "writable open failed");
  guestWrite(state.memory, destination, data.data(), data.size());
  check(service("sceKernelPwrite", output, destination, data.size(), 13) ==
            data.size(),
        "positioned write length");
  auto expected = std::vector<uint8_t>(13, 0);
  expected.insert(expected.end(), data.begin(), data.end());
  check(contents(root / "saves/slot/result.bin") == expected,
        "positioned write bytes");
  check(service("sceKernelLseek", output, 0, SEEK_CUR) == 0,
        "pwrite changed offset");
  if (copying) {
    check(state.pointers == 0 && state.reads && state.writes &&
              state.validations,
          "copy path escaped a guest pointer");
    check(state.largest_copy <= (256u << 10), "unbounded staging copy");
    state.reject_read = true;
    check(service("sceKernelWrite", output, destination, 16) == fault(),
          "rejected guest input wrote file");
    state.reject_read = false;
    check(service("sceKernelLseek", output, 0, SEEK_CUR) == 0 &&
              contents(root / "saves/slot/result.bin") == expected,
          "rejected input changed disk");
    state.reject_validation = true;
    check(service("sceKernelPread", fd, destination, 16, 0) == fault(),
          "rejected validation read file");
    state.reject_validation = false;
    state.reject_write = true;
    const auto before = state.memory.bytes;
    check(service("pread", fd, destination, 16, 0) == UINT64_MAX &&
              state.guest_errno == 14,
          "POSIX copy failure lost errno");
    check(service("sceKernelStat", path, Memory::base + 2048) == fault(),
          "rejected stat output succeeded");
    check(state.memory.bytes == before, "rejected output changed memory");
    state.reject_write = false;
    state.write_entered = false;
    state.pause_publication = true;
    auto pending = std::async(std::launch::async, [&] {
      return service("sceKernelPread", fd, destination, 16, 0);
    });
    bool blocked = false, entered = false;
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!(entered = state.write_entered.load()) &&
           std::chrono::steady_clock::now() < deadline)
      std::this_thread::yield();
    // The original LOCK excludes publication even though host I/O has already
    // filled the private staging buffer. No borrowed guest output is used.
    GuestAtomicMutex original_lock;
    {
      std::lock_guard<GuestAtomicMutex> lock(original_lock);
      state.write_released = true;
      blocked = pending.wait_for(30ms) == std::future_status::timeout;
    }
    check(pending.get() == 16 && entered && blocked,
          "native file output crossed original LOCK");
  } else {
    check(state.pointers > 0, "legacy pointer path not exercised");
  }
  check(service("sceKernelClose", output) == 0 &&
            service("sceKernelClose", fd) == 0,
        "close");
  const auto stats = dcp_file_memory_stats();
  check(stats.synchronized_copies == copying, "copy diagnostic mode");
  check(copying ? stats.read_bytes >= data.size() &&
                      stats.write_bytes >= data.size()
                : stats.read_bytes == 0 && stats.write_bytes == 0,
        "copy diagnostic accounting");
  dcp_file_reset();
  check(!dcp_file_memory_stats().synchronized_copies &&
            !dcp_file_memory_stats().read_bytes &&
            !dcp_file_memory_stats().write_bytes,
        "file counters survive reset");
  if (copying) {
    hooks.write_memory = nullptr;
    bool rejected = false;
    try {
      dcp_file_init(hooks);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    check(rejected, "partial memory callback set accepted");
  }
}
void validation_windows(const fs::path &root) {
  fs::create_directories(root / "game");
  fs::create_directories(root / "saves");
  const size_t bytes = (64u << 20) + 4096 + 17;
  const auto path = root / "game/sparse.bin";
  std::ofstream(path).close();
  fs::resize_file(path, bytes);
  Context state;
  state.memory.bytes.resize(bytes + 4096, 0xa5);
  dcp_file_init(callbacks(state, root, true));
  auto fd = service("sceKernelOpen", state.text(0, "/app0/sparse.bin"), 0);
  check(int64_t(fd) >= 3, "window fixture open");
  check(service("sceKernelRead", fd, Memory::base + 4096, bytes) == bytes,
        "64 MiB validation boundary read");
  check(state.validations == 2 && state.largest_copy <= (256u << 10),
        "validation windows or staging size");
  check(std::all_of(state.memory.bytes.begin() + 4096, state.memory.bytes.end(),
                    [](uint8_t value) { return value == 0; }),
        "window boundary lost bytes");
  check(state.pointers == 0, "window fixture escaped pointer");
  check(service("sceKernelClose", fd) == 0, "window fixture close");
  dcp_file_reset();
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc < 2 || argc > 3 ||
        (argc == 3 && std::strcmp(argv[2], "--unsafe-publication")))
      throw std::runtime_error(
          "Usage: file-memory-fixture EXTERNAL_OUTPUT [--unsafe-publication]");
    const auto root =
        fs::path(argv[1]) / ("file-memory-" + std::to_string(getpid()));
    exercise(root / "legacy", false, false);
    exercise(root / "copies", true, argc == 3);
    validation_windows(root / "windows");
    fs::remove_all(root);
    std::cout << "{\"passed\":" << passed
              << ",\"failed\":0,\"original_lock_exclusion\":true}\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
