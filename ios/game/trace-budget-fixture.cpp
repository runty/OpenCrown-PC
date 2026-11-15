#include "trace-budget.h"
#include <cassert>
#include <thread>

int main() {
  dcp::game::TraceBudget budget;
  auto &idle = budget.addWorker();
  auto &busy = budget.addWorker();
  assert(budget.steps() == 0);
  constexpr unsigned limit = 513;
  assert(budget.enter(idle, limit));
  for (unsigned i = 1; i < limit; ++i)
    assert(budget.enter(busy, limit));
  assert(budget.steps() == limit);
  assert(!budget.enter(idle, limit));
  assert(budget.steps() == limit + 1);

  dcp::game::TraceBudget concurrent;
  constexpr unsigned concurrentLimit = 100003;
  std::atomic<unsigned> accepted{0};
  std::vector<std::thread> threads;
  for (unsigned i = 0; i < 8; ++i) {
    threads.emplace_back([&] {
      auto &worker = concurrent.addWorker();
      while (concurrent.enter(worker, concurrentLimit))
        ++accepted;
    });
  }
  for (auto &thread : threads)
    thread.join();
  assert(accepted == concurrentLimit);
  assert(concurrent.steps() == concurrentLimit + threads.size());
}
