#pragma once
// Private experiment: a counter slot belongs to one worker for one session.
// A sampled total is not an atomic snapshot across all workers.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
class TraceCounterStudy {
  struct alignas(128) Slot { std::atomic<uint64_t> value{0}; };
  static constexpr size_t count=128;
  std::array<Slot,count> slots_{};
  std::atomic<size_t> next_{0};
  alignas(128) std::atomic<uint64_t> overflow_{0};
public:
  using Token=std::atomic<uint64_t>*;
  static_assert(std::atomic<uint64_t>::is_always_lock_free);
  Token acquire() noexcept {
    // Saturate rather than wrapping and assigning an in-use slot twice.
    size_t next=next_.load(std::memory_order_relaxed);
    while(next<count && !next_.compare_exchange_weak(next,next+1,std::memory_order_relaxed)){}
    return next<count ? &slots_[next].value : &overflow_;
  }
  static void increment(Token token) noexcept {token->fetch_add(1,std::memory_order_relaxed);}
  uint64_t total() const noexcept {
    uint64_t result=overflow_.load(std::memory_order_relaxed);
    for(const auto &slot:slots_) result+=slot.value.load(std::memory_order_relaxed);
    return result;
  }
};
