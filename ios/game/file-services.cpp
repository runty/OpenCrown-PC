// Guest kernel file semantics adapted from linux-port/src/hle/kernel/file.cpp.
#include "file-services.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
constexpr int kBadFd = 9, kAccess = 13, kInvalid = 22, kFault = 14,
              kNoEntry = 2;
uint64_t error(int value) {
  return uint64_t(int64_t(int32_t(0x80020000u | unsigned(value))));
}
int host_error() {
  switch (errno) {
  case ENOENT:
  case ENOTDIR:
    return kNoEntry;
  case EEXIST:
  case ENOTEMPTY:
    return 17;
  case ENOSPC:
    return 28;
  case EBADF:
    return kBadFd;
  case EINVAL:
    return kInvalid;
  case EFAULT:
    return kFault;
  case EIO:
    return 5;
  default:
    return kAccess;
  }
}
struct Descriptor {
  int value = -1;
  Descriptor() = default;
  explicit Descriptor(int fd) : value(fd) {}
  Descriptor(Descriptor &&other) : value(other.value) { other.value = -1; }
  Descriptor &operator=(Descriptor &&other) {
    if (value >= 0)
      ::close(value);
    value = other.value;
    other.value = -1;
    return *this;
  }
  Descriptor(const Descriptor &) = delete;
  ~Descriptor() {
    if (value >= 0)
      ::close(value);
  }
};
struct File {
  Descriptor descriptor;
  bool writable = false, append = false, savedata = false;
  std::mutex lock;
};
struct Root {
  Descriptor descriptor;
  bool writable = false, case_fallback = false;
};
struct Path {
  Descriptor parent;
  std::string leaf;
  bool writable = false, savedata = false;
  int failure = kNoEntry;
};
DcpFileHooks hooks;
Root game, save, temporary;
std::mutex table_lock;
std::shared_mutex service_gate;
std::map<int32_t, std::shared_ptr<File>> files;
int32_t next_fd = 3;
std::atomic<bool> synchronized_copies{false};
std::atomic<uint64_t> read_bytes{0}, write_bytes{0};

void *pointer(uint64_t address, size_t bytes, bool writing) {
  return hooks.pointer ? hooks.pointer(hooks.context, address, bytes, writing)
                       : nullptr;
}
bool read_memory(uint64_t address, void *output, size_t bytes) {
  if (hooks.read_memory) {
    if (!hooks.read_memory(hooks.context, address, output, bytes))
      return false;
    read_bytes.fetch_add(bytes, std::memory_order_relaxed);
    return true;
  }
  const auto *p = pointer(address, bytes, false);
  if (!p)
    return false;
  std::memcpy(output, p, bytes);
  return true;
}
bool write_memory(uint64_t address, const void *input, size_t bytes) {
  if (hooks.write_memory) {
    if (!hooks.write_memory(hooks.context, address, input, bytes))
      return false;
    write_bytes.fetch_add(bytes, std::memory_order_relaxed);
    return true;
  }
  auto *p = pointer(address, bytes, true);
  if (!p)
    return false;
  std::memcpy(p, input, bytes);
  return true;
}
bool string(uint64_t address, std::string &text) {
  if (!address)
    return false;
  text.clear();
  for (size_t i = 0; i < 4096; ++i) {
    if (address > UINT64_MAX - i)
      return false;
    char value;
    if (!read_memory(address + i, &value, 1))
      return false;
    if (!value)
      return true;
    text.push_back(value);
  }
  return false;
}
std::shared_ptr<File> file(int32_t fd) {
  std::lock_guard<std::mutex> lock(table_lock);
  auto found = files.find(fd);
  return found == files.end() ? nullptr : found->second;
}
std::string case_match(int parent, const std::string &component) {
  struct stat status;
  if (::fstatat(parent, component.c_str(), &status, AT_SYMLINK_NOFOLLOW) == 0)
    return component;
  Descriptor duplicate(
      ::openat(parent, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if (duplicate.value < 0)
    return component;
  DIR *directory = ::fdopendir(duplicate.value);
  if (!directory)
    return component;
  duplicate.value = -1;
  std::string match;
  while (dirent *entry = ::readdir(directory)) {
    if (::strcasecmp(entry->d_name, component.c_str()) == 0) {
      if (!match.empty()) {
        ::closedir(directory);
        return component;
      }
      match = entry->d_name;
    }
  }
  ::closedir(directory);
  return match.empty() ? component : match;
}
Path resolve(const std::string &guest) {
  Path result;
  const auto slash = guest.find('/', 1);
  const std::string mount = guest.substr(0, slash);
  Root *root = mount == "/app0"        ? &game
               : mount == "/savedata0" ? &save
               : mount == "/temp0"     ? &temporary
                                       : nullptr;
  result.savedata = root == &save;
  if (!root || guest.empty() || guest[0] != '/' ||
      guest.find('\\') != std::string::npos ||
      guest.find(':') != std::string::npos)
    return result;
  std::vector<std::string> components;
  for (size_t i = slash; i != std::string::npos && i < guest.size();) {
    size_t end = guest.find('/', i + 1);
    if (end == std::string::npos)
      end = guest.size();
    std::string component = guest.substr(i + 1, end - i - 1);
    i = end;
    if (component == "..") {
      result.failure = kAccess;
      return result;
    }
    if (!component.empty() && component != ".")
      components.push_back(component);
  }
  if (root == &save) {
    if (hooks.savedata_access && !hooks.savedata_access(hooks.context, false))
      return result;
    std::string name = hooks.savedata_directory
                           ? hooks.savedata_directory(hooks.context)
                           : "SAVEDATA00";
    if (name.empty() || name.size() > 31 ||
        name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstu"
                               "vwxyz0123456789_-") != std::string::npos)
      return result;
    result.parent = Descriptor(
        ::open((std::filesystem::path(hooks.save_root) / name).c_str(),
               O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    result.writable =
        root->writable &&
        (!hooks.savedata_access || hooks.savedata_access(hooks.context, true));
  } else {
    result.parent = Descriptor(::dup(root->descriptor.value));
    result.writable = root->writable;
  }
  if (result.parent.value < 0) {
    result.failure = host_error();
    return result;
  }
  for (size_t i = 0; i < components.size(); ++i) {
    const auto component = root->case_fallback
                               ? case_match(result.parent.value, components[i])
                               : components[i];
    if (i + 1 == components.size()) {
      result.leaf = component;
      result.failure = 0;
      return result;
    }
    Descriptor next(::openat(result.parent.value, component.c_str(),
                             O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (next.value < 0) {
      result.failure = host_error();
      return result;
    }
    result.parent = std::move(next);
  }
  result.leaf = ".";
  result.failure = 0;
  return result;
}
Path resolve(uint64_t address) {
  std::string path;
  if (!string(address, path)) {
    Path failure;
    failure.failure = kFault;
    return failure;
  }
  return resolve(path);
}
void fill_stat(void *destination, const struct stat &status, bool writable) {
  auto *out = static_cast<uint8_t *>(destination);
  std::memset(out, 0, 0x78);
  uint16_t mode = uint16_t((S_ISDIR(status.st_mode) ? 0040555 : 0100444) |
                           (writable ? 0222 : 0));
  uint16_t links = 1;
  uint64_t size = uint64_t(status.st_size), blocks = (size + 511) / 512;
  uint32_t block_size = 0x10000;
  std::memcpy(out + 8, &mode, 2);
  std::memcpy(out + 10, &links, 2);
  std::memcpy(out + 0x48, &size, 8);
  std::memcpy(out + 0x50, &blocks, 8);
  std::memcpy(out + 0x58, &block_size, 4);
}
uint64_t open(const uint64_t *a) {
  auto path = resolve(a[0]);
  if (path.failure)
    return error(path.failure);
  uint32_t flags = uint32_t(a[1]);
  const bool writing = (flags & 3) != 0 || (flags & (8 | 0x200 | 0x400));
  if (writing && !path.writable)
    return error(kAccess);
  int native_flags = (writing ? O_RDWR : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW;
  if (flags & 0x200)
    native_flags |= O_CREAT | ((flags & 0x800) ? O_EXCL : 0);
  if (flags & 0x400)
    native_flags |= O_TRUNC;
  int native =
      ::openat(path.parent.value, path.leaf.c_str(), native_flags, 0644);
  if (native < 0)
    return error(host_error());
  auto value = std::make_shared<File>();
  value->descriptor = Descriptor(native);
  value->append = (flags & 8) != 0;
  value->writable = writing && path.writable;
  value->savedata = path.savedata;
  std::lock_guard<std::mutex> lock(table_lock);
  if (next_fd == INT32_MAX)
    return error(24);
  int32_t fd = next_fd++;
  files.emplace(fd, value);
  return uint64_t(fd);
}
uint64_t transfer(const uint64_t *a, bool writing, bool positioned) {
  int32_t fd = int32_t(a[0]);
  uint64_t remaining = a[2], done = 0;
  auto value = file(fd);
  const bool standard = writing && (fd == 1 || fd == 2);
  if (!standard && (!value || (writing && !value->writable)))
    return error(kBadFd);
  if (remaining > uint64_t(INT64_MAX) ||
      (positioned &&
       (int64_t(a[3]) < 0 || a[3] > uint64_t(INT64_MAX) - remaining)))
    return error(kInvalid);
  std::unique_lock<std::mutex> lock;
  if (value) {
    lock = std::unique_lock<std::mutex>(value->lock);
    if (writing && value->append && !positioned &&
        ::lseek(value->descriptor.value, 0, SEEK_END) < 0)
      return error(host_error());
  }
  const bool copying = hooks.validate_memory != nullptr;
  // Preserve 64 MiB validation windows while bounding staging memory and
  // keeping potentially blocking file I/O outside guest access locks.
  std::vector<uint8_t> staging(
      copying ? size_t(std::min<uint64_t>(remaining, 256u << 10)) : 0);
  size_t validated = 0;
  while (remaining) {
    if (a[1] > UINT64_MAX - done)
      return error(kFault);
    const uint64_t address = a[1] + done;
    if (copying && !validated) {
      validated = size_t(std::min<uint64_t>(remaining, 64u << 20));
      if (address > UINT64_MAX - validated ||
          !hooks.validate_memory(hooks.context, address, validated, !writing))
        return error(kFault);
    }
    const size_t bytes = copying
                             ? std::min(validated, staging.size())
                             : size_t(std::min<uint64_t>(remaining, 64u << 20));
    void *buffer = copying ? staging.data() : pointer(address, bytes, !writing);
    if (!buffer)
      return error(kFault);
    if (copying && writing && !read_memory(address, buffer, bytes))
      return error(kFault);
    ssize_t count;
    if (standard)
      count = ::write(fd, buffer, bytes);
    else if (positioned)
      count = writing ? ::pwrite(value->descriptor.value, buffer, bytes,
                                 off_t(a[3] + done))
                      : ::pread(value->descriptor.value, buffer, bytes,
                                off_t(a[3] + done));
    else
      count = writing ? ::write(value->descriptor.value, buffer, bytes)
                      : ::read(value->descriptor.value, buffer, bytes);
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0)
      return error(writing ? host_error() : kFault);
    if (count == 0) {
      if (writing)
        return error(5);
      break;
    }
    if (copying) {
      if (!writing && !write_memory(address, buffer, size_t(count)))
        return error(kFault);
      validated -= size_t(count);
    }
    done += uint64_t(count);
    remaining -= uint64_t(count);
  }
  return done;
}
bool named(const char *a, const char *b) { return std::strcmp(a, b) == 0; }
} // namespace

void dcp_file_reset() {
  std::lock_guard<std::mutex> lock(table_lock);
  files.clear();
  game.descriptor = Descriptor{};
  save.descriptor = Descriptor{};
  temporary.descriptor = Descriptor{};
  next_fd = 3;
  hooks = {};
  synchronized_copies = false;
  read_bytes = write_bytes = 0;
}
DcpFileMemoryStats dcp_file_memory_stats() {
  return {synchronized_copies.load(std::memory_order_relaxed),
          read_bytes.load(std::memory_order_relaxed),
          write_bytes.load(std::memory_order_relaxed)};
}
void dcp_file_init(DcpFileHooks callbacks) {
  dcp_file_reset();
  const unsigned copies = unsigned(callbacks.validate_memory != nullptr) +
                          unsigned(callbacks.read_memory != nullptr) +
                          unsigned(callbacks.write_memory != nullptr);
  if (copies && copies != 3)
    throw std::runtime_error("file memory callbacks must be supplied together");
  if (callbacks.game_root.empty() || callbacks.save_root.empty())
    throw std::runtime_error(
        "file adapter requires game and disposable save roots");
  std::filesystem::create_directories(
      std::filesystem::path(callbacks.save_root) / ".temp0");
  game.descriptor = Descriptor(
      ::open(callbacks.game_root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  temporary.descriptor = Descriptor(
      ::open((std::filesystem::path(callbacks.save_root) / ".temp0").c_str(),
             O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (game.descriptor.value < 0 || temporary.descriptor.value < 0) {
    dcp_file_reset();
    throw std::runtime_error("file adapter cannot open host roots");
  }
  game.writable = false;
  game.case_fallback = true;
  save.writable = temporary.writable = true;
  save.case_fallback = temporary.case_fallback = false;
  hooks = std::move(callbacks);
  synchronized_copies = copies == 3;
}
bool dcp_file_service(const char *name, const uint64_t a[6], uint64_t *result) {
  std::shared_lock<std::shared_mutex> admission(service_gate);
  if (!name || !a || !result)
    return false;
  bool posix = false;
  std::string translated;
  if (name[0] == '_' || named(name, "open") || named(name, "close") ||
      named(name, "read") || named(name, "write") || named(name, "lseek") ||
      named(name, "pread") || named(name, "pwrite") || named(name, "stat") ||
      named(name, "fstat") || named(name, "fsync") ||
      named(name, "ftruncate") || named(name, "mkdir") ||
      named(name, "unlink") || named(name, "rename")) {
    std::string alias = name[0] == '_' ? name + 1 : name;
    static const std::map<std::string, std::string> aliases = {
        {"open", "Open"},     {"close", "Close"},         {"read", "Read"},
        {"write", "Write"},   {"lseek", "Lseek"},         {"pread", "Pread"},
        {"pwrite", "Pwrite"}, {"stat", "Stat"},           {"fstat", "Fstat"},
        {"fsync", "Fsync"},   {"ftruncate", "Ftruncate"}, {"mkdir", "Mkdir"},
        {"unlink", "Unlink"}, {"rename", "Rename"}};
    auto found = aliases.find(alias);
    if (found == aliases.end())
      return false;
    translated = "sceKernel" + found->second;
    name = translated.c_str();
    posix = true;
  }
  if (named(name, "sceKernelOpen"))
    *result = open(a);
  else if (named(name, "sceKernelClose")) {
    std::lock_guard<std::mutex> lock(table_lock);
    *result = files.erase(int32_t(a[0])) ? 0 : error(kBadFd);
  } else if (named(name, "sceKernelRead") || named(name, "sceKernelPread"))
    *result = transfer(a, false, named(name, "sceKernelPread"));
  else if (named(name, "sceKernelWrite") || named(name, "sceKernelPwrite"))
    *result = transfer(a, true, named(name, "sceKernelPwrite"));
  else if (named(name, "sceKernelLseek") || named(name, "sceKernelFsync") ||
           named(name, "sceKernelFtruncate") || named(name, "sceKernelFstat")) {
    auto value = file(int32_t(a[0]));
    if (!value)
      *result = error(kBadFd);
    else {
      std::lock_guard<std::mutex> lock(value->lock);
      if (named(name, "sceKernelLseek")) {
        off_t offset = ::lseek(value->descriptor.value, off_t(a[1]), int(a[2]));
        *result = offset < 0 ? error(kInvalid) : uint64_t(offset);
      } else if (named(name, "sceKernelFsync"))
        *result = value->writable && ::fsync(value->descriptor.value) != 0
                      ? error(host_error())
                      : 0;
      else if (named(name, "sceKernelFtruncate"))
        *result = !value->writable
                      ? error(kAccess)
                      : (::ftruncate(value->descriptor.value, off_t(a[1])) != 0
                             ? error(host_error())
                             : 0);
      else {
        std::array<uint8_t, 0x78> out{};
        struct stat status;
        if (::fstat(value->descriptor.value, &status) != 0)
          *result = error(kBadFd);
        else {
          fill_stat(out.data(), status, value->writable);
          *result =
              write_memory(a[1], out.data(), out.size()) ? 0 : error(kFault);
        }
      }
    }
  } else if (named(name, "sceKernelStat") ||
             named(name, "sceKernelCheckReachability") ||
             named(name, "sceKernelMkdir") || named(name, "sceKernelUnlink") ||
             named(name, "sceKernelRename")) {
    auto path = resolve(a[0]);
    if (path.failure)
      *result = error(path.failure);
    else if (named(name, "sceKernelStat") ||
             named(name, "sceKernelCheckReachability")) {
      struct stat status;
      if (::fstatat(path.parent.value, path.leaf.c_str(), &status,
                    AT_SYMLINK_NOFOLLOW) != 0)
        *result = error(kNoEntry);
      else if (S_ISLNK(status.st_mode))
        *result = error(kAccess);
      else if (named(name, "sceKernelStat")) {
        std::array<uint8_t, 0x78> out{};
        fill_stat(out.data(), status, path.writable);
        *result =
            write_memory(a[1], out.data(), out.size()) ? 0 : error(kFault);
      } else
        *result = 0;
    } else if (!path.writable)
      *result = error(kAccess);
    else if (named(name, "sceKernelMkdir"))
      *result = ::mkdirat(path.parent.value, path.leaf.c_str(), 0755) == 0
                    ? 0
                    : error(host_error());
    else if (named(name, "sceKernelUnlink"))
      *result = ::unlinkat(path.parent.value, path.leaf.c_str(), 0) == 0
                    ? 0
                    : error(host_error());
    else {
      auto to = resolve(a[1]);
      if (to.failure)
        *result = error(to.failure);
      else if (!to.writable)
        *result = error(kAccess);
      else
        *result = ::renameat(path.parent.value, path.leaf.c_str(),
                             to.parent.value, to.leaf.c_str()) == 0
                      ? 0
                      : error(host_error());
    }
  } else
    return false;
  if (posix && int64_t(*result) < 0) {
    if (hooks.set_errno)
      hooks.set_errno(hooks.context, int32_t(*result & 0xffff));
    *result = UINT64_MAX;
  }
  return true;
}

size_t dcp_file_savedata_writers() {
  std::lock_guard<std::mutex> lock(table_lock);
  size_t count = 0;
  for (const auto &entry : files)
    if (entry.second->savedata && entry.second->writable)
      ++count;
  return count;
}

bool dcp_file_with_savedata_quiescent(bool (*operation)(void *),
                                      void *context) {
  if (!operation)
    return false;
  std::unique_lock<std::shared_mutex> exclusive(service_gate);
  if (dcp_file_savedata_writers() != 0)
    return false;
  try {
    return operation(context);
  } catch (...) {
    return false;
  }
}
