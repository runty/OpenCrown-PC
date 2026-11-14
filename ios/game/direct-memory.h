#pragma once
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

namespace dcp::game {
// Direct-backed resource arenas use unsigned 32-bit offsets between their
// allocations. Keep later mappings above earlier ones in one <=4 GiB span.
// The reservation is inaccessible until an individual allocation is committed;
// callers register only those allocations with the checked guest-memory layer.
class DirectMemoryArena {
public:
  struct Stats {
    uint64_t base, capacity, used, allocations;
  };
  explicit DirectMemoryArena(uint64_t preferred = uint64_t(4) << 30,
                             uint64_t minimum = uint64_t(3) << 30)
      : preferred_(preferred), minimum_(minimum) {
    const uint64_t page = getpagesize();
    if (!minimum || minimum > preferred || preferred > (uint64_t(1) << 32) ||
        preferred % page || minimum % page)
      throw std::invalid_argument("invalid DirectMemory reservation size");
  }
  DirectMemoryArena(const DirectMemoryArena &) = delete;
  DirectMemoryArena &operator=(const DirectMemoryArena &) = delete;
  ~DirectMemoryArena() {
    if (base_)
      munmap(reinterpret_cast<void *>(base_), capacity_);
  }
  uint64_t allocate(uint64_t bytes, uint64_t alignment) {
    const uint64_t page = getpagesize();
    alignment = std::max(alignment, page);
    if (!bytes || (alignment & (alignment - 1)) ||
        bytes > UINT64_MAX - (page - 1))
      throw std::runtime_error("invalid DirectMemory allocation");
    bytes = (bytes + page - 1) & ~(page - 1);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!base_)
      reserve();
    const uint64_t cursor = base_ + used_;
    if (cursor > UINT64_MAX - (alignment - 1))
      throw std::runtime_error("DirectMemory alignment overflow");
    const uint64_t address = (cursor + alignment - 1) & ~(alignment - 1);
    if (address < base_ || address - base_ > capacity_ ||
        bytes > capacity_ - (address - base_))
      throw std::runtime_error("ordered DirectMemory reservation exhausted");
    if (mprotect(reinterpret_cast<void *>(address), bytes,
                 PROT_READ | PROT_WRITE))
      throw std::runtime_error("cannot commit DirectMemory allocation");
    used_ = address - base_ + bytes;
    ++allocations_;
    return address;
  }
  Stats stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {base_, capacity_, used_, allocations_};
  }

private:
  void reserve() {
    // No fixed mappings and no unbounded address-hint probing. Some mobile
    // address spaces may accept 3 GiB rather than the preferred 4 GiB. Both
    // retain the offset representation; exhaustion is an explicit error.
    for (uint64_t bytes = preferred_;; bytes = std::max(minimum_, bytes / 2)) {
      void *memory = mmap(nullptr, bytes, PROT_NONE,
                          MAP_PRIVATE | MAP_ANON, -1, 0);
      if (memory != MAP_FAILED) {
        const auto address = reinterpret_cast<uint64_t>(memory);
        if (address && address < (uint64_t(1) << 40) &&
            bytes <= (uint64_t(1) << 40) - address) {
          base_ = address;
          capacity_ = bytes;
          return;
        }
        munmap(memory, bytes);
      }
      if (bytes == minimum_)
        break;
    }
    throw std::runtime_error("cannot reserve ordered DirectMemory address space");
  }
  const uint64_t preferred_, minimum_;
  uint64_t base_ = 0, capacity_ = 0, used_ = 0, allocations_ = 0;
  mutable std::mutex mutex_;
};
} // namespace dcp::game
