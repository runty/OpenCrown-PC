#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace dcp::game {
// Reserve trace entries in batches so independent guest workers do not write
// the same cache line on every translated function call. Near exhaustion, any
// worker can consume unused entries reserved by an idle or exited worker.
class TraceBudget {
public:
  struct alignas(128) Worker {
    std::atomic<unsigned> remaining{0};
  };

  Worker &addWorker() {
    std::lock_guard lock(mutex_);
    workers_.push_back(std::make_unique<Worker>());
    return *workers_.back();
  }

  bool enter(Worker &worker, uint64_t limit) {
    if (consume(worker))
      return true;
    return refill(worker, limit);
  }

  uint64_t steps() const {
    std::lock_guard lock(mutex_);
    uint64_t total = issued_ + rejected_;
    for (const auto &worker : workers_)
      total -= worker->remaining.load(std::memory_order_relaxed);
    return total;
  }

private:
  static bool consume(Worker &worker) {
    auto count = worker.remaining.load(std::memory_order_relaxed);
    while (count) {
      if (worker.remaining.compare_exchange_weak(
              count, count - 1, std::memory_order_relaxed))
        return true;
    }
    return false;
  }

  [[gnu::noinline]] bool refill(Worker &worker, uint64_t limit) {
    std::lock_guard lock(mutex_);
    if (issued_ < limit) {
      const auto grant = std::min<uint64_t>(256, limit - issued_);
      issued_ += grant;
      worker.remaining.store(static_cast<unsigned>(grant - 1), std::memory_order_relaxed);
      return true;
    }
    for (const auto &other : workers_)
      if (consume(*other))
        return true;
    ++rejected_;
    return false;
  }

  mutable std::mutex mutex_;
  std::vector<std::unique_ptr<Worker>> workers_;
  uint64_t issued_ = 0, rejected_ = 0;
};
} // namespace dcp::game
