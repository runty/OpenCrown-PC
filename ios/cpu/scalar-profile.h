#pragma once
// Compile-time diagnostic only. Its counts and sampled acquisition latency are
// instrumented observations, never frame-rate or CPU-headroom qualification.
#if DCP_SCALAR_PROFILE
#include <cstddef>
#include <cstdint>
#include <string>
namespace dcp::cpu::profile {
struct Thread;
struct Record;
struct Sample {
  Thread *thread = nullptr;
  Record *record = nullptr;
  uint64_t before = 0, page = 0, elapsed = 0, instruction_pc = 0;
  bool captured = false;
  unsigned stripe = 64;
  Sample() = default;
  Sample(Thread *owner, Record *value, uint64_t start, uint64_t address_page,
         unsigned band, uint64_t ip)
      : thread(owner), record(value), before(start), page(address_page),
        instruction_pc(ip), stripe(band) {}
  Sample(const Sample &) = delete;
  Sample &operator=(const Sample &) = delete;
  void acquired();
  ~Sample();
};
// Bind the live guest RIP for sampled sites. Nested native guest callbacks must
// restore their parent's state pointer before the nested State leaves scope.
struct StateScope {
  Thread *owner;
  const uint64_t *previous;
  Record *previous_record;
  explicit StateScope(const uint64_t *rip);
  ~StateScope();
  StateScope(const StateScope &) = delete;
  StateScope &operator=(const StateScope &) = delete;
};
// Reset/report require quiescence; a failed join never authorizes counter
// reads.
void reset();
Thread *thread();
void trace(uint64_t pc);
void start_town();
Sample scalar(Thread *, bool lifted, bool writing, size_t bytes,
              const void *pointer, unsigned stripe, bool atomic_held);
std::string report(bool workers_joined);
} // namespace dcp::cpu::profile
#endif
