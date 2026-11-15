#include "ime-services.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
std::array<uint8_t, 2048> memory;
unsigned checks = 0;
bool copying = false, reject_write = false;
unsigned escapes = 0, copied_reads = 0, copied_writes = 0;
void require(bool value) {
  ++checks;
  if (!value)
    throw std::runtime_error("IME fixture check " + std::to_string(checks));
}
void *pointer(void *, uint64_t address, size_t size, bool) {
  if (address < 0x10000 || address - 0x10000 > memory.size() ||
      size > memory.size() - (address - 0x10000))
    throw std::runtime_error("fixture invalid pointer");
  return memory.data() + address - 0x10000;
}
template <class T> void put(size_t offset, T value) {
  std::memcpy(memory.data() + offset, &value, sizeof(value));
}
uint64_t service(const char *name, uint64_t first = 0, uint64_t third = 0,
                 uint64_t fourth = 0) {
  uint64_t arguments[6] = {first, 0, third, fourth, 0, 0}, result = UINT64_MAX;
  require(dcp_ime_service(name, arguments, &result));
  return result;
}
void seed(bool host = true, uint32_t maximum = 4) {
  reject_write = false;
  DcpImeHooks hooks{
      nullptr,
      [](void *context, uint64_t address, size_t size, bool writing) {
        ++escapes;
        return pointer(context, address, size, writing);
      },
      host};
  if (copying) {
    hooks.validate_memory = [](void *context, uint64_t address, size_t size,
                               bool writing) {
      return pointer(context, address, size, writing) != nullptr;
    };
    hooks.read_memory = [](void *context, uint64_t address, void *output,
                           size_t size) {
      std::memcpy(output, pointer(context, address, size, false), size);
      ++copied_reads;
      return true;
    };
    hooks.write_memory = [](void *context, uint64_t address, const void *input,
                            size_t size) {
      if (reject_write)
        return false;
      std::memcpy(pointer(context, address, size, true), input, size);
      ++copied_writes;
      return true;
    };
  }
  dcp_ime_init(hooks);
  memory.fill(0xa5);
  put<uint32_t>(0x25, maximum);
  put<uint64_t>(0x29, 0x10201);
  put<uint64_t>(0x49, 0x10301);
  const char16_t name[] = u"Old", title[] = u"Name";
  std::memcpy(memory.data() + 0x201, name, sizeof(name));
  std::memcpy(memory.data() + 0x301, title, sizeof(title));
}
std::u16string name() {
  std::u16string value;
  for (size_t i = 0; i <= 4; ++i) {
    char16_t c;
    std::memcpy(&c, memory.data() + 0x201 + 2 * i, 2);
    if (!c)
      break;
    value.push_back(c);
  }
  return value;
}
} // namespace

void exercise() {
  seed();
  require(service("sceImeDialogGetStatus") == 0);
  require(service("sceImeDialogGetResult", 0x10401) == 0x80bc0004);
  require(service("sceImeDialogInit", 0x10001) == 0);
  DcpImeRequest request;
  require(dcp_ime_pending(&request));
  require(request.title == u"Name" && request.text == u"Old" &&
          request.max_units == 4);
  require(service("sceImeDialogInit", 0x10001) == 0x80bc0001);
  require(service("sceImeDialogGetStatus") == 1);
  const std::u16string edited = u"A\U0001f600BC";
  require(!dcp_ime_finish(request.id + 1, &edited));
  require(dcp_ime_finish(request.id, &edited));
  require(!dcp_ime_finish(request.id, nullptr));
  require(name() == u"Old"); // Host completion never touches guest memory.
  require(service("sceImeDialogGetStatus") == 2);
  require(name() == u"A\U0001f600B");
  require(memory[0x200] == 0xa5 && memory[0x20b] == 0xa5);
  require(service("sceImeDialogGetResult", 0x10401) == 0);
  require(memory[0x401] == 0 && memory[0x400] == 0xa5 && memory[0x405] == 0xa5);
  require(service("sceImeDialogTerm") == 0);
  seed();
  require(service("sceImeDialogInit", 0x10001) == 0);
  DcpImeRequest newer;
  require(dcp_ime_pending(&newer) && newer.id != request.id);
  require(!dcp_ime_finish(request.id, &edited));
  require(dcp_ime_finish(newer.id, nullptr));
  require(service("sceImeDialogGetStatus") == 2);
  require(service("sceImeDialogGetResult", 0x10401) == 0);
  require(name() == u"Old" && memory[0x401] == 1);
  seed();
  require(service("sceImeDialogInit", 0x10001) == 0);
  require(dcp_ime_pending(&request));
  require(dcp_ime_finish(request.id, &edited));
  require(service("sceImeDialogAbort") == 0);
  require(service("sceImeDialogGetResult", 0x10401) == 0);
  require(name() == u"Old" && memory[0x401] == 2);
  const std::u16string high(1, char16_t(0xd83d));
  const std::u16string low(1, char16_t(0xde00));
  const std::u16string nul = {u'A', 0, u'B'};
  for (const auto &pair : std::array<std::pair<std::u16string, std::u16string>, 5>{
           std::make_pair(std::u16string(u"ABC\U0001f600"), std::u16string(u"ABC")),
           std::make_pair(high, std::u16string(u"\ufffd")),
           std::make_pair(low, std::u16string(u"\ufffd")),
           std::make_pair(nul, std::u16string(u"A")),
           std::make_pair(std::u16string{}, std::u16string{})}) {
    seed();
    require(service("sceImeDialogInit", 0x10001) == 0);
    require(dcp_ime_pending(&request));
    require(dcp_ime_finish(request.id, &pair.first));
    require(service("sceImeDialogGetStatus") == 2 && name() == pair.second);
  }
  seed(false);
  require(service("sceImeDialogInit", 0x10001) == 0);
  require(service("sceImeDialogGetStatus") == 2 && name() == u"Old");
  require(!dcp_ime_pending(nullptr));
  for (uint32_t maximum : {0u, 2049u, UINT32_MAX}) {
    seed(true, maximum);
    require(service("sceImeDialogInit", 0x10001) == 0x80020016);
    require(service("sceImeDialogGetStatus") == 0);
  }
  seed();
  put<uint64_t>(0x29, 0x107ff);
  bool rejected = false;
  try { service("sceImeDialogInit", 0x10001); }
  catch (const std::runtime_error &) { rejected = true; }
  require(rejected && !dcp_ime_pending(nullptr));
  require(service("sceImeDialogGetPanelSizeExtended", 0, 0x10401, 0x10407) == 0);
  uint32_t width = 0, height = 0;
  std::memcpy(&width, memory.data() + 0x401, 4);
  std::memcpy(&height, memory.data() + 0x407, 4);
  require(width == 793 && height == 408 && memory[0x405] == 0xa5);
  // Queue completion from a separate UI producer while the guest polls.
  for (unsigned i = 0; i < 100; ++i) {
    seed();
    require(service("sceImeDialogInit", 0x10001) == 0);
    require(dcp_ime_pending(&request));
    std::atomic<bool> okay{false};
    std::thread ui([&] {
      std::u16string value = u"Test";
      okay = dcp_ime_finish(request.id, &value);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
      uint64_t arguments[6]{}, value = 0;
      if (!dcp_ime_service("sceImeDialogGetStatus", arguments, &value) ||
          (value != 1 && value != 2) || std::chrono::steady_clock::now() > deadline)
        std::_Exit(1); // Never block in a failed test's thread destructor.
      if (value == 2)
        break;
      std::this_thread::yield();
    }
    ui.join();
    require(okay && name() == u"Test");
  }
  seed();
  const auto before = memory;
  rejected = false;
  try {
    service("sceImeDialogGetPanelSizeExtended", 0, 0x10401, 0x107ff);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected &&
          memory == before); // Validate both outputs before either write.
  if (copying) {
    require(service("sceImeDialogInit", 0x10001) == 0);
    require(dcp_ime_pending(&request));
    require(dcp_ime_finish(request.id, &edited));
    reject_write = true;
    rejected = false;
    try {
      service("sceImeDialogGetStatus");
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    require(rejected && name() == u"Old");
    reject_write = false;
    require(service("sceImeDialogGetStatus") == 2 && name() == u"A\U0001f600B");
    DcpImeHooks partial;
    partial.validate_memory = [](void *, uint64_t, size_t, bool) {
      return true;
    };
    rejected = false;
    try {
      dcp_ime_init(partial);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    require(rejected);
  }
}
int main() {
  exercise();
  require(escapes > 0);
  escapes = 0;
  copying = true;
  exercise();
  require(escapes == 0 && copied_reads > 0 && copied_writes > 0);
  std::cout << "{\"checks\":" << checks
            << ",\"failed\":0,\"concurrent_completions\":200,\"checked_"
               "copies\":true}\n";
}
