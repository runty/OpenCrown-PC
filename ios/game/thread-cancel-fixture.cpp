// Public native-thread adapter probe. The run hook is a cooperative guest shim;
// real translated GuestThreadExit/Engine isolation is verified by the game run.
#include "thread-services.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
namespace {
// Independent ABI oracle: FreeBSD/PS4 uses pointer value 1, rather than the
// Linux UINT64_MAX sentinel. Do not derive this expectation from production.
constexpr uint64_t canceled_value = 1;
constexpr uint64_t destructor_pc = 0xd00d;
constexpr uint64_t wait_canceled = 0x80020055;
enum class Mode : uint64_t {
  Loop = 1,
  Condition,
  TimedCondition,
  Event,
  Normal,
  Parked,
  NativeBlocked
};
struct Job {
  std::atomic<bool> entered{false}, release{false}, cleanupEntered{false},
      cleanupRelease{true};
  std::atomic<bool> invalid{false};
  std::atomic<uint64_t> progress{0}, destructorArgument{0}, waitResult{0};
  std::atomic<unsigned> destructors{0}, completedDestructors{0};
};
struct Context {
  alignas(16) std::array<uint64_t, 512> cells{};
  std::array<Job, 128> jobs;
  std::recursive_mutex guestMutex;
  std::atomic<bool> globalStop{false};
  std::atomic<unsigned> cancellationNotifications{0};
  std::atomic<bool> retired{false};
  std::atomic<unsigned> retiredNotifications{0};
  std::mutex parkLock;
  std::condition_variable parkChanged;
  struct Region {
    uint64_t address;
    size_t bytes;
  };
  std::mutex regionsLock;
  std::vector<Region> regions;
  uint64_t address(size_t cell) { return uint64_t(&cells[cell]); }
  uint64_t key() const { return uint32_t(cells[0]); }
};
unsigned passed = 0;
void check(bool okay, const char *message) {
  if (!okay)
    throw std::runtime_error(message);
  ++passed;
}
uint64_t call(const char *name, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0,
              uint64_t d = 0, uint64_t e = 0) {
  uint64_t arguments[6] = {a, b, c, d, e}, result = UINT64_MAX;
  if (!dcp_thread_service(name, arguments, &result))
    throw std::runtime_error("fixture service is unhandled");
  return result;
}
template <class F>
bool wait_for(F predicate, std::chrono::milliseconds timeout = 2000ms) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  return predicate();
}
void register_region(void *context, void *address, size_t bytes) {
  auto &state = *static_cast<Context *>(context);
  std::lock_guard<std::mutex> guard(state.regionsLock);
  state.regions.push_back({uint64_t(address), bytes});
}
void *pointer(void *context, uint64_t address, size_t bytes, bool) {
  auto &state = *static_cast<Context *>(context);
  std::lock_guard<std::mutex> guard(state.regionsLock);
  for (const auto &region : state.regions)
    if (address >= region.address && bytes <= region.bytes &&
        address - region.address <= region.bytes - bytes)
      return reinterpret_cast<void *>(address);
  return nullptr;
}
uint64_t run(void *context, uint64_t pc, uint64_t argument) {
  auto &state = *static_cast<Context *>(context);
  if (pc == destructor_pc) {
    if (argument < 0x1000 || argument - 0x1000 >= state.jobs.size())
      throw std::runtime_error("invalid TLS destructor argument");
    auto &job = state.jobs[size_t(argument - 0x1000)];
    if (++job.destructors != 1 ||
        call("scePthreadGetspecific", state.key()) != 0 ||
        dcp_thread_current_cancelled() || dcp_thread_stopping() ||
        !dcp_thread_current_handle())
      job.invalid = true;
    job.destructorArgument = argument;
    job.cleanupEntered = true;
    if (!wait_for([&] {
          return job.cleanupRelease.load() || state.globalStop.load();
        }))
      job.invalid = true;
    // A new cancel after worker exit has begun must not abort guest TLS
    // cleanup.
    if (!state.globalStop &&
        (dcp_thread_current_cancelled() || dcp_thread_stopping()))
      job.invalid = true;
    ++job.completedDestructors;
    return 0;
  }
  if (argument >= state.jobs.size())
    throw std::runtime_error("invalid fixture job");
  auto &job = state.jobs[size_t(argument)];
  if (!dcp_thread_current_handle() ||
      call("scePthreadSetspecific", state.key(), 0x1000 + argument))
    job.invalid = true;
  if (pc == uint64_t(Mode::Normal)) {
    job.entered = true;
    return 0x2000 + argument;
  }
  if (pc == uint64_t(Mode::Condition) || pc == uint64_t(Mode::TimedCondition)) {
    std::unique_lock<std::recursive_mutex> lock(state.guestMutex);
    job.entered = true;
    while (!dcp_thread_stopping() && !job.release.load()) {
      auto status =
          call(pc == uint64_t(Mode::Condition) ? "scePthreadCondWait"
                                               : "scePthreadCondTimedwait",
               state.address(1), state.address(2), 10000000);
      job.waitResult = status;
      if (status == wait_canceled)
        break;
    }
  } else if (pc == uint64_t(Mode::Event)) {
    job.entered = true;
    job.waitResult = call("sceKernelWaitEventFlag", state.cells[3], 1, 1);
  } else if (pc == uint64_t(Mode::NativeBlocked)) {
    job.entered = true;
    if (!wait_for([&] { return job.release.load(); }))
      job.invalid = true;
  } else if (pc == uint64_t(Mode::Parked)) {
    std::unique_lock<std::mutex> lock(state.parkLock);
    job.entered = true;
    if (!state.parkChanged.wait_for(lock, 2s, [&] {
          return dcp_thread_stopping() || job.release.load();
        }))
      job.invalid = true;
  } else if (pc == uint64_t(Mode::Loop)) {
    job.entered = true;
    while (!dcp_thread_stopping() && !job.release.load()) {
      ++job.progress;
      std::this_thread::yield();
    }
  } else
    throw std::runtime_error("unknown fixture entry");
  // Production catches its private GuestThreadExit here and returns the same
  // sentinel. This hook intentionally uses only the public cancellation state.
  if (dcp_thread_current_cancelled())
    return canceled_value;
  return 0x2000 + argument;
}
void initialize(Context &context) {
  register_region(&context, context.cells.data(), sizeof(context.cells));
  register_region(&context, &context.guestMutex, sizeof(context.guestMutex));
  dcp_thread_init({&context, run,
                   [](void *opaque) {
                     return static_cast<Context *>(opaque)->globalStop.load();
                   },
                   pointer, register_region,
                   [](void *opaque) {
                     auto &state = *static_cast<Context *>(opaque);
                     ++state.cancellationNotifications;
                     if (state.retired.load())
                       ++state.retiredNotifications;
                     // This read takes the adapter's state lock. Notification
                     // callbacks must execute outside that lock.
                     if (*dcp_thread_error())
                       state.retiredNotifications++;
                     std::lock_guard<std::mutex> lock(state.parkLock);
                     state.parkChanged.notify_all();
                   }});
  context.cells[2] = uint64_t(&context.guestMutex);
  check(call("scePthreadKeyCreate", context.address(0), destructor_pc) == 0,
        "TLS key create failed");
  check(call("scePthreadCondInit", context.address(1)) == 0,
        "condition create failed");
  check(call("sceKernelCreateEventFlag", context.address(3)) == 0,
        "event create failed");
}
uint64_t create(Context &context, size_t slot, Mode mode) {
  check(call("scePthreadCreate", context.address(16 + slot * 2), 0,
             uint64_t(mode), slot) == 0,
        "worker create failed");
  check(wait_for([&] { return context.jobs[slot].entered.load(); }),
        "worker did not enter fixture");
  return context.cells[16 + slot * 2];
}
uint64_t join(Context &context, size_t slot) {
  auto joining = std::async(std::launch::async, [&] {
    return call("scePthreadJoin", context.cells[16 + slot * 2],
                context.address(17 + slot * 2));
  });
  if (joining.wait_for(2s) != std::future_status::ready) {
    context.globalStop = true;
    dcp_thread_stop_and_join(2000);
    throw std::runtime_error("worker join timed out");
  }
  check(joining.get() == 0, "worker join failed");
  return context.cells[17 + slot * 2];
}
void expect_cleanup(Context &context, size_t slot) {
  auto &job = context.jobs[slot];
  check(job.destructors == 1 && job.completedDestructors == 1 &&
            job.destructorArgument == 0x1000 + slot && !job.invalid,
        "TLS cleanup missing, repeated, canceled again or given wrong original "
        "value");
}
} // namespace
int main() {
  try {
    check(!dcp_thread_current_handle() && !dcp_thread_current_cancelled(),
          "native caller appears canceled");
    Context context;
    initialize(context);
    try {
      auto survivor = create(context, 0, Mode::Loop);
      (void)survivor;
      auto victim = create(context, 1, Mode::Loop);
      check(call("scePthreadCancel", victim) == 0, "worker cancel failed");
      check(join(context, 1) == canceled_value,
            "join did not return PTHREAD_CANCELED");
      expect_cleanup(context, 1);
      const auto before = context.jobs[0].progress.load();
      check(wait_for(
                [&] { return context.jobs[0].progress.load() > before + 50; }),
            "cancel stopped an unrelated worker");
      check(!dcp_thread_stopping() && !*dcp_thread_error(),
            "cancel set a global error or stop state");
      context.jobs[0].release = true;
      check(join(context, 0) == 0x2000, "surviving worker result changed");
      expect_cleanup(context, 0);
      for (size_t i = 2; i < 5; ++i) {
        Mode mode = i == 2   ? Mode::Condition
                    : i == 3 ? Mode::TimedCondition
                             : Mode::Event;
        auto worker = create(context, i, mode);
        std::this_thread::sleep_for(15ms);
        check(call("scePthreadCancel", worker) == 0, "waiter cancel failed");
        check(join(context, i) == canceled_value,
              "canceled waiter join result changed");
        check(context.jobs[i].waitResult == wait_canceled,
              "condition/event wait did not observe cancellation");
        expect_cleanup(context, i);
      }
      create(context, 5, Mode::Normal);
      check(join(context, 5) == 0x2005, "normal worker result changed");
      expect_cleanup(context, 5);
      // Deterministic late-cancel race: cleanup remains active while
      // cancellation requests arrive after the original worker body already
      // returned.
      context.jobs[6].cleanupRelease = false;
      auto finishing = create(context, 6, Mode::Normal);
      check(wait_for([&] { return context.jobs[6].cleanupEntered.load(); }),
            "cleanup did not begin");
      const unsigned notificationsBeforeCleanup =
          context.cancellationNotifications.load();
      for (unsigned i = 0; i < 100; ++i)
        check(call("scePthreadCancel", finishing) == 0, "late cancel rejected");
      check(context.cancellationNotifications.load() ==
                notificationsBeforeCleanup,
            "late cancellation signaled a worker already in TLS cleanup");
      context.jobs[6].cleanupRelease = true;
      check(join(context, 6) == 0x2006,
            "late cancel changed an exited worker result");
      expect_cleanup(context, 6);
      for (size_t i = 7; i < 71; ++i) {
        auto worker = create(context, i, Mode::Loop);
        std::array<std::thread, 3> cancelers;
        for (auto &canceler : cancelers)
          canceler = std::thread([&] {
            for (unsigned n = 0; n < 8; ++n)
              if (call("scePthreadCancel", worker) != 0)
                context.jobs[i].invalid = true;
          });
        for (auto &canceler : cancelers)
          canceler.join();
        check(join(context, i) == canceled_value,
              "racing cancel join result changed");
        expect_cleanup(context, i);
      }
      auto parked = create(context, 71, Mode::Parked);
      const unsigned notificationsBeforeParked =
          context.cancellationNotifications.load();
      check(call("scePthreadCancel", parked) == 0,
            "parked worker cancellation failed");
      check(join(context, 71) == canceled_value,
            "parked worker did not wake with canceled result");
      expect_cleanup(context, 71);
      check(context.cancellationNotifications.load() >
                notificationsBeforeParked,
            "cancellation did not notify a parked guest");
      check(!*dcp_thread_error() && !dcp_thread_stopping(),
            "repeated cancellation polluted global state");
      check(dcp_thread_stop_and_join(2000),
            "finished-worker cleanup did not join");
    } catch (...) {
      context.globalStop = true;
      for (auto &job : context.jobs) {
        job.release = true;
        job.cleanupRelease = true;
      }
      dcp_thread_stop_and_join(2000);
      throw;
    }
    // Global shutdown deliberately skips guest destructors, whose
    // memory/services may be disappearing. It is distinct from canceling a
    // single worker.
    Context shutdown;
    initialize(shutdown);
    try {
      create(shutdown, 0, Mode::Event);
      create(shutdown, 1, Mode::Loop);
      shutdown.globalStop = true;
      check(dcp_thread_stop_and_join(2000),
            "global stop did not release waiters");
      check(shutdown.jobs[0].destructors == 0 &&
                shutdown.jobs[1].destructors == 0,
            "global stop entered guest TLS cleanup");
      check(!*dcp_thread_error(), "global shutdown reported a worker error");
    } catch (...) {
      shutdown.globalStop = true;
      dcp_thread_stop_and_join(2000);
      throw;
    }
    // Retirement is a lifecycle boundary: no service caller may retain the
    // old hook context after a successful stop/join. Race native callers while
    // it is alive, join them, then retire/delete and exercise later stops.
    for (unsigned generation = 0; generation < 16; ++generation) {
      auto retired = std::make_unique<Context>();
      initialize(*retired);
      const auto handle = create(*retired, 0, Mode::Loop);
      std::array<std::thread, 4> cancelers;
      std::atomic<bool> failed{false};
      for (auto &canceler : cancelers)
        canceler = std::thread([&] {
          for (unsigned n = 0; n < 32; ++n)
            if (call("scePthreadCancel", handle) != 0)
              failed = true;
        });
      std::array<std::thread, 2> stoppers;
      for (auto &stopper : stoppers)
        stopper = std::thread([&] {
          if (!dcp_thread_stop_and_join(2000))
            failed = true;
        });
      for (auto &canceler : cancelers)
        canceler.join();
      for (auto &stopper : stoppers)
        stopper.join();
      check(!failed, "racing stop/cancel callers failed");
      const auto notifications = retired->cancellationNotifications.load();
      check(notifications > 0, "active context received no stop notification");
      retired->retired = true;
      for (unsigned n = 0; n < 8; ++n)
        check(dcp_thread_stop_and_join(0),
              "repeated stop after retirement failed");
      check(retired->cancellationNotifications == notifications &&
                retired->retiredNotifications == 0,
            "successful join retained a notification into a retired context");
      retired.reset();
      for (unsigned n = 0; n < 8; ++n)
        check(dcp_thread_stop_and_join(0),
              "stop after context destruction failed");
      // Next initialize installs a fresh context after the old one is gone.
    }
    auto retained = std::make_unique<Context>();
    initialize(*retained);
    create(*retained, 0, Mode::NativeBlocked);
    const auto beforeFailedJoin = retained->cancellationNotifications.load();
    check(!dcp_thread_stop_and_join(0),
          "uncooperative native worker unexpectedly joined");
    const auto afterFailedJoin = retained->cancellationNotifications.load();
    check(afterFailedJoin > beforeFailedJoin,
          "failed join lost its wake notification");
    check(!dcp_thread_stop_and_join(0) &&
              retained->cancellationNotifications > afterFailedJoin,
          "failed join cleared notification hooks before context was safe to "
          "free");
    retained->jobs[0].release = true;
    check(dcp_thread_stop_and_join(2000),
          "retained context failed its later successful join");
    check(retained->jobs[0].destructors == 0,
          "global stop entered retained worker guest cleanup");
    const auto afterRetainedJoin = retained->cancellationNotifications.load();
    retained->retired = true;
    check(dcp_thread_stop_and_join(0) &&
              retained->cancellationNotifications == afterRetainedJoin &&
              retained->retiredNotifications == 0,
          "later successful join did not retire retained hooks");
    retained.reset();
    check(dcp_thread_stop_and_join(0),
          "destroyed retained context was referenced");
    std::cout
        << "{\"checks\":" << passed
        << ",\"canceled_join_value\":" << canceled_value
        << ",\"canceled_workers\":69,\"unrelated_worker_survives\":true,"
           "\"tls_original_value_once\":true,\"wait_cancellation\":true,"
           "\"late_cancel_cleanup\":true,\"retired_context_generations\":16,"
           "\"failed_join_retains_context\":true,\"failed\":0}\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
