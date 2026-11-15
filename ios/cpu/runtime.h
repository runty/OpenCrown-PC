#pragma once
#include "remill/Arch/X86/Runtime/State.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

// A guest LOCK instruction excludes every checked data access, including
// reads by other host threads. Multiple guest contexts may nest on one thread.
class GuestAtomicMutex {
public:
  void lock();
  void unlock() noexcept;
};

struct GuestScalarThread;
struct Memory {
  static constexpr uint64_t base = 0x100000;
  std::vector<uint8_t> bytes = std::vector<uint8_t>(65536);
  struct Region {
    uint8_t *data;
    size_t size;
  };
  std::shared_ptr<std::vector<Region>> regions =
      std::make_shared<std::vector<Region>>();
  const std::atomic<bool> *stopping = nullptr;
  bool (*cancelled)() = nullptr;
  // Optional complete set of signals for the bound runtime poll callback.
  // Generic callers leave these empty and their callback runs on every poll.
  struct PollSignals {
    const std::atomic<bool> *paused = nullptr, *serviceStop = nullptr,
                            *workerCancel = nullptr;
  } poll;
  bool stopOnFault = false;
  std::unique_lock<GuestAtomicMutex> atomicLock;
  std::string fault;
  unsigned calls = 0;
  unsigned returns = 0;
  // Only lifted scalar bridges use this owner-thread binding. Host hooks
  // sharing this Memory continue to use the calling thread's checked cache.
  GuestScalarThread *scalarThread = nullptr;
  struct SignalCache {
    const std::atomic<uint64_t> &generation;
    std::atomic<uint64_t> lastClear{0};
  };
  SignalCache *signalCache = nullptr;
};
using Lifted = Memory *(*)(State &, uint64_t, Memory *);
Memory *dispatch(State &, uint64_t, Memory *);
void bindScalarThread(Memory &);
void addRegion(Memory &, uint8_t *, size_t);
uint8_t *guestPointer(Memory &, uint64_t, size_t);
void guestRead(Memory &, uint64_t, void *, size_t);
// Compare a guest span with a private, immutable host snapshot.
bool guestMatches(Memory &, uint64_t, const void *, size_t);
void guestWrite(Memory &, uint64_t, const void *, size_t);
void guestMove(Memory &, uint64_t destination, uint64_t source, size_t);
void guestFill(Memory &, uint64_t destination, uint8_t value, size_t);
uint64_t read64(Memory &, uint64_t);
void write64(Memory &, uint64_t, uint64_t);
extern "C" const char *dcp_cpu_report();

extern "C" int32_t __remill_fpu_get_rounding();
extern "C" void __remill_fpu_set_rounding(int32_t mode);
extern "C" Memory *__remill_read_memory_f80(Memory *, uint64_t, float80_t &);
extern "C" Memory *__remill_write_memory_f80(Memory *, uint64_t,
                                             const float80_t &);
