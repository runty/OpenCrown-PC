#include "vblank-clock.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>
int main() {
  unsigned checks = 0;
  auto require = [&](bool okay) {
    ++checks;
    if (!okay) throw std::runtime_error("vblank deadline mismatch");
  };
  using dcp::game::VblankClock;
  constexpr auto p = VblankClock::period;
  VblankClock clock;
  uint64_t now = 123456789;
  require(clock.reserve(now) == now + p);
  for (unsigned frame = 2; frame <= 1000; ++frame)
    require(clock.reserve(now + (frame-1)*p + 2000000) == now + frame*p);
  // Moderate overruns retain the absolute schedule; long stalls reset once.
  auto previous = clock.next;
  require(clock.reserve(previous + 2*p) == previous);
  previous = clock.next;
  require(clock.reserve(previous + 4*p) == previous);
  previous = clock.next;
  require(clock.reserve(previous + 4*p + 1) == previous + 5*p + 1);
  // Pausing freezes the supplied virtual time, so it does not cause a reset.
  VblankClock paused;
  const auto first = paused.reserve(now);
  require(paused.reserve(first) == first+p);
  VblankClock fresh;
  require(fresh.reserve(now) == first);
  using Clock = std::chrono::steady_clock;
  auto start=Clock::now();
  VblankClock real;
  for(unsigned i=0;i<60;++i){
    auto elapsed=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
    const auto deadline=real.reserve(elapsed);
    std::this_thread::sleep_until(start+std::chrono::nanoseconds(deadline));
    if(i!=59) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const auto seconds=std::chrono::duration<double>(Clock::now()-start).count();
  require(seconds >= 0.99 && seconds < 1.20);
  std::printf("{\"checks\":%u,\"failed\":0,\"frames\":60,\"work_ms\":5,\"elapsed_seconds\":%.6f}\n",checks,seconds);
}
