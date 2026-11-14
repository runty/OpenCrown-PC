// Native regression for save snapshot admission versus in-flight
// open/write/close.
#include "file-services.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <unistd.h>

using namespace std::chrono_literals;
namespace fs = std::filesystem;
namespace {
struct Gate {
  std::mutex lock;
  std::condition_variable changed;
  bool entered = false, released = false;
  void block() {
    std::unique_lock<std::mutex> guard(lock);
    entered = true;
    changed.notify_all();
    if (!changed.wait_for(guard, 5s, [&] { return released; }))
      throw std::runtime_error("fixture gate timed out");
  }
  void wait() {
    std::unique_lock<std::mutex> guard(lock);
    if (!changed.wait_for(guard, 5s, [&] { return entered; }))
      throw std::runtime_error("fixture call did not arrive");
  }
  void release() {
    std::lock_guard<std::mutex> guard(lock);
    released = true;
    changed.notify_all();
  }
};
struct Pointers {
  std::atomic<uint64_t> blocked{0};
  Gate *gate = nullptr;
};
uint64_t service(const char *name, uint64_t a = 0, uint64_t b = 0,
                 uint64_t c = 0) {
  const uint64_t arguments[6] = {a, b, c};
  uint64_t result;
  if (!dcp_file_service(name, arguments, &result))
    throw std::runtime_error("unhandled file fixture service");
  return result;
}
std::string contents(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}
unsigned passed = 0;
void check(bool okay, const char *message) {
  if (!okay)
    throw std::runtime_error(message);
  ++passed;
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Usage: file-quiescence EXTERNAL_OUTPUT");
    const auto root =
        fs::path(argv[1]) / ("quiescence-" + std::to_string(getpid()));
    fs::create_directories(root / "game");
    fs::create_directories(root / "saves/slot");
    Pointers pointers;
    DcpFileHooks hooks;
    hooks.context = &pointers;
    hooks.pointer = [](void *context, uint64_t address, size_t,
                       bool) -> void * {
      auto *state = static_cast<Pointers *>(context);
      if (state->blocked.load() == address && state->gate)
        state->gate->block();
      return reinterpret_cast<void *>(address);
    };
    hooks.game_root = (root / "game").string();
    hooks.save_root = (root / "saves").string();
    hooks.savedata_access = [](void *, bool) { return true; };
    hooks.savedata_directory = [](void *) { return std::string("slot"); };
    dcp_file_init(hooks);
    const char path[] = "/savedata0/data.bin";
    auto fd = service("sceKernelOpen", uint64_t(path), 2 | 0x200);
    check(int64_t(fd) >= 3, "writable open failed");
    unsigned callbacks = 0;
    auto count = [](void *context) {
      ++*static_cast<unsigned *>(context);
      return true;
    };
    check(!dcp_file_with_savedata_quiescent(count, &callbacks) &&
              callbacks == 0,
          "snapshot admitted with a writable descriptor");
    Gate writing;
    pointers.gate = &writing;
    const char data[] = "abc";
    pointers.blocked = uint64_t(data);
    auto write = std::async(std::launch::async, [&] {
      return service("sceKernelWrite", fd, uint64_t(data), 3);
    });
    writing.wait();
    check(service("sceKernelClose", fd) == 0, "concurrent close failed");
    check(dcp_file_savedata_writers() == 0,
          "closed descriptor remains in table");
    std::promise<void> started;
    auto transaction = std::async(std::launch::async, [&] {
      started.set_value();
      return dcp_file_with_savedata_quiescent(count, &callbacks);
    });
    started.get_future().wait();
    const bool writeBlockedSnapshot =
        transaction.wait_for(25ms) == std::future_status::timeout;
    writing.release();
    check(write.get() == 3, "in-flight write failed after close");
    check(transaction.get() && writeBlockedSnapshot && callbacks == 1,
          "snapshot did not wait for erased-but-live write descriptor");
    check(contents(root / "saves/slot/data.bin") == "abc",
          "snapshot boundary preceded the write");
    // An open which passed admission but has not inserted its descriptor must
    // complete before the exclusive check decides whether writers remain.
    Gate opening;
    pointers.gate = &opening;
    pointers.blocked = uint64_t(path);
    auto open = std::async(std::launch::async, [&] {
      return service("sceKernelOpen", uint64_t(path), 2);
    });
    opening.wait();
    auto pending = std::async(std::launch::async, [&] {
      return dcp_file_with_savedata_quiescent(count, &callbacks);
    });
    const bool openBlockedSnapshot =
        pending.wait_for(25ms) == std::future_status::timeout;
    opening.release();
    auto opened = open.get();
    check(int64_t(opened) >= 3 && !pending.get() && openBlockedSnapshot &&
              callbacks == 1,
          "snapshot crossed an in-flight open admission");
    check(service("sceKernelClose", opened) == 0,
          "close after open fixture failed");
    pointers.blocked = 0;
    pointers.gate = nullptr;
    // Hold the snapshot's exclusive gate: subsequent file calls wait outside
    // it.
    Gate exclusive;
    auto snapshot = std::async(std::launch::async, [&] {
      return dcp_file_with_savedata_quiescent(
          [](void *context) {
            static_cast<Gate *>(context)->block();
            return true;
          },
          &exclusive);
    });
    exclusive.wait();
    auto after = std::async(std::launch::async, [&] {
      return service("sceKernelOpen", uint64_t(path), 0);
    });
    const bool snapshotBlockedOpen =
        after.wait_for(25ms) == std::future_status::timeout;
    exclusive.release();
    check(snapshot.get() && snapshotBlockedOpen,
          "file call crossed an active snapshot");
    auto readOnly = after.get();
    check(int64_t(readOnly) >= 3, "read-only open failed");
    check(dcp_file_with_savedata_quiescent(count, &callbacks) && callbacks == 2,
          "read-only descriptor incorrectly blocked checkpoint");
    check(service("sceKernelClose", readOnly) == 0, "read-only close failed");
    check(!dcp_file_with_savedata_quiescent(nullptr, nullptr),
          "null snapshot callback accepted");
    check(!dcp_file_with_savedata_quiescent(
              [](void *) -> bool { throw std::runtime_error("expected"); },
              nullptr),
          "snapshot exception escaped or was accepted");
    dcp_file_reset();
    fs::remove_all(root);
    std::cout << "{\"passed\":" << passed << ",\"failed\":0}\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
