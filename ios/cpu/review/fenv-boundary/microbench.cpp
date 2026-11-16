#include "boundary.h"
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <stdexcept>
static uint64_t clock_ns() {
  timespec t{};
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t))
    throw std::runtime_error("thread CPU clock unavailable");
  return uint64_t(t.tv_sec) * 1000000000 + uint64_t(t.tv_nsec);
}
int main() {
  constexpr uint64_t iterations = 1000000;
  for (int round = 0; round < 5; ++round) {
    uint64_t t = clock_ns();
    for (uint64_t i = 0; i < iterations; ++i)
      asm volatile("" ::: "memory");
    uint64_t control = clock_ns() - t;
    t = clock_ns();
    for (uint64_t i = 0; i < iterations; ++i) {
      ExperimentalNativeBoundary boundary;
      asm volatile("" ::: "memory");
    }
    uint64_t experiment = clock_ns() - t;
    std::printf("round=%d count=%llu control_ns=%llu boundary_ns=%llu\n", round,
                (unsigned long long)iterations, (unsigned long long)control,
                (unsigned long long)experiment);
  }
}
