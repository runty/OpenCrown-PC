// Headless save/pad/audio services, following linux-port/src/hle counterparts.
#include "offline-services.h"
#include "../../linux-port/src/hle/kernel/pure.h"
#include "../../linux-port/src/hle/rtc.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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
std::vector<dcp::hle::InputEvent> input_events, scene_events;
std::mutex state_lock, pace_lock, save_lock;
std::string mounted;
bool mount_writable = false, mount_transition = false,
     audio_initialized = false;
uint64_t mount_blocks = 0x8000;
uint8_t mount_metadata_failures = 0;
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
bool transaction(std::unique_lock<std::mutex> &lock, bool begin) {
  if (!hooks.savedata_transaction)
    return true;
  mount_transition = true;
  lock.unlock();
  bool okay = false;
  try {
    okay = hooks.savedata_transaction(hooks.context, begin);
  } catch (...) {
    okay = false;
  }
  lock.lock();
  // Caller retains the transition until admission/commit or failure restoration
  // is complete under this same state_lock acquisition.
  return okay;
}
uint32_t save_service(const char *name, const uint64_t *a) {
  // Retain save-operation ordering while releasing state_lock for host
  // snapshots.
  std::lock_guard<std::mutex> operation(save_lock);
  std::unique_lock<std::mutex> lock(state_lock);
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
    bool writable = (mode & 1) == 0 && (mode & (2 | 4 | 0x20));
    if (writable && !transaction(lock, true)) {
      mount_transition = false;
      return sd_internal;
    }
    if (!exists && !mkdirs(directory(dir) / "sce_sys")) {
      mount_transition = false;
      return sd_internal;
    }
    mounted = dir;
    mount_writable = writable;
    mount_blocks = std::max(blocks, default_blocks);
    mount_metadata_failures = 0;
    mount_transition = false;
    auto *out = static_cast<uint8_t *>(pointer(a[1], 0x20, true));
    std::memset(out, 0, 0x20);
    std::memcpy(out, "/savedata0", 11);
    store(a[1] + 0x1c, uint32_t(exists ? 0 : 1));
    return 0;
  }
  if (named(name, "sceSaveDataUmount")) {
    if (!active(a[0]))
      return sd_unmounted;
    if (mount_writable && mount_metadata_failures)
      return sd_internal;
    if (mount_writable && !transaction(lock, false)) {
      // Restore the existing mount for a guest retry; publish this together
      // with the transition ending, after the host gate/callback has returned.
      mount_transition = false;
      return sd_internal;
    }
    mounted.clear();
    mount_writable = false;
    mount_transition = false;
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
    bool okay =
        mkdirs(host / "sce_sys") &&
        write_file(host / "sce_sys/param.bin", data.data(), data.size());
    if (okay)
      mount_metadata_failures &= uint8_t(~1u);
    else
      mount_metadata_failures |= 1;
    return okay ? 0 : sd_internal;
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
    bool okay = mkdirs(host / "sce_sys") &&
                write_file(host / "sce_sys/icon0.png", p, size);
    if (okay)
      mount_metadata_failures &= uint8_t(~2u);
    else
      mount_metadata_failures |= 2;
    return okay ? 0 : sd_internal;
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
  std::unique_lock<std::mutex> lock(state_lock);
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
  if (named(name, "scePadReadState") ||
      named(name, "scePadGetControllerInformation")) {
    const unsigned slot = unsigned(a[0] - 1);
    lock.unlock();
    DcpPlatformPad pad{};
    pad.connected = 1;
    pad.lx = pad.ly = pad.rx = pad.ry = 128;
    if (hooks.input_read) {
      hooks.input_read(hooks.context, slot, &pad);
      pad.touch_count = std::min<uint8_t>(pad.touch_count, 2);
      if (!pad.connected) {
        pad = {};
        pad.lx = pad.ly = pad.rx = pad.ry = 128;
      }
    } else if (hooks.scripted_input && slot == 0) {
      if (hooks.scripted_input_active && !hooks.scripted_input_active(hooks.context)) {
        if (hooks.scene_elapsed_frames && !scene_events.empty())
          pad.buttons = dcp::hle::script_buttons_at(scene_events, hooks.scene_elapsed_frames(hooks.context));
      } else {
        uint64_t frame = hooks.vblank_count ? hooks.vblank_count(hooks.context) : 0;
        if (!hooks.input_script.empty())
          pad.buttons = dcp::hle::script_buttons_at(input_events, frame);
        else if (frame >= 300 && frame < 310)
          pad.buttons = 8;
        else if (frame >= 600 && frame <= 3488 && (frame - 600) % 180 < 8)
          pad.buttons = 0x4000;
      }
      if (pad.buttons & dcp::hle::kScriptStickLeft) pad.lx = 0;
      if (pad.buttons & dcp::hle::kScriptStickRight) pad.lx = 255;
      if (pad.buttons & dcp::hle::kScriptStickUp) pad.ly = 0;
      if (pad.buttons & dcp::hle::kScriptStickDown) pad.ly = 255;
      if (pad.buttons & dcp::hle::kScriptRightStickLeft) pad.rx = 0;
      if (pad.buttons & dcp::hle::kScriptRightStickRight) pad.rx = 255;
      if (pad.buttons & dcp::hle::kScriptRightStickUp) pad.ry = 0;
      if (pad.buttons & dcp::hle::kScriptRightStickDown) pad.ry = 255;
      pad.buttons &= ~dcp::hle::kScriptStickMask;
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
      out[0xb] = 1;
      out[0xc] = bool(pad.connected);
      return 0;
    }
    auto *out = static_cast<uint8_t *>(pointer(a[1], 0x78, true));
    if (!out)
      return 0x80920003;
    std::memset(out, 0, 0x78);
    std::memcpy(out, &pad.buttons, 4);
    out[4] = pad.lx;
    out[5] = pad.ly;
    out[6] = pad.rx;
    out[7] = pad.ry;
    out[8] = pad.l2;
    out[9] = pad.r2;
    float orientation = 1;
    std::memcpy(out + 0x18, &orientation, 4);
    out[0x34] = pad.touch_count;
    for (unsigned i = 0; i < pad.touch_count; ++i) {
      uint16_t x = std::min<uint16_t>(pad.tx[i], 1919);
      uint16_t y = std::min<uint16_t>(pad.ty[i], 942);
      std::memcpy(out + 0x3c + i * 8, &x, 2);
      std::memcpy(out + 0x3e + i * 8, &y, 2);
      out[0x40 + i * 8] = pad.tid[i] & 127;
    }
    out[0x4c] = bool(pad.connected);
    out[0x68] = 1;
    uint64_t now =
        uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                     Clock::now().time_since_epoch())
                     .count());
    std::memcpy(out + 0x50, &now, 8);
    return 0;
  }
  if (named(name, "scePadSetVibration"))
    return pointer(a[1], 2, false) ? 0 : 0x80920003;
  if (named(name, "scePadSetLightBar"))
    return pointer(a[1], 3, false) ? 0 : 0x80920003;
  return 0;
}
// The null sink also provides a bounded fallback when a real sink is inactive.
// pace_lock serializes audio operations, never the shared save/pad state lock.
void pace_audio(uint32_t frames) {
  if (!frames)
    return;
  auto now = Clock::now();
  if (next_audio == Clock::time_point{} ||
      now - next_audio > std::chrono::milliseconds(100))
    next_audio = now;
  next_audio += std::chrono::nanoseconds(uint64_t(frames) * 1000000000 / 48000);
  while (Clock::now() < next_audio &&
         !(hooks.stopping && hooks.stopping(hooks.context)))
    std::this_thread::sleep_until(
        std::min(next_audio, Clock::now() + std::chrono::milliseconds(10)));
}
uint32_t audio_service(const char *name, const uint64_t *a) {
  constexpr uint32_t max_frames = 4096;
  std::lock_guard<std::mutex> pace(pace_lock);
  uint32_t frames = 0;
  struct Source {
    Port port;
    const uint8_t *bytes = nullptr;
  };
  std::array<Source, 8> sources{};
  uint32_t count = 0;
  bool start = false, stop = false;
  uint32_t opened = 0;
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
      if (format > 7 || !a[3] || a[3] > max_frames || a[4] != 48000)
        return 0x80260002;
      const unsigned channels[] = {1, 2, 8, 1, 2, 8, 8, 8};
      start = std::none_of(ports.begin(), ports.end(),
                           [](const Port &p) { return p.used; });
      for (size_t i = 0; i < ports.size(); ++i)
        if (!ports[i].used) {
          ports[i] = {true, format >= 3 && format != 6, uint32_t(a[3]),
                      channels[format]};
          opened = uint32_t(i + 1);
          break;
        }
      if (!opened)
        return 0x8026000a;
    } else if (named(name, "sceAudioOutClose")) {
      if (a[0] < 1 || a[0] > 8 || !ports[size_t(a[0] - 1)].used)
        return 0x80260004;
      ports[size_t(a[0] - 1)] = {};
      stop = std::none_of(ports.begin(), ports.end(),
                          [](const Port &p) { return p.used; });
    } else {
      const bool batch = named(name, "sceAudioOutOutputs");
      // The guest uses single null Output as a flush before Close.
      if (!batch && !a[1])
        return 0;
      if (batch && (!a[0] || !a[1] || a[1] > 8))
        return 0x80260002;
      count = batch ? uint32_t(a[1]) : 1;
      const auto *table = batch ? static_cast<const uint8_t *>(
                                      pointer(a[0], size_t(count) * 16, false))
                                : nullptr;
      if (batch && !table)
        return 0x80260002;
      std::array<bool, 8> seen{};
      for (uint32_t i = 0; i < count; ++i) {
        uint64_t handle = a[0], buffer = a[1];
        if (batch) {
          int32_t guest_handle;
          std::memcpy(&guest_handle, table + i * 16, 4);
          handle = uint32_t(guest_handle);
          std::memcpy(&buffer, table + i * 16 + 8, 8);
        }
        if (handle < 1 || handle > 8 || !ports[size_t(handle - 1)].used)
          return 0x80260004;
        if (seen[size_t(handle - 1)])
          return 0x80260002;
        seen[size_t(handle - 1)] = true;
        const Port port = ports[size_t(handle - 1)];
        // Mixing different frame counts would overrun a shorter input buffer.
        if (i && frames != port.frames)
          return 0x80260002;
        frames = port.frames;
        size_t bytes =
            size_t(port.frames) * port.channels * (port.is_float ? 4 : 2);
        const auto *data =
            buffer ? static_cast<const uint8_t *>(pointer(buffer, bytes, false))
                   : nullptr;
        if (buffer && !data)
          return 0x80260002;
        sources[i] = {port, data};
      }
    }
  }
  // Host callbacks may take time or query another service: no state_lock here.
  if (opened) {
    if (start && hooks.audio_start)
      hooks.audio_start(hooks.context);
    return opened;
  }
  if (named(name, "sceAudioOutClose")) {
    if (stop && hooks.audio_stop)
      hooks.audio_stop(hooks.context);
    return 0;
  }
  if (!hooks.audio_submit) {
    pace_audio(frames);
    return 0;
  }
  // Double accumulation keeps even adversarial finite float inputs from
  // overflowing before the final clip. Guest buffers need not be aligned.
  std::vector<double> mix(size_t(frames) * 2, 0);
  for (uint32_t i = 0; i < count; ++i) {
    const auto &source = sources[i];
    if (!source.bytes)
      continue;
    auto sample = [&](size_t at) -> double {
      if (source.port.is_float) {
        float value;
        std::memcpy(&value, source.bytes + at * 4, 4);
        return std::isfinite(value) ? value : 0;
      }
      int16_t value;
      std::memcpy(&value, source.bytes + at * 2, 2);
      return double(value) / 32768;
    };
    for (uint32_t f = 0; f < frames; ++f) {
      size_t at = size_t(f) * source.port.channels;
      double left = sample(at);
      double right = source.port.channels >= 2 ? sample(at + 1) : left;
      if (source.port.channels == 8) {
        // Same policy as the Mac sink: FL FR C LFE SL SR BL BR; omit LFE.
        left +=
            .707 * sample(at + 2) + .707 * sample(at + 4) + .5 * sample(at + 6);
        right +=
            .707 * sample(at + 2) + .707 * sample(at + 5) + .5 * sample(at + 7);
      }
      mix[size_t(f) * 2] += left;
      mix[size_t(f) * 2 + 1] += right;
    }
  }
  std::vector<float> stereo(mix.size());
  for (size_t i = 0; i < mix.size(); ++i)
    stereo[i] = float(std::clamp(mix[i], -1.0, 1.0));
  size_t accepted =
      hooks.audio_submit(hooks.context, stereo.data(), frames, 100);
  accepted = std::min<size_t>(accepted, frames);
  if (accepted < frames) {
    // A paused sink can return immediately. Drop stale sound and pace the
    // remainder once instead of retrying in a tight loop.
    next_audio = Clock::now();
    pace_audio(frames - uint32_t(accepted));
  } else {
    next_audio = {};
  }
  return 0;
}

} // namespace

void dcp_offline_init(DcpOfflineHooks callbacks) {
  std::vector<dcp::hle::InputEvent> parsed, scene;
  int bad_line = 0;
  if (!callbacks.input_script.empty() &&
      !dcp::hle::parse_input_script(callbacks.input_script, &parsed, &bad_line))
    throw std::runtime_error("Invalid input script line " + std::to_string(bad_line));
  if (!callbacks.scene_input_script.empty() &&
      !dcp::hle::parse_input_script(callbacks.scene_input_script, &scene, &bad_line))
    throw std::runtime_error("Invalid scene input script line " + std::to_string(bad_line));
  std::lock_guard<std::mutex> pace(pace_lock);
  if (hooks.audio_stop && std::any_of(ports.begin(), ports.end(),
                                      [](const Port &p) { return p.used; }))
    hooks.audio_stop(hooks.context);
  std::lock_guard<std::mutex> operation(save_lock);
  std::lock_guard<std::mutex> lock(state_lock);
  hooks = std::move(callbacks);
  input_events = std::move(parsed);
  scene_events = std::move(scene);
  if (hooks.save_root.empty())
    throw std::runtime_error("offline adapter requires a disposable save root");
  std::filesystem::create_directories(hooks.save_root);
  mounted.clear();
  mount_writable = false;
  mount_transition = false;
  mount_blocks = default_blocks;
  mount_metadata_failures = 0;
  pads = {};
  ports = {};
  audio_initialized = false;
  next_audio = {};
}
bool dcp_offline_savedata_access(bool writing) {
  std::lock_guard<std::mutex> lock(state_lock);
  return !mount_transition && !mounted.empty() && (!writing || mount_writable);
}
std::string dcp_offline_savedata_directory() {
  std::lock_guard<std::mutex> lock(state_lock);
  return mount_transition ? std::string{} : mounted;
}
bool dcp_offline_service(const char *name, const uint64_t a[6],
                         uint64_t *result) {
  if (!name || !a || !result)
    return false;
  if (named(name, "__stack_chk_fail"))
    throw std::runtime_error("guest stack protector check failed");
  if (named(name, "0fPn7oELg8E")) {
    // The reference Toolkit2 clear-presence adapter ignores both request/future
    // pointers. This offline runtime never publishes presence, so clearing the
    // already absent value is idempotent and performs no network or guest I/O.
    *result = 0;
    return true;
  }
  if (named(name, "sceRtcGetCurrentClockLocalTime")) {
    std::array<uint16_t, 8> date{};
    void *out = a[0] ? pointer(a[0], sizeof(date), true) : nullptr;
    const auto status = dcp::hle::rtc_local_clock(out ? date.data() : nullptr);
    if (!status)
      std::memcpy(out, date.data(), sizeof(date));
    *result = result32(status);
    return true;
  }
  if (named(name, "sceRtcGetWin32FileTime")) {
    std::array<uint16_t, 8> date{};
    const void *src = a[0] ? pointer(a[0], sizeof(date), false) : nullptr;
    void *out = a[1] ? pointer(a[1], sizeof(uint64_t), true) : nullptr;
    uint64_t fileTime = 0;
    if (src)
      std::memcpy(date.data(), src, sizeof(date));
    const auto status = dcp::hle::rtc_file_time(src ? date.data() : nullptr,
                                                out ? &fileTime : nullptr);
    if (!status)
      std::memcpy(out, &fileTime, sizeof(fileTime));
    *result = result32(status);
    return true;
  }
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
