// Synthetic host callbacks only; no game data or UIKit/device dependency.
#include "offline-services.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>
namespace {
struct Context {
  std::array<uint8_t, 1 << 20> bytes{};
  DcpPlatformPad pad{};
  std::vector<float> audio;
  size_t accepted = SIZE_MAX;
  unsigned calls = 0, starts = 0, stops = 0, begins = 0, commits = 0;
  unsigned input_reads = 0, connection_queries = 0;
  bool transaction_okay = true, expect_metadata = false;
  std::string save_root;
  std::mutex transition_mutex;
  std::condition_variable transition_cv;
  bool hold_callback = false, callback_waiting = false,
       release_callback = false;
};
void *pointer(void *opaque, uint64_t address, size_t size, bool) {
  auto &c = *static_cast<Context *>(opaque);
  return address <= c.bytes.size() && size <= c.bytes.size() - size_t(address)
             ? c.bytes.data() + address
             : nullptr;
}
void input(void *opaque, unsigned slot, DcpPlatformPad *pad) {
  assert(slot == 0);
  auto &c = *static_cast<Context *>(opaque);
  ++c.input_reads;
  *pad = c.pad;
  // Read-only state queries demonstrate the shared state mutex is not held.
  (void)dcp_offline_savedata_access(false);
}
int connected(void *opaque, unsigned slot) {
  assert(slot == 0);
  auto &c = *static_cast<Context *>(opaque);
  ++c.connection_queries;
  (void)dcp_offline_savedata_access(false);
  return c.pad.connected;
}
int start(void *opaque) {
  ++static_cast<Context *>(opaque)->starts;
  return 0;
}
void stop(void *opaque) { ++static_cast<Context *>(opaque)->stops; }
size_t submit(void *opaque, const float *data, size_t frames,
              uint32_t timeout) {
  auto &c = *static_cast<Context *>(opaque);
  assert(timeout == 100 && frames <= 4096);
  ++c.calls;
  c.audio.assign(data, data + frames * 2);
  (void)dcp_offline_savedata_access(false);
  return std::min(frames, c.accepted);
}
bool transaction(void *opaque, bool begin) {
  auto &c = *static_cast<Context *>(opaque);
  if (begin)
    ++c.begins;
  else {
    ++c.commits;
    if (c.expect_metadata) {
      std::ifstream file(std::filesystem::path(c.save_root) /
                             "SYNTHETIC/sce_sys/param.bin",
                         std::ios::binary);
      uint32_t value = 0;
      file.read(reinterpret_cast<char *>(&value), 4);
      assert(file && value == 0x12345678);
    }
  }
  {
    std::unique_lock<std::mutex> lock(c.transition_mutex);
    if (c.hold_callback) {
      c.callback_waiting = true;
      c.transition_cv.notify_all();
      c.transition_cv.wait(lock, [&] { return c.release_callback; });
    }
  }
  return c.transaction_okay;
}
uint64_t call(const char *name, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0,
              uint64_t d = 0, uint64_t e = 0, uint64_t f = 0) {
  uint64_t arguments[6] = {a, b, c, d, e, f}, result = 0;
  assert(dcp_offline_service(name, arguments, &result));
  return result;
}
uint64_t error(uint32_t value) { return uint64_t(int64_t(int32_t(value))); }
template <class T> void put(Context &c, size_t at, T value) {
  std::memcpy(c.bytes.data() + at, &value, sizeof(value));
}
template <class T> T get(Context &c, size_t at) {
  T v;
  std::memcpy(&v, c.bytes.data() + at, sizeof(v));
  return v;
}
void near(float a, float b) { assert(std::fabs(a - b) < .00001f); }
} // namespace
int main(int argc, char **argv) {
  assert(argc == 2); // Caller chooses a disposable external save directory.
  Context c;
  DcpOfflineHooks hooks;
  c.save_root = argv[1];
  hooks.context = &c;
  hooks.pointer = pointer;
  hooks.save_root = argv[1];
  hooks.input_read = input;
  hooks.input_connected = connected;
  hooks.audio_start = start;
  hooks.audio_stop = stop;
  hooks.audio_submit = submit;
  hooks.savedata_transaction = transaction;
  dcp_offline_init(hooks);
  // Unaligned guest buffers and truncated destinations must be handled without
  // touching neighboring data. The FILETIME epoch is 1601-01-01.
  constexpr size_t date = 203, stamp = 701;
  c.bytes[date - 1] = c.bytes[date + 16] = 0xab;
  assert(call("sceRtcGetCurrentClockLocalTime", date) == 0);
  assert(get<uint16_t>(c, date) >= 1970 && get<uint16_t>(c, date + 2) >= 1 &&
         get<uint16_t>(c, date + 2) <= 12 && get<uint32_t>(c, date + 12) < 1000000);
  assert(c.bytes[date - 1] == 0xab && c.bytes[date + 16] == 0xab);
  assert(call("sceRtcGetCurrentClockLocalTime", 0) == error(0x80b50002));
  assert(call("sceRtcGetCurrentClockLocalTime", c.bytes.size() - 15) == error(0x80b50002));
  std::memset(c.bytes.data() + date, 0, 16);
  put<uint16_t>(c, date, 1970);
  put<uint16_t>(c, date + 2, 1);
  put<uint16_t>(c, date + 4, 1);
  put<uint32_t>(c, date + 12, 42);
  assert(call("sceRtcGetWin32FileTime", date, stamp) == 0);
  assert(get<uint64_t>(c, stamp) == UINT64_C(116444736000000420));
  assert(call("sceRtcGetWin32FileTime", date, c.bytes.size() - 7) == error(0x80b50002));
  put<uint16_t>(c, date + 2, 13);
  assert(call("sceRtcGetWin32FileTime", date, stamp) == error(0x80b50002));
  assert(get<uint64_t>(c, stamp) == UINT64_C(116444736000000420));
  assert(call("scePadOpen", 1000) == 1);
  // Even before the first ReadState, information uses a non-consuming peek.
  assert(call("scePadGetControllerInformation", 1, 128) == 0 &&
         !c.bytes[128 + 0xc] && c.input_reads == 0 && c.connection_queries == 1);
  c.pad.connected = 1;
  c.pad.buttons = 0x4008;
  c.pad.lx = 5;
  c.pad.ly = 200;
  c.pad.rx = 100;
  c.pad.ry = 255;
  c.pad.l2 = 77;
  c.pad.r2 = 99;
  c.pad.touch_count = 255;
  c.pad.tx[0] = 65000;
  c.pad.ty[0] = 65000;
  c.pad.tid[0] = 255;
  c.pad.tx[1] = 123;
  c.pad.ty[1] = 456;
  c.pad.tid[1] = 7;
  assert(call("scePadReadState", 1, 128) == 0);
  assert(get<uint32_t>(c, 128) == 0x4008 && c.bytes[132] == 5 &&
         c.bytes[137] == 99);
  near(get<float>(c, 128 + 0x18), 1);
  assert(c.bytes[128 + 0x34] == 2 && get<uint16_t>(c, 128 + 0x3c) == 1919);
  assert(get<uint16_t>(c, 128 + 0x3e) == 942 && c.bytes[128 + 0x40] == 127);
  assert(get<uint16_t>(c, 128 + 0x44) == 123 &&
         get<uint16_t>(c, 128 + 0x46) == 456 && c.bytes[128 + 0x48] == 7);
  assert(c.bytes[128 + 0x4c] == 1 && c.bytes[128 + 0x68] == 1 &&
         get<uint64_t>(c, 128 + 0x50) > 0);
  const unsigned reads = c.input_reads;
  assert(call("scePadGetControllerInformation", 1, 128) == 0 &&
         c.bytes[128 + 0xc] && c.input_reads == reads);
  assert(call("scePadReadState", 1, c.bytes.size() - 100) == error(0x80920003) &&
         c.input_reads == reads);
  const unsigned queries = c.connection_queries;
  assert(call("scePadGetControllerInformation", 1, c.bytes.size() - 27) ==
             error(0x80920003) && c.connection_queries == queries);
  c.pad.connected = 0;
  assert(call("scePadGetControllerInformation", 1, 128) == 0 &&
         !c.bytes[128 + 0xc] && c.input_reads == reads);
  assert(call("scePadReadState", 1, 128) == 0 && get<uint32_t>(c, 128) == 0 &&
         c.bytes[132] == 128 && !c.bytes[128 + 0x4c]);
  assert(call("scePadGetControllerInformation", 1, 128) == 0 &&
         !c.bytes[128 + 0xc]);
  // A host without the peek hook uses the last valid read, not another read.
  hooks.input_connected = nullptr;
  dcp_offline_init(hooks);
  assert(call("scePadOpen", 1000) == 1);
  assert(call("scePadReadState", 1, 128) == 0);
  const unsigned fallback_reads = c.input_reads;
  c.pad.connected = 1;
  assert(call("scePadGetControllerInformation", 1, 128) == 0 &&
         !c.bytes[128 + 0xc] && c.input_reads == fallback_reads);
  assert(call("scePadReadState", 1, 128) == 0);
  assert(call("scePadGetControllerInformation", 1, 128) == 0 &&
         c.bytes[128 + 0xc] && c.input_reads == fallback_reads + 1);
  assert(call("sceAudioOutInit") == 0);
  assert(call("sceAudioOutOpen", 255, 0, 0, 4097, 48000, 4) ==
         error(0x80260002));
  assert(call("sceAudioOutOpen", 255, 0, 0, UINT64_C(0x100000100), 48000, 4) ==
         error(0x80260002));
  const unsigned channels[] = {1, 2, 8, 1, 2, 8, 8, 8};
  for (unsigned format = 0; format < 8; ++format) {
    unsigned ch = channels[format];
    bool fp = format >= 3 && format != 6;
    assert(call("sceAudioOutOpen", 255, 0, 0, 2, 48000, format) == 1);
    // Intentionally unaligned guest input; LFE is large and must be omitted.
    for (unsigned f = 0; f < 2; ++f)
      for (unsigned i = 0; i < ch; ++i) {
        float value = i == 3 ? .9f : .03125f * float(i + 1);
        if (fp)
          put(c, 2049 + (f * ch + i) * 4, value);
        else
          put(c, 2049 + (f * ch + i) * 2, int16_t(value * 32768));
      }
    assert(call("sceAudioOutOutput", 1, 2049) == 0 && c.audio.size() == 4);
    float left = .03125f, right = ch == 1 ? left : .0625f;
    if (ch == 8) {
      left += .707f * .09375f + .707f * .15625f + .5f * .21875f;
      right += .707f * .09375f + .707f * .1875f + .5f * .25f;
    }
    near(c.audio[0], left);
    near(c.audio[1], right);
    assert(call("sceAudioOutClose", 1) == 0);
  }
  assert(c.starts == 8 && c.stops == 8);
  assert(call("sceAudioOutOpen", 255, 0, 0, 256, 48000, 4) == 1);
  assert(call("sceAudioOutOpen", 255, 0, 0, 256, 48000, 3) == 2 &&
         c.starts == 9);
  for (unsigned f = 0; f < 256; ++f) {
    put(c, 4096 + f * 8, .75f);
    put(c, 4100 + f * 8, -.75f);
    put(c, 8192 + f * 4, .5f);
  }
  put(c, 32, int32_t(1));
  put(c, 40, uint64_t(4096));
  put(c, 48, int32_t(2));
  put(c, 56, uint64_t(8192));
  assert(call("sceAudioOutOutputs", 32, 2) == 0);
  near(c.audio[0], 1);
  near(c.audio[1], -.25f);
  put(c, 4096, std::numeric_limits<float>::quiet_NaN());
  put(c, 4100, std::numeric_limits<float>::infinity());
  assert(call("sceAudioOutOutput", 1, 4096) == 0);
  near(c.audio[0], 0);
  near(c.audio[1], 0);
  put(c, 4096, std::numeric_limits<float>::max());
  put(c, 4100, -std::numeric_limits<float>::max());
  assert(call("sceAudioOutOutput", 1, 4096) == 0);
  near(c.audio[0], 1);
  near(c.audio[1], -1);
  unsigned previous = c.calls;
  assert(call("sceAudioOutOutput", 1, c.bytes.size() - 10) ==
             error(0x80260002) &&
         c.calls == previous);
  assert(call("sceAudioOutOutputs", c.bytes.size() - 10, 2) ==
         error(0x80260002));
  assert(call("sceAudioOutOutputs", 32, UINT64_C(0x100000002)) ==
         error(0x80260002));
  put(c, 48, int32_t(1));
  assert(call("sceAudioOutOutputs", 32, 2) == error(0x80260002));
  put(c, 48, int32_t(2));
  assert(call("sceAudioOutOpen", 255, 0, 0, 128, 48000, 4) == 3);
  put(c, 48, int32_t(3));
  assert(call("sceAudioOutOutputs", 32, 2) == error(0x80260002));
  c.accepted = 0;
  auto begin = std::chrono::steady_clock::now();
  previous = c.calls;
  assert(call("sceAudioOutOutput", 1, 4096) == 0 && c.calls == previous + 1);
  assert(std::chrono::steady_clock::now() - begin >=
         std::chrono::milliseconds(4));
  c.accepted = 128;
  begin = std::chrono::steady_clock::now();
  assert(call("sceAudioOutOutput", 1, 4096) == 0);
  assert(std::chrono::steady_clock::now() - begin >=
         std::chrono::milliseconds(2));
  assert(call("sceAudioOutClose", 1) == 0 && c.stops == 8);
  assert(call("sceAudioOutClose", 2) == 0 && call("sceAudioOutClose", 3) == 0 &&
         c.stops == 9);
  // Save callback failure gates admission and retains a mount for commit retry.
  std::strcpy(reinterpret_cast<char *>(c.bytes.data() + 16000), "SYNTHETIC");
  std::strcpy(reinterpret_cast<char *>(c.bytes.data() + 16032), "/savedata0");
  put(c, 12000 + 8, uint64_t(16000));
  put(c, 12000 + 16, uint64_t(0x8000));
  put(c, 12000 + 24, uint32_t(6));
  auto blocked_transition = [&](bool mount, bool okay) {
    c.transaction_okay = okay;
    {
      std::lock_guard<std::mutex> lock(c.transition_mutex);
      c.hold_callback = true;
      c.callback_waiting = false;
      c.release_callback = false;
    }
    uint64_t returned = 0;
    std::thread worker([&] {
      returned = mount ? call("sceSaveDataMount2", 12000, 12100)
                       : call("sceSaveDataUmount", 16032);
    });
    {
      std::unique_lock<std::mutex> lock(c.transition_mutex);
      assert(c.transition_cv.wait_for(lock, std::chrono::seconds(5),
                                      [&] { return c.callback_waiting; }));
    }
    // A file thread can query state while the host snapshot/checkpoint is
    // blocked: neither the old mount nor its directory may be published.
    assert(!dcp_offline_savedata_access(false));
    assert(!dcp_offline_savedata_access(true));
    assert(dcp_offline_savedata_directory().empty());
    {
      std::lock_guard<std::mutex> lock(c.transition_mutex);
      c.release_callback = true;
      c.transition_cv.notify_all();
    }
    worker.join();
    c.hold_callback = false;
    assert(returned == (okay ? 0 : error(0x809f000b)));
    assert(dcp_offline_savedata_access(true) == (mount ? okay : !okay));
  };
  blocked_transition(true, false);
  assert(
      c.begins == 1 && !dcp_offline_savedata_access(true) &&
      !std::filesystem::exists(std::filesystem::path(argv[1]) / "SYNTHETIC"));
  blocked_transition(true, true);
  assert(c.begins == 2 && dcp_offline_savedata_access(true));
  put(c, 17000, uint32_t(0x12345678));
  auto metadata =
      std::filesystem::path(argv[1]) / "SYNTHETIC/sce_sys/param.bin";
  std::filesystem::create_directory(metadata);
  assert(call("sceSaveDataSetParam", 16032, 0, 17000, 4) == error(0x809f000b));
  assert(call("sceSaveDataUmount", 16032) == error(0x809f000b) &&
         c.commits == 0);
  std::filesystem::remove(metadata);
  assert(call("sceSaveDataSetParam", 16032, 0, 17000, 4) == 0);
  c.expect_metadata = true;
  blocked_transition(false, false);
  assert(dcp_offline_savedata_access(true) && c.commits == 1);
  blocked_transition(false, true);
  assert(!dcp_offline_savedata_access(false) && c.commits == 2);
  put(c, 12000 + 24, uint32_t(1));
  assert(call("sceSaveDataMount2", 12000, 12100) == 0 && c.begins == 2);
  assert(call("sceSaveDataUmount", 16032) == 0 && c.commits == 2);
  std::cout << "PASS: guest pad layout/touch/disconnect/non-consuming metadata, all audio "
               "formats/downmix/batches/bounds/short writes, save transition "
               "isolation/failure/retry/readonly\n";
}
