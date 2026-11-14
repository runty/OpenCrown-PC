#pragma once
#include "platform.h"
#include <array>
#include <atomic>
#include <mutex>

namespace dcp::mobile {
constexpr size_t audio_capacity = 32768;
static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "Audio indices require lock-free atomics");
/* Single consumer; callers serialize producers. Reset only with both quiescent.
 */
class AudioRing {
public:
  size_t push(const float *stereo, size_t frames, uint64_t &clipped);
  size_t pop(float *left, float *right, size_t frames);
  size_t queued() const;
  size_t clear();

private:
  std::array<float, audio_capacity * 2> samples_{};
  alignas(64) std::atomic<uint64_t> read_{0};
  alignas(64) std::atomic<uint64_t> write_{0};
};

DcpPlatformPad neutral(bool connected = true);
uint8_t axis(float value, bool invert = false);
uint8_t trigger(float value);
void touch_point(DcpPlatformPad &pad, float x, float y, uint8_t id);

class InputState {
public:
  void physical(unsigned slot, const DcpPlatformPad &pad);
  void touch(const DcpPlatformPad &pad);
  void active(bool active);
  void reset();
  DcpPlatformPad read(unsigned slot);

private:
  std::mutex mutex_;
  std::array<DcpPlatformPad, 4> physical_{};
  DcpPlatformPad touch_ = neutral();
  bool active_ = true;
};
} // namespace dcp::mobile
