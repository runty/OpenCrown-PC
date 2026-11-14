// Native workers and headless synchronization, following hle/kernel/thread.cpp.
#include "thread-services.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr uint64_t ok = 0;
constexpr uint64_t invalid = 0x80020016, busy = 0x80020010,
                   timedout = 0x8002003c, canceled = 0x80020055;
struct Attr {
  uint64_t stacksize = 0x100000;
  int32_t detach = 0, policy = 1, prio = 700, inherit = 4;
  uint64_t affinity = 0x7f;
};
static_assert(sizeof(Attr) == 32);
struct Worker {
  alignas(16) std::array<uint8_t, 128> identity{};
  Attr attr;
  std::thread thread;
  std::atomic<bool> cancel{false};
  std::mutex lock;
  std::condition_variable cv;
  bool done = false;
  bool finishing = false; // Protected by lock; cancellation cannot interrupt TLS cleanup.
  std::atomic<bool> detached{false};
  std::mutex join_lock;
  uint64_t result = 0;
  char name[32]{};
};
struct Cond {
  std::condition_variable_any cv;
};
struct Event {
  std::mutex lock;
  std::condition_variable cv;
  uint64_t bits = 0;
  bool deleted = false;
};
struct Key {
  bool used = false;
  uint64_t destructor = 0;
};
DcpThreadHooks hooks;
std::atomic<bool> stop{false};
std::mutex state_lock;
std::recursive_mutex once_lock;
std::map<uint64_t, std::shared_ptr<Worker>> workers;
std::map<uint64_t, std::shared_ptr<Cond>> conditions;
std::map<uint64_t, std::shared_ptr<Event>> events;
std::array<Key, 256> keys;
std::vector<std::shared_ptr<Cond>> retired_conditions;
std::vector<std::shared_ptr<Event>> retired_events;
std::string error;
uint64_t next_queue = 0x1000;
thread_local Worker *current = nullptr;
thread_local std::array<uint64_t, 256> specific{};
bool shutting_down() {
  return stop.load() || (hooks.stopping && hooks.stopping(hooks.context));
}
void notify_cancellation() {
  void (*callback)(void *) = nullptr;
  void *context = nullptr;
  {
    std::lock_guard<std::mutex> lock(state_lock);
    callback = hooks.cancellation_changed;
    context = hooks.context;
  }
  if (callback)
    callback(context);
}

template <class T> T load(uint64_t address) {
  T value;
  if (!hooks.pointer)
    throw std::runtime_error("thread service has no checked memory hook");
  void *p = hooks.pointer(hooks.context, address, sizeof(value), false);
  if (!p)
    throw std::runtime_error("thread service rejected guest read");
  std::memcpy(&value, p, sizeof(value));
  return value;
}
template <class T> void store(uint64_t address, T value) {
  if (!hooks.pointer)
    throw std::runtime_error("thread service has no checked memory hook");
  void *p = hooks.pointer(hooks.context, address, sizeof(value), true);
  if (!p)
    throw std::runtime_error("thread service rejected guest write");
  std::memcpy(p, &value, sizeof(value));
}
bool named(const char *a, const char *b) { return std::strcmp(a, b) == 0; }
uint64_t handle(const Worker &worker) {
  return reinterpret_cast<uint64_t>(worker.identity.data());
}
std::shared_ptr<Worker> worker(uint64_t id) {
  std::lock_guard<std::mutex> lock(state_lock);
  auto found = workers.find(id);
  return found == workers.end() ? nullptr : found->second;
}
void record_error(const char *message) {
  std::lock_guard<std::mutex> lock(state_lock);
  if (error.empty())
    error = message;
  stop = true;
}
void copy_name(char (&destination)[32], uint64_t source) {
  std::memset(destination, 0, sizeof(destination));
  if (!source)
    return;
  for (unsigned i = 0; i < 31; ++i) {
    destination[i] = load<char>(source + i);
    if (!destination[i])
      break;
  }
}
std::shared_ptr<Cond> cond(uint64_t cell) {
  uint64_t id = load<uint64_t>(cell);
  std::lock_guard<std::mutex> lock(state_lock);
  if (!id) {
    auto value = std::make_shared<Cond>();
    id = reinterpret_cast<uint64_t>(value.get());
    if (hooks.register_range)
      hooks.register_range(hooks.context, value.get(), sizeof(Cond));
    conditions.emplace(id, value);
    store(cell, id);
    return value;
  }
  auto found = conditions.find(id);
  if (found == conditions.end())
    throw std::runtime_error("unknown guest condition variable");
  return found->second;
}
std::shared_ptr<Event> event(uint64_t id) {
  std::lock_guard<std::mutex> lock(state_lock);
  auto found = events.find(id);
  return found == events.end() ? nullptr : found->second;
}
void finish(Worker &value, uint64_t result) {
  {
    std::lock_guard<std::mutex> lock(value.lock);
    value.result = result;
    value.done = true;
  }
  value.cv.notify_all();
}
void run_worker(std::shared_ptr<Worker> value, uint64_t pc, uint64_t argument) {
  current = value.get();
  specific.fill(0);
  uint64_t result = 0;
  try {
    if (!hooks.run)
      throw std::runtime_error("thread service has no guest execution hook");
    result = hooks.run(hooks.context, pc, argument);
    {
      std::lock_guard<std::mutex> lock(value->lock);
      value->finishing = true;
      if (value->cancel.exchange(false))
        result = kDcpThreadCanceledResult;
    }
    if (!shutting_down()) {
      for (unsigned i = 0; i < keys.size(); ++i) {
        if (shutting_down())
          break;
        uint64_t destructor = 0;
        {
          std::lock_guard<std::mutex> lock(state_lock);
          if (keys[i].used)
            destructor = keys[i].destructor;
        }
        if (specific[i] && destructor) {
          uint64_t arg = specific[i];
          specific[i] = 0;
          hooks.run(hooks.context, destructor, arg);
        }
      }
    }
  } catch (const std::exception &e) {
    if (!dcp_thread_stopping())
      record_error(e.what());
  } catch (...) {
    record_error("guest worker threw an unknown exception");
  }
  finish(*value, result);
  current = nullptr;
}
uint64_t wait_cond(const uint64_t *a, bool timed) {
  auto condition = cond(a[0]);
  const uint64_t mutex_address = load<uint64_t>(a[1]);
  void *pointer = hooks.pointer
                      ? hooks.pointer(hooks.context, mutex_address,
                                      sizeof(std::recursive_mutex), true)
                      : nullptr;
  if (!pointer)
    return invalid;
  auto &mutex = *static_cast<std::recursive_mutex *>(pointer);
  std::unique_lock<std::recursive_mutex> lock(mutex, std::adopt_lock);
  const auto deadline =
      Clock::now() + std::chrono::microseconds(timed ? uint32_t(a[2]) : 0);
  uint64_t result = ok;
  while (!dcp_thread_stopping()) {
    auto until = Clock::now() + std::chrono::milliseconds(10);
    if (timed && until > deadline)
      until = deadline;
    if (condition->cv.wait_until(lock, until) == std::cv_status::no_timeout)
      break;
    if (timed && Clock::now() >= deadline) {
      result = timedout;
      break;
    }
  }
  if (dcp_thread_stopping())
    result = canceled;
  lock.release(); // Guest still owns one acquisition after returning from the
                  // wait.
  return result;
}
} // namespace

void dcp_thread_init(DcpThreadHooks callbacks) {
  std::lock_guard<std::mutex> lock(state_lock);
  for (auto &[id, value] : workers)
    if (value->thread.joinable())
      throw std::runtime_error("previous guest worker has not joined");
  hooks = callbacks;
  workers.clear();
  conditions.clear();
  events.clear();
  retired_conditions.clear();
  retired_events.clear();
  keys = {};
  error.clear();
  next_queue = 0x1000;
  stop = false;
  specific.fill(0);
}
uint64_t dcp_thread_current_handle() { return current ? handle(*current) : 0; }
bool dcp_thread_stopping() {
  return shutting_down() || dcp_thread_current_cancelled();
}
const std::atomic<bool> *dcp_thread_stop_flag() { return &stop; }
const std::atomic<bool> *dcp_thread_current_cancel_flag() {
  return current ? &current->cancel : nullptr;
}
bool dcp_thread_current_cancelled() { return current && current->cancel.load(); }
bool dcp_thread_shutdown_requested() { return shutting_down(); }
bool dcp_thread_attributes(uint64_t id, void *destination) {
  auto value = worker(id);
  if (!value || !destination)
    return false;
  std::lock_guard<std::mutex> lock(value->lock);
  std::memcpy(destination, &value->attr, 32);
  return true;
}
const char *dcp_thread_error() {
  static thread_local std::string copy;
  std::lock_guard<std::mutex> lock(state_lock);
  copy = error;
  return copy.c_str();
}
bool dcp_thread_stop_and_join(uint32_t timeout_ms) {
  stop = true;
  notify_cancellation();
  std::vector<std::shared_ptr<Worker>> pending;
  {
    std::lock_guard<std::mutex> lock(state_lock);
    for (auto &[id, c] : conditions)
      c->cv.notify_all();
    for (auto &[id, e] : events)
      e->cv.notify_all();
    for (auto &[id, w] : workers)
      pending.push_back(w);
  }
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  bool joined = true;
  for (auto &value : pending) {
    if (value.get() == current) {
      joined = false;
      continue;
    }
    {
      std::unique_lock<std::mutex> lock(value->lock);
      if (!value->cv.wait_until(lock, deadline, [&] { return value->done; })) {
        joined = false;
        continue;
      }
    }
    {
      std::lock_guard<std::mutex> joining(value->join_lock);
      if (value->thread.joinable())
        value->thread.join();
    }
  }
  if (joined) {
    std::lock_guard<std::mutex> lock(state_lock);
    hooks.cancellation_changed = nullptr;
  }
  return joined;
}

bool dcp_thread_service(const char *name, const uint64_t a[6],
                        uint64_t *result) {
  if (!name || !a || !result)
    return false;
  *result = ok;
  if (named(name, "scePthreadCreate")) {
    if (!a[0] || !a[2]) {
      *result = invalid;
      return true;
    }
    auto value = std::make_shared<Worker>();
    if (a[1]) {
      uint64_t attr = load<uint64_t>(a[1]);
      if (attr)
        value->attr = load<Attr>(attr);
    }
    copy_name(value->name, a[4]);
    value->detached = value->attr.detach != 0;
    const uint64_t id = handle(*value);
    if (hooks.register_range)
      hooks.register_range(hooks.context, value->identity.data(),
                           value->identity.size());
    store(a[0], id);
    {
      std::lock_guard<std::mutex> lock(state_lock);
      workers.emplace(id, value);
    }
    try {
      std::lock_guard<std::mutex> lock(value->lock);
      value->thread = std::thread(run_worker, value, a[2], a[3]);
    } catch (const std::exception &e) {
      record_error(e.what());
      finish(*value, 0);
      *result = 0x80020023;
    }
  } else if (named(name, "scePthreadJoin")) {
    auto value = worker(a[0]);
    if (!value || value.get() == current || value->detached) {
      *result = invalid;
      return true;
    }
    std::unique_lock<std::mutex> lock(value->lock);
    while (!value->done && !dcp_thread_stopping())
      value->cv.wait_for(lock, std::chrono::milliseconds(10));
    if (!value->done) {
      *result = canceled;
      return true;
    }
    uint64_t returned = value->result;
    lock.unlock();
    {
      std::lock_guard<std::mutex> joining(value->join_lock);
      if (value->thread.joinable())
        value->thread.join();
    }
    if (a[1])
      store(a[1], returned);
  } else if (named(name, "scePthreadEqual")) {
    *result = a[0] == a[1];
  } else if (named(name, "scePthreadDetach") ||
             named(name, "scePthreadCancel")) {
    auto value = worker(a[0]);
    if (!value) {
      *result = invalid;
      return true;
    }
    if (named(name, "scePthreadDetach"))
      value->detached = true;
    else {
      bool changed = false;
      {
        std::lock_guard<std::mutex> lock(value->lock);
        if (!value->done && !value->finishing) {
          value->cancel = true;
          changed = true;
        }
      }
      if (changed)
        notify_cancellation();
    }
  } else if (named(name, "scePthreadYield") || named(name, "sched_yield")) {
    std::this_thread::yield();
  } else if (named(name, "scePthreadSetprio") ||
             named(name, "scePthreadGetprio") ||
             named(name, "scePthreadSetaffinity") ||
             named(name, "scePthreadGetaffinity") ||
             named(name, "scePthreadRename") ||
             named(name, "scePthreadGetname")) {
    auto value = worker(a[0]);
    if (!value) {
      *result = invalid;
      return true;
    }
    std::lock_guard<std::mutex> lock(value->lock);
    if (named(name, "scePthreadSetprio"))
      value->attr.prio = int32_t(a[1]);
    else if (named(name, "scePthreadGetprio"))
      store(a[1], value->attr.prio);
    else if (named(name, "scePthreadSetaffinity"))
      value->attr.affinity = a[1];
    else if (named(name, "scePthreadGetaffinity"))
      store(a[1], value->attr.affinity);
    else if (named(name, "scePthreadRename"))
      copy_name(value->name, a[1]);
    else
      for (unsigned i = 0; i < 32; ++i)
        store(a[1] + i, value->name[i]);
  } else if (named(name, "scePthreadKeyCreate")) {
    std::lock_guard<std::mutex> lock(state_lock);
    unsigned i = 0;
    while (i < keys.size() && keys[i].used)
      ++i;
    if (i == keys.size())
      *result = 0x80020023;
    else {
      keys[i] = {true, a[1]};
      store(a[0], int32_t(i));
    }
  } else if (named(name, "scePthreadKeyDelete")) {
    std::lock_guard<std::mutex> lock(state_lock);
    if (a[0] >= keys.size())
      *result = invalid;
    else
      keys[a[0]] = {};
  } else if (named(name, "scePthreadGetspecific") ||
             named(name, "scePthreadSetspecific")) {
    if (a[0] >= keys.size()) {
      *result = invalid;
      return true;
    }
    if (named(name, "scePthreadGetspecific"))
      *result = specific[a[0]];
    else
      specific[a[0]] = a[1];
  } else if (named(name, "scePthreadOnce")) {
    std::lock_guard<std::recursive_mutex> lock(once_lock);
    if (load<int32_t>(a[0]) == 0) {
      store(a[0], int32_t(1));
      if (hooks.run)
        hooks.run(hooks.context, a[1], 0);
    }
  } else if (named(name, "scePthreadCondInit")) {
    store(a[0], uint64_t(0));
    cond(a[0]);
  } else if (named(name, "scePthreadCondDestroy")) {
    uint64_t id = load<uint64_t>(a[0]);
    std::lock_guard<std::mutex> lock(state_lock);
    auto found = conditions.find(id);
    if (found != conditions.end()) {
      retired_conditions.push_back(found->second);
      conditions.erase(found);
    }
    store(a[0], uint64_t(0));
  } else if (named(name, "scePthreadCondWait") ||
             named(name, "scePthreadCondTimedwait")) {
    *result = wait_cond(a, named(name, "scePthreadCondTimedwait"));
  } else if (named(name, "scePthreadCondSignal")) {
    cond(a[0])->cv.notify_one();
  } else if (named(name, "scePthreadCondBroadcast")) {
    cond(a[0])->cv.notify_all();
  } else if (named(name, "sceKernelCreateEventFlag")) {
    auto value = std::make_shared<Event>();
    value->bits = a[3];
    uint64_t id = reinterpret_cast<uint64_t>(value.get());
    if (hooks.register_range)
      hooks.register_range(hooks.context, value.get(), sizeof(Event));
    {
      std::lock_guard<std::mutex> lock(state_lock);
      events.emplace(id, value);
    }
    store(a[0], id);
  } else if (named(name, "sceKernelDeleteEventFlag") ||
             named(name, "sceKernelSetEventFlag") ||
             named(name, "sceKernelClearEventFlag") ||
             named(name, "sceKernelPollEventFlag") ||
             named(name, "sceKernelWaitEventFlag")) {
    auto value = event(a[0]);
    if (!value) {
      *result = invalid;
      return true;
    }
    std::unique_lock<std::mutex> lock(value->lock);
    if (named(name, "sceKernelDeleteEventFlag")) {
      value->deleted = true;
      lock.unlock();
      value->cv.notify_all();
      std::lock_guard<std::mutex> global(state_lock);
      retired_events.push_back(value);
      events.erase(a[0]);
    } else if (named(name, "sceKernelSetEventFlag")) {
      value->bits |= a[1];
      lock.unlock();
      value->cv.notify_all();
    } else if (named(name, "sceKernelClearEventFlag"))
      value->bits &= a[1];
    else {
      bool timed = a[4] != 0 && named(name, "sceKernelWaitEventFlag");
      uint32_t usec = timed ? load<uint32_t>(a[4]) : 0;
      auto deadline = Clock::now() + std::chrono::microseconds(usec);
      auto match = [&] {
        return (a[2] & 1) ? (value->bits & a[1]) == a[1]
                          : (value->bits & a[1]) != 0;
      };
      while (!match() && !value->deleted && !dcp_thread_stopping()) {
        if (named(name, "sceKernelPollEventFlag")) {
          *result = busy;
          break;
        }
        auto until = Clock::now() + std::chrono::milliseconds(10);
        if (timed && until > deadline)
          until = deadline;
        value->cv.wait_until(lock, until);
        if (timed && Clock::now() >= deadline && !match()) {
          *result = timedout;
          store(a[4], uint32_t(0));
          break;
        }
      }
      if (value->deleted || dcp_thread_stopping())
        *result = canceled;
      if (a[3])
        store(a[3], value->bits);
      if (*result == ok) {
        if (a[2] & 0x10)
          value->bits = 0;
        else if (a[2] & 0x20)
          value->bits &= ~a[1];
      }
    }
  } else if (named(name, "sceKernelCreateEqueue")) {
    std::lock_guard<std::mutex> lock(state_lock);
    store(a[0], next_queue);
    next_queue += 16;
  } else if (named(name, "sceKernelDeleteEqueue")) {
  } else
    return false;
  return true;
}
