// Headless save/pad/audio services, following linux-port/src/hle counterparts.
#include "offline-services.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
DcpOfflineHooks hooks;
std::mutex state_lock, pace_lock;
std::string mounted;
bool mount_writable = false, audio_initialized = false;
uint64_t mount_blocks = 0x8000;
std::array<bool, 4> pads{};
struct Port {
  bool used = false, is_float = false;
  uint32_t frames = 0;
  unsigned channels = 0;
};
std::array<Port, 8> ports{};
Clock::time_point next_audio{};
constexpr uint64_t param_size = 0x530, block_size = 0x8000,
                   default_blocks = 0x8000;
constexpr uint32_t sd_parameter = 0x809f0000, sd_unmounted = 0x809f0004,
                   sd_exists = 0x809f0007, sd_notfound = 0x809f0008,
                   sd_internal = 0x809f000b, sd_full = 0x809f000c;
uint64_t result32(uint32_t value) { return uint64_t(int64_t(int32_t(value))); }
bool named(const char *a, const char *b) { return std::strcmp(a, b) == 0; }
void *pointer(uint64_t address, size_t bytes, bool writing) {
  return hooks.pointer ? hooks.pointer(hooks.context, address, bytes, writing)
                       : nullptr;
}
template <class T> T load(uint64_t address) {
  T value;
  void *p = pointer(address, sizeof(value), false);
  if (!p)
    throw std::runtime_error("offline service rejected guest read");
  std::memcpy(&value, p, sizeof(value));
  return value;
}
template <class T> void store(uint64_t address, T value) {
  void *p = pointer(address, sizeof(value), true);
  if (!p)
    throw std::runtime_error("offline service rejected guest write");
  std::memcpy(p, &value, sizeof(value));
}
std::string string(uint64_t address, size_t limit = 256) {
  if (!address)
    return {};
  std::string out;
  for (size_t i = 0; i < limit; ++i) {
    if (address > UINT64_MAX - i)
      break;
    char c = load<char>(address + i);
    if (!c)
      return out;
    out += c;
  }
  throw std::runtime_error("offline service string exceeds limit");
}
bool valid(const std::string &name) {
  return !name.empty() && name.size() <= 31 &&
         name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrst"
                                "uvwxyz0123456789_-") == std::string::npos;
}
std::filesystem::path directory(const std::string &name) {
  return std::filesystem::path(hooks.save_root) / name;
}
bool actual_directory(const std::filesystem::path &path) {
  std::error_code ec;
  auto status = std::filesystem::symlink_status(path, ec);
  return !ec && std::filesystem::is_directory(status);
}
bool mkdirs(const std::filesystem::path &path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  return !ec && actual_directory(path);
}
bool read_file(const std::filesystem::path &path, void *buffer, size_t size) {
  std::error_code ec;
  if (std::filesystem::is_symlink(std::filesystem::symlink_status(path, ec)))
    return false;
  std::ifstream file(path, std::ios::binary);
  return file &&
         bool(file.read(static_cast<char *>(buffer), std::streamsize(size)));
}
bool write_file(const std::filesystem::path &path, const void *buffer,
                size_t size) {
  std::error_code ec;
  if (std::filesystem::is_symlink(std::filesystem::symlink_status(path, ec)))
    return false;
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file)
    return false;
  file.write(static_cast<const char *>(buffer), std::streamsize(size));
  file.close();
  return bool(file);
}
uint64_t used(const std::filesystem::path &path) {
  uint64_t bytes = 0;
  std::error_code ec;
  for (auto i = std::filesystem::recursive_directory_iterator(path, ec);
       !ec && i != std::filesystem::recursive_directory_iterator();
       i.increment(ec)) {
    auto s = i->symlink_status(ec);
    if (!ec && std::filesystem::is_regular_file(s))
      bytes += i->file_size(ec);
  }
  return (bytes + block_size - 1) / block_size;
}
bool matches(const std::string &pattern, const std::string &name) {
  if (pattern.empty())
    return true;
  size_t p = 0, n = 0, star = std::string::npos, retry = 0;
  while (n < name.size()) {
    if (p < pattern.size() && (pattern[p] == '_' || pattern[p] == name[n])) {
      ++p;
      ++n;
    } else if (p < pattern.size() && pattern[p] == '%') {
      star = p++;
      retry = n;
    } else if (star != std::string::npos) {
      p = star + 1;
      n = ++retry;
    } else
      return false;
  }
  while (p < pattern.size() && pattern[p] == '%')
    ++p;
  return p == pattern.size();
}
bool active(uint64_t address) {
  return !mounted.empty() && string(address) == "/savedata0";
}
uint32_t save_service(const char *name, const uint64_t *a) {
  std::lock_guard<std::mutex> lock(state_lock);
  if (named(name, "sceSaveDataInitialize3"))
    return 0;
  if (named(name, "sceSaveDataTerminate")) {
    mounted.clear();
    mount_writable = false;
    return 0;
  }
  if (named(name, "sceSaveDataMount2")) {
    if (!a[0] || !a[1] || !pointer(a[0], 0x1c, false) ||
        !pointer(a[1], 0x20, true))
      return sd_parameter;
    std::string dir = string(load<uint64_t>(a[0] + 8), 32);
    if (!valid(dir))
      return sd_parameter;
    uint64_t blocks = load<uint64_t>(a[0] + 0x10);
    uint32_t mode = load<uint32_t>(a[0] + 0x18);
    std::error_code link_error;
    if (std::filesystem::is_symlink(
            std::filesystem::symlink_status(directory(dir), link_error)))
      return sd_internal;
    bool exists = actual_directory(directory(dir)), create = mode & (4 | 0x20);
    if (!exists && !create)
      return sd_notfound;
    if (exists && (mode & 4))
      return sd_exists;
    if (!mounted.empty())
      return sd_full;
    if (!exists && !mkdirs(directory(dir) / "sce_sys"))
      return sd_internal;
    mounted = dir;
    mount_writable = (mode & 1) == 0 && (mode & (2 | 4 | 0x20));
    mount_blocks = std::max(blocks, default_blocks);
    auto *out = static_cast<uint8_t *>(pointer(a[1], 0x20, true));
    std::memset(out, 0, 0x20);
    std::memcpy(out, "/savedata0", 11);
    store(a[1] + 0x1c, uint32_t(exists ? 0 : 1));
    return 0;
  }
  if (named(name, "sceSaveDataUmount")) {
    if (!active(a[0]))
      return sd_unmounted;
    mounted.clear();
    mount_writable = false;
    return 0;
  }
  if (named(name, "sceSaveDataDelete")) {
    if (!a[0])
      return sd_parameter;
    std::string dir = string(load<uint64_t>(a[0] + 0x10), 32);
    if (!valid(dir))
      return sd_parameter;
    if (!actual_directory(directory(dir)))
      return sd_notfound;
    std::error_code ec;
    std::filesystem::remove_all(directory(dir), ec);
    return ec ? sd_internal : 0;
  }
  if (named(name, "sceSaveDataDirNameSearch")) {
    if (!a[0] || !a[1] || !pointer(a[0], 0x20, false) ||
        !pointer(a[1], 0x28, true))
      return sd_parameter;
    std::string pattern = string(load<uint64_t>(a[0] + 0x10));
    uint32_t order = load<uint32_t>(a[0] + 0x1c);
    std::vector<std::string> names;
    std::error_code ec;
    for (auto i = std::filesystem::directory_iterator(hooks.save_root, ec);
         !ec && i != std::filesystem::directory_iterator(); i.increment(ec)) {
      std::string dir = i->path().filename().string();
      if (valid(dir) && actual_directory(i->path()) && matches(pattern, dir))
        names.push_back(dir);
    }
    std::sort(names.begin(), names.end());
    if (order == 1)
      std::reverse(names.begin(), names.end());
    uint64_t dirs = load<uint64_t>(a[1] + 8),
             params = load<uint64_t>(a[1] + 0x18),
             infos = load<uint64_t>(a[1] + 0x20);
    uint32_t capacity = load<uint32_t>(a[1] + 0x10),
             count = std::min<uint32_t>(capacity, uint32_t(names.size()));
    for (uint32_t i = 0; dirs && i < count; ++i) {
      auto *out =
          static_cast<char *>(pointer(dirs + uint64_t(i) * 32, 32, true));
      if (!out)
        return sd_parameter;
      std::memset(out, 0, 32);
      std::memcpy(out, names[i].data(), names[i].size());
      if (params) {
        void *p = pointer(params + uint64_t(i) * param_size, param_size, true);
        if (!p)
          return sd_parameter;
        std::memset(p, 0, param_size);
        read_file(directory(names[i]) / "sce_sys/param.bin", p, param_size);
      }
      if (infos) {
        uint64_t address = infos + uint64_t(i) * 0x30;
        void *p = pointer(address, 0x30, true);
        if (!p)
          return sd_parameter;
        std::memset(p, 0, 0x30);
        store(address, default_blocks);
        uint64_t occupied = used(directory(names[i]));
        store(address + 8,
              occupied < default_blocks ? default_blocks - occupied : 0);
      }
    }
    store(a[1], uint32_t(names.size()));
    store(a[1] + 0x14, count);
    return 0;
  }
  if (!active(a[0]))
    return sd_unmounted;
  auto host = directory(mounted);
  if (named(name, "sceSaveDataGetMountInfo")) {
    void *out = pointer(a[1], 0x30, true);
    if (!out)
      return sd_unmounted;
    std::memset(out, 0, 0x30);
    store(a[1], mount_blocks);
    uint64_t occupied = used(host);
    store(a[1] + 8, occupied < mount_blocks ? mount_blocks - occupied : 0);
    return 0;
  }
  if (named(name, "sceSaveDataSetParam")) {
    if (!a[2] || !mount_writable)
      return sd_parameter;
    std::array<uint8_t, param_size> data{};
    read_file(host / "sce_sys/param.bin", data.data(), data.size());
    size_t size = size_t(std::min<uint64_t>(a[3], param_size));
    void *in = pointer(a[2], size, false);
    if (!in)
      return sd_parameter;
    if (a[1] == 0)
      std::memcpy(data.data(), in, size);
    return mkdirs(host / "sce_sys") && write_file(host / "sce_sys/param.bin",
                                                  data.data(), data.size())
               ? 0
               : sd_internal;
  }
  if (named(name, "sceSaveDataGetParam")) {
    std::array<uint8_t, param_size> data{};
    if (!read_file(host / "sce_sys/param.bin", data.data(), data.size()))
      return sd_notfound;
    if (a[1] != 0)
      return sd_parameter;
    size_t size = size_t(std::min<uint64_t>(a[3], param_size));
    void *out = pointer(a[2], size, true);
    if (!out)
      return sd_parameter;
    std::memcpy(out, data.data(), size);
    if (a[4])
      store(a[4], uint64_t(size));
    return 0;
  }
  if (named(name, "sceSaveDataSaveIcon")) {
    if (!a[1] || !mount_writable)
      return sd_parameter;
    uint64_t buffer = load<uint64_t>(a[1]), size = load<uint64_t>(a[1] + 0x10);
    if (!buffer || size > (16u << 20))
      return sd_parameter;
    void *p = pointer(buffer, size, false);
    if (!p)
      return sd_parameter;
    return mkdirs(host / "sce_sys") &&
                   write_file(host / "sce_sys/icon0.png", p, size)
               ? 0
               : sd_internal;
  }
  if (named(name, "sceSaveDataLoadIcon")) {
    if (!a[1])
      return sd_parameter;
    std::error_code ec;
    uint64_t size = std::filesystem::file_size(host / "sce_sys/icon0.png", ec);
    if (ec)
      return sd_notfound;
    uint64_t buffer = load<uint64_t>(a[1]), capacity = load<uint64_t>(a[1] + 8);
    store(a[1] + 0x10, size);
    if (size > capacity || size > (16u << 20))
      return sd_parameter;
    void *p = pointer(buffer, size, true);
    return p && read_file(host / "sce_sys/icon0.png", p, size) ? 0
                                                               : sd_internal;
  }
  return sd_parameter;
}
uint32_t pad_service(const char *name, const uint64_t *a) {
  std::lock_guard<std::mutex> lock(state_lock);
  if (named(name, "scePadInit"))
    return 0;
  if (named(name, "scePadOpen") || named(name, "scePadGetHandle")) {
    int64_t slot = int32_t(a[0]) - 1000;
    if (slot < 0 || slot >= 4 || int32_t(a[1]) != 0 || int32_t(a[2]) != 0)
      return 0x80920001;
    if (named(name, "scePadOpen"))
      pads[size_t(slot)] = true;
    return pads[size_t(slot)] ? uint32_t(slot + 1) : 0x80920001;
  }
  if (a[0] < 1 || a[0] > 4 || !pads[size_t(a[0] - 1)])
    return 0x80920003;
  if (named(name, "scePadClose")) {
    pads[size_t(a[0] - 1)] = false;
    return 0;
  }
  if (named(name, "scePadReadState")) {
    auto *out = static_cast<uint8_t *>(pointer(a[1], 0x78, true));
    if (!out)
      return 0x80920003;
    std::memset(out, 0, 0x78);
    out[4] = out[5] = out[6] = out[7] = 0x80;
    float orientation = 1;
    std::memcpy(out + 0x18, &orientation, 4);
    out[0x4c] = out[0x68] = 1;
    uint64_t now =
        uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                     Clock::now().time_since_epoch())
                     .count());
    std::memcpy(out + 0x50, &now, 8);
    uint32_t buttons = 0;
    uint64_t frame = hooks.vblank_count ? hooks.vblank_count(hooks.context) : 0;
    if (hooks.scripted_input && a[0] == 1) {
      if (frame >= 300 && frame < 310)
        buttons = 8;
      else if (frame >= 600 && frame <= 3488 && (frame - 600) % 180 < 8)
        buttons = 0x4000;
    }
    std::memcpy(out, &buttons, 4);
    return 0;
  }
  if (named(name, "scePadGetControllerInformation")) {
    auto *out = static_cast<uint8_t *>(pointer(a[1], 0x1c, true));
    if (!out)
      return 0x80920003;
    std::memset(out, 0, 0x1c);
    float density = 44.86f;
    uint16_t x = 1920, y = 943;
    std::memcpy(out, &density, 4);
    std::memcpy(out + 4, &x, 2);
    std::memcpy(out + 6, &y, 2);
    out[8] = out[9] = 0x0d;
    out[0xb] = out[0xc] = 1;
    return 0;
  }
  if (named(name, "scePadSetVibration"))
    return pointer(a[1], 2, false) ? 0 : 0x80920003;
  if (named(name, "scePadSetLightBar"))
    return pointer(a[1], 3, false) ? 0 : 0x80920003;
  return 0;
}
uint32_t audio_service(const char *name, const uint64_t *a) {
  uint32_t frames = 0;
  {
    std::lock_guard<std::mutex> lock(state_lock);
    if (named(name, "sceAudioOutInit")) {
      audio_initialized = true;
      return 0;
    }
    if (named(name, "sceAudioOutOpen")) {
      if (!audio_initialized)
        return 0x80260001;
      uint32_t format = uint32_t(a[5]) & 0xff;
      if (format > 7 || !uint32_t(a[3]) || uint32_t(a[4]) != 48000)
        return 0x80260002;
      const unsigned channels[] = {1, 2, 8, 1, 2, 8, 8, 8};
      for (size_t i = 0; i < ports.size(); ++i)
        if (!ports[i].used) {
          ports[i] = {true, format >= 3 && format != 6, uint32_t(a[3]),
                      channels[format]};
          return uint32_t(i + 1);
        }
      return 0x8026000a;
    }
    if (named(name, "sceAudioOutClose")) {
      if (a[0] < 1 || a[0] > 8 || !ports[size_t(a[0] - 1)].used)
        return 0x80260004;
      ports[size_t(a[0] - 1)] = {};
      return 0;
    }
    const bool batch = named(name, "sceAudioOutOutputs");
    if (!batch && !a[1])
      return 0;
    uint32_t count = batch ? uint32_t(a[1]) : 1;
    if (!count || count > 8 || (batch && !a[0]))
      return 0x80260002;
    for (uint32_t i = 0; i < count; ++i) {
      uint64_t handle = batch ? uint32_t(load<int32_t>(a[0] + uint64_t(i) * 16))
                              : a[0],
               buffer =
                   batch ? load<uint64_t>(a[0] + uint64_t(i) * 16 + 8) : a[1];
      if (handle < 1 || handle > 8 || !ports[size_t(handle - 1)].used)
        return 0x80260004;
      auto &port = ports[size_t(handle - 1)];
      frames = port.frames;
      if (buffer && !pointer(buffer,
                             size_t(port.frames) * port.channels *
                                 (port.is_float ? 4 : 2),
                             false))
        return 0x80260002;
    }
  }
  std::lock_guard<std::mutex> pace(pace_lock);
  auto now = Clock::now();
  if (next_audio == Clock::time_point{} ||
      now - next_audio > std::chrono::milliseconds(100))
    next_audio = now;
  next_audio += std::chrono::nanoseconds(uint64_t(frames) * 1000000000 / 48000);
  while (Clock::now() < next_audio &&
         !(hooks.stopping && hooks.stopping(hooks.context)))
    std::this_thread::sleep_until(
        std::min(next_audio, Clock::now() + std::chrono::milliseconds(10)));
  return 0;
}
} // namespace

void dcp_offline_init(DcpOfflineHooks callbacks) {
  std::lock_guard<std::mutex> lock(state_lock);
  hooks = std::move(callbacks);
  if (hooks.save_root.empty())
    throw std::runtime_error("offline adapter requires a disposable save root");
  std::filesystem::create_directories(hooks.save_root);
  mounted.clear();
  mount_writable = false;
  mount_blocks = default_blocks;
  pads = {};
  ports = {};
  audio_initialized = false;
  next_audio = {};
}
bool dcp_offline_savedata_access(bool writing) {
  std::lock_guard<std::mutex> lock(state_lock);
  return !mounted.empty() && (!writing || mount_writable);
}
std::string dcp_offline_savedata_directory() {
  std::lock_guard<std::mutex> lock(state_lock);
  return mounted;
}
bool dcp_offline_service(const char *name, const uint64_t a[6],
                         uint64_t *result) {
  if (!name || !a || !result)
    return false;
  static const char *save_calls[] = {
      "sceSaveDataInitialize3",  "sceSaveDataTerminate",
      "sceSaveDataMount2",       "sceSaveDataUmount",
      "sceSaveDataGetMountInfo", "sceSaveDataSetParam",
      "sceSaveDataGetParam",     "sceSaveDataSaveIcon",
      "sceSaveDataLoadIcon",     "sceSaveDataDelete",
      "sceSaveDataDirNameSearch"};
  static const char *pad_calls[] = {
      "scePadInit",          "scePadOpen",
      "scePadClose",         "scePadGetHandle",
      "scePadReadState",     "scePadGetControllerInformation",
      "scePadSetVibration",  "scePadSetLightBar",
      "scePadResetLightBar", "scePadResetOrientation"};
  static const char *audio_calls[] = {"sceAudioOutInit", "sceAudioOutOpen",
                                      "sceAudioOutClose", "sceAudioOutOutput",
                                      "sceAudioOutOutputs"};
  for (auto call : save_calls)
    if (named(name, call)) {
      *result = result32(save_service(name, a));
      return true;
    }
  for (auto call : pad_calls)
    if (named(name, call)) {
      *result = result32(pad_service(name, a));
      return true;
    }
  for (auto call : audio_calls)
    if (named(name, call)) {
      *result = result32(audio_service(name, a));
      return true;
    }
  return false;
}
