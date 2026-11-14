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
  bool stopOnFault = false;
  std::unique_lock<std::recursive_mutex> atomicLock;
  std::string fault;
  unsigned calls = 0;
  unsigned returns = 0;
};
using Lifted = Memory *(*)(State &, uint64_t, Memory *);
Memory *dispatch(State &, uint64_t, Memory *);
void addRegion(Memory &, uint8_t *, size_t);
uint8_t *guestPointer(Memory &, uint64_t, size_t);
void guestRead(Memory &, uint64_t, void *, size_t);
void guestWrite(Memory &, uint64_t, const void *, size_t);
uint64_t read64(Memory &, uint64_t);
void write64(Memory &, uint64_t, uint64_t);
extern "C" const char *dcp_cpu_report();
