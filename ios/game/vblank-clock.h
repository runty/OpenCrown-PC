#pragma once
#include <cstdint>

namespace dcp::game {
// Time is the runtime's pause-excluded monotonic clock. Caller serializes use.
// Match the desktop's absolute 60 Hz deadlines and four-period stall reset.
struct VblankClock {
  static constexpr uint64_t period = 1000000000 / 60;
  uint64_t next = 0;
  uint64_t reserve(uint64_t now) {
    if (!next || (now > next && now - next > 4 * period))
      next = now + period;
    const auto deadline = next;
    next += period;
    return deadline;
  }
};
} // namespace dcp::game
