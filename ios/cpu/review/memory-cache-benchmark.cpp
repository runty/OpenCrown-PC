// Controlled mapped-read workload, identical for committed baseline and current
// runtime.
#include "runtime.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>
Memory *dispatch(State &, uint64_t, Memory *memory) { return memory; }
static uint64_t cpu() {
  timespec value{};
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value))
    std::abort();
  return uint64_t(value.tv_sec) * 1000000000 + value.tv_nsec;
}
int main() {
  constexpr unsigned mappings = 1536, stride = 4096;
  Memory memory;
  std::vector<uint8_t> bytes(size_t(mappings) * stride);
  std::vector<uint64_t> addresses;
  for (unsigned i = 0; i < mappings; ++i) {
    auto *pointer = bytes.data() + size_t(i) * stride + 32;
    addRegion(memory, pointer, 256);
    uint64_t value = i + 1;
    std::memcpy(pointer, &value, 8);
    addresses.push_back(reinterpret_cast<uint64_t>(pointer));
  }
  struct Phase {
    const char *name;
    unsigned iterations;
    bool cold, pointer;
  };
  Phase phases[] = {{"hot_read64", 1000000, false, false},
                    {"churn_read64", 300000, true, false},
                    {"hot_pointer", 1000000, false, true}};
  std::printf("{\"mappings\":%u,\"phases\":[", mappings);
  bool comma = false;
  for (auto &phase : phases) {
    for (unsigned i = mappings - 8; i < mappings; ++i)
      read64(memory, addresses[i]);
    uint64_t checksum = 0, expected = 0, cpuStart = cpu();
    auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < phase.iterations; ++i) {
      unsigned index =
          phase.cold ? (i * 173u) % mappings : mappings - 8 + (i % 8);
      uint64_t value;
      if (phase.pointer)
        std::memcpy(&value, guestPointer(memory, addresses[index], 8), 8);
      else
        value = read64(memory, addresses[index]);
      checksum += value;
      expected += index + 1;
    }
    auto wall = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start)
                    .count();
    uint64_t elapsed = cpu() - cpuStart;
    if (checksum != expected)
      return 1;
    std::printf("%s{\"name\":\"%s\",\"iterations\":%u,\"cpu_ns\":%llu,\"wall_"
                "ns\":%llu,\"checksum\":%llu}",
                comma ? "," : "", phase.name, phase.iterations,
                (unsigned long long)elapsed, (unsigned long long)wall,
                (unsigned long long)checksum);
    comma = true;
  }
  std::puts("]}");
}
