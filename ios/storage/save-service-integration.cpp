// Real offline/file adapters and local transactions, without translated code.
#include "../game/file-services.h"
#include "../game/offline-services.h"
#include "save-transactions.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace fs = std::filesystem;
using dcp::storage::SaveTransactions;
namespace {
unsigned passed = 0;
void expect(bool okay, const char *message) {
  if (!okay)
    throw std::runtime_error(message);
  ++passed;
}
std::string read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}
struct Runtime {
  std::array<uint8_t, 8192> memory{};
  SaveTransactions transactions;
  fs::path root;
  unsigned begins = 0, commits = 0;
  Runtime(fs::path path)
      : transactions(path / "ProbeSaves", path / "SaveCheckpoints"),
        root(std::move(path)) {
    fs::create_directories(root / "Game");
    expect(transactions.recover(), "startup recovery failed");
    DcpOfflineHooks offline;
    offline.context = this;
    offline.pointer = pointer;
    offline.save_root = (root / "ProbeSaves").string();
    offline.savedata_transaction = [](void *context, bool begin) {
      struct Request {
        Runtime *runtime;
        bool begin;
      } request{static_cast<Runtime *>(context), begin};
      return dcp_file_with_savedata_quiescent(
          [](void *operation) {
            auto &request = *static_cast<Request *>(operation);
            if (request.begin) {
              ++request.runtime->begins;
              return request.runtime->transactions.begin();
            }
            ++request.runtime->commits;
            return request.runtime->transactions.commit();
          },
          &request);
    };
    dcp_offline_init(offline);
    DcpFileHooks files;
    files.context = this;
    files.pointer = pointer;
    files.game_root = (root / "Game").string();
    files.save_root = (root / "ProbeSaves").string();
    files.savedata_access = [](void *, bool writing) {
      return dcp_offline_savedata_access(writing);
    };
    files.savedata_directory = [](void *) {
      return dcp_offline_savedata_directory();
    };
    dcp_file_init(files);
  }
  ~Runtime() { dcp_file_reset(); }
  static void *pointer(void *context, uint64_t address, size_t bytes, bool) {
    auto &runtime = *static_cast<Runtime *>(context);
    const auto base = uint64_t(runtime.memory.data());
    return address >= base && bytes <= runtime.memory.size() &&
                   address - base <= runtime.memory.size() - bytes
               ? reinterpret_cast<void *>(address)
               : nullptr;
  }
  uint64_t address(size_t offset) { return uint64_t(memory.data() + offset); }
  template <class T> void store(size_t offset, T value) {
    std::memcpy(memory.data() + offset, &value, sizeof(value));
  }
  uint64_t text(size_t offset, const std::string &text) {
    expect(text.size() + 1 <= memory.size() - offset,
           "fixture text out of bounds");
    std::memcpy(memory.data() + offset, text.c_str(), text.size() + 1);
    return address(offset);
  }
  uint64_t call(const char *name, uint64_t a = 0, uint64_t b = 0,
                uint64_t c = 0) {
    uint64_t arguments[6] = {a, b, c}, result = UINT64_MAX;
    bool handled = dcp_offline_service(name, arguments, &result) ||
                   dcp_file_service(name, arguments, &result);
    expect(handled, "unhandled integration service");
    return result;
  }
  void mount(uint32_t mode) {
    store(0, uint32_t(1000));
    store(8, text(128, "SAVEDATA00"));
    store(16, uint64_t(0x8000));
    store(24, mode);
    expect(call("sceSaveDataMount2", address(0), address(256)) == 0,
           "savedata mount failed");
    expect(std::strcmp(reinterpret_cast<char *>(memory.data() + 256),
                       "/savedata0") == 0,
           "mountpoint result mismatch");
  }
  uint64_t open(bool writing) {
    auto fd = call("sceKernelOpen", text(320, "/savedata0/data.bin"),
                   writing ? 0x602 : 0);
    expect(int64_t(fd) >= 3, "guest savedata open failed");
    return fd;
  }
  void write(uint64_t fd, const std::string &data) {
    expect(call("sceKernelWrite", fd, text(512, data), data.size()) ==
               data.size(),
           "guest savedata write failed");
  }
  void close(uint64_t fd) {
    expect(call("sceKernelClose", fd) == 0, "guest savedata close failed");
  }
  uint64_t unmount() { return call("sceSaveDataUmount", address(256)); }
  void verify(const std::string &data) {
    mount(1);
    auto fd = open(false);
    auto bytes = call("sceKernelRead", fd, address(1024), 256);
    expect(bytes == data.size() &&
               std::memcmp(memory.data() + 1024, data.data(), data.size()) == 0,
           "reopened save bytes mismatch");
    close(fd);
    expect(unmount() == 0, "readonly unmount failed");
    expect(!transactions.active(),
           "readonly mount created a write transaction");
  }
};
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error(
          "Usage: save-service-integration EXTERNAL_OUTPUT");
    const auto root =
        fs::path(argv[1]) / ("services-" + std::to_string(getpid()));
    {
      Runtime runtime(root);
      runtime.mount(2 | 0x20);
      expect(runtime.transactions.active() && runtime.begins == 1,
             "writable mount did not begin transaction");
      auto fd = runtime.open(true);
      runtime.write(fd, "complete-v1");
      expect(uint32_t(runtime.unmount()) == 0x809f000b && runtime.commits == 0,
             "unmount committed with an open writer");
      expect(dcp_offline_savedata_access(true) && runtime.transactions.active(),
             "rejected unmount lost the writable transaction");
      runtime.close(fd);
      expect(runtime.unmount() == 0 && runtime.commits == 1 &&
                 !runtime.transactions.active(),
             "closed save did not commit");
    }
    {
      Runtime runtime(root);
      runtime.verify("complete-v1");
      runtime.mount(2);
      auto fd = runtime.open(true);
      runtime.write(fd, "complete-v2");
      runtime.close(fd);
      expect(runtime.unmount() == 0 && !runtime.transactions.active(),
             "second committed save failed");
    }
    {
      Runtime runtime(root);
      runtime.verify("complete-v2");
      runtime.mount(2);
      auto fd = runtime.open(true);
      runtime.write(fd, "interrupted-v3");
      expect(runtime.transactions.active(),
             "interrupted session journal missing");
      // Destruction closes descriptors without Umount, like a process
      // terminating.
    }
    {
      Runtime runtime(root);
      expect(!runtime.transactions.active() &&
                 !runtime.transactions.last_recovery_path().empty(),
             "relaunch did not recover interrupted session");
      expect(read(fs::path(runtime.transactions.last_recovery_path()) /
                  "SAVEDATA00/data.bin") == "interrupted-v3",
             "partial write not preserved");
      runtime.verify("complete-v2");
      expect(!dcp_offline_savedata_access(true),
             "readonly unmount left writable access");
    }
    {
      Runtime runtime(root);
      expect(runtime.transactions.last_recovery_path().empty(),
             "recovery repeated on a clean relaunch");
      runtime.verify("complete-v2");
    }
    fs::remove_all(root);
    std::cout << "{\"passed\":" << passed
              << ",\"failed\":0,\"relaunches\":4,\"interrupted_write_"
                 "recovered\":true}\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
