#pragma once
#include <cstdint>
#include <cstring>
#include <mutex>

namespace dcp::game {
// Read-only snapshots of the original game's cursor. Integer validation avoids
// changing the translated thread's floating-point environment on bad data.
class CursorReadback {
public:
  struct Sample {
    uint32_t sequence = 0;
    float x = 0, y = 0;
  };
  bool publish(uint64_t xy) {
    const auto x = uint32_t(xy), y = uint32_t(xy >> 32);
    if ((x & 0x7fffffffu) > 0x44700000u || // 960
        (y & 0x7fffffffu) > 0x44070000u)   // 540
      return false;
    std::lock_guard<std::mutex> lock(mutex_);
    xy_ = xy;
    if (!++sequence_)
      ++sequence_;
    return true;
  }
  Sample read() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Sample result;
    result.sequence = sequence_;
    const auto x = uint32_t(xy_), y = uint32_t(xy_ >> 32);
    std::memcpy(&result.x, &x, sizeof x);
    std::memcpy(&result.y, &y, sizeof y);
    return result;
  }
private:
  mutable std::mutex mutex_;
  uint64_t xy_ = 0;
  uint32_t sequence_ = 0;
};
} // namespace dcp::game
