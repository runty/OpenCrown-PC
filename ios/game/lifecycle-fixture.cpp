// Real translated-runtime lifecycle regression. No UIKit or physical devices.
#include "game.h"
#include "offline-services.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
[[noreturn]] void fail(const std::string &message) {
  std::cerr << "FAIL: " << message << '\n';
  // An unresponsive runtime must not hang a future destructor or the test
  // runner.
  std::_Exit(1);
}
void require(bool okay, const std::string &message) {
  if (!okay)
    fail(message);
}
struct Host {
  const std::thread::id main_thread = std::this_thread::get_id();
  std::mutex mutex;
  std::condition_variable changed;
  bool pending = false, released = false, bad_callback = false;
  unsigned unloads = 0, main_handoffs = 0;
};
void will_unload(void *opaque) {
  auto &host = *static_cast<Host *>(opaque);
  std::unique_lock<std::mutex> lock(host.mutex);
  ++host.unloads;
  host.bad_callback =
      std::this_thread::get_id() == host.main_thread || !dcp_game_is_running();
  host.pending = true;
  host.changed.notify_all();
  if (!host.changed.wait_for(lock, 5s, [&] { return host.released; }))
    host.bad_callback = true;
  if (!dcp_game_is_running())
    host.bad_callback = true;
}
void pump_main_handoff(Host &host) {
  require(std::this_thread::get_id() == host.main_thread,
          "handoff ran off main thread");
  std::unique_lock<std::mutex> lock(host.mutex);
  if (!host.pending || host.released)
    return;
  require(dcp_game_is_running(),
          "running cleared before main-thread renderer drain");
  require(!dcp_game_configure({}),
          "host reconfiguration admitted before unload completed");
  // Model a main-thread renderer drain while the guest coordinator awaits it.
  std::this_thread::sleep_for(1ms);
  require(dcp_game_is_running(),
          "running cleared during main-thread renderer drain");
  ++host.main_handoffs;
  host.released = true;
  host.changed.notify_all();
}
void require_alive(std::future<std::string> &future, Host &host) {
  {
    std::lock_guard<std::mutex> lock(host.mutex);
    require(!host.pending, "runtime began unloading before explicit stop");
  }
  require(future.wait_for(0ms) != std::future_status::ready,
          "runtime ended before explicit stop");
}
void hold(std::future<std::string> &future, Host &host,
          std::chrono::milliseconds duration) {
  auto until = Clock::now() + duration;
  do {
    require_alive(future, host);
    std::this_thread::sleep_for(1ms);
  } while (Clock::now() < until);
}
uint64_t integer_field(const std::string &json, const char *name) {
  const std::string key = std::string("\"") + name + "\":";
  auto at = json.find(key);
  require(at != std::string::npos, "report missing " + key);
  return std::stoull(json.substr(at + key.size()));
}
struct Result {
  std::string report;
  double stop_ms;
};
enum class Mode {
  ParkedStop,
  ResumeCycles,
  ConfiguredCancel,
  ConcurrentReport
};
uint64_t pad_call(const char *name, uint64_t argument) {
  uint64_t args[6] = {argument, 0, 0, 0, 0, 0}, result = 0;
  require(dcp_offline_service(name, args, &result),
          "pad adapter service not found");
  return result;
}
Result session(const std::filesystem::path &root, unsigned index, Mode mode) {
  const bool cycles = mode == Mode::ResumeCycles;
  const bool precancel = mode == Mode::ConfiguredCancel;
  Host host;
  const auto save = root / "saves" / ("session-" + std::to_string(index));
  std::filesystem::create_directories(save);
  require(!setenv("DCP_SAVE_ROOT", save.c_str(), 1),
          "cannot configure disposable saves");
  require(!dcp_game_is_running(), "previous session still running");
  require(dcp_game_configure({&host, will_unload, nullptr}),
          "host configuration rejected while idle");
  dcp_game_set_paused(1);
  auto stopped_at = Clock::now();
  if (precancel) {
    dcp_game_request_stop();
    require(!dcp_game_is_running(),
            "configured cancellation started a session itself");
  }
  auto future = std::async(std::launch::async, [] {
    const char *report = dcp_cpu_report();
    return std::string(report ? report : "{}");
  });
  auto deadline = Clock::now() + 5s;
  while (!dcp_game_is_running()) {
    require(Clock::now() < deadline, "runtime did not publish running state");
    if (!precancel)
      require_alive(future, host);
    else
      require(future.wait_for(0ms) != std::future_status::ready,
              "configured cancellation never published running");
    std::this_thread::sleep_for(1ms);
  }
  require(!dcp_game_configure({}),
          "host reconfiguration admitted while running");
  // Allow module initialization to reach its first checked/parked guest access.
  if (!precancel)
    hold(future, host, cycles ? 100ms : 50ms);
  if (mode == Mode::ConcurrentReport) {
    require(pad_call("scePadOpen", 1000) == 1,
            "cannot establish first-session adapter sentinel");
    auto second = std::async(std::launch::async, [] {
      const char *report = dcp_cpu_report();
      return std::string(report ? report : "{}");
    });
    require(second.wait_for(1s) == std::future_status::ready,
            "concurrent report was not rejected promptly");
    auto rejected = second.get();
    require(rejected.find("already running or stopping") != std::string::npos,
            "concurrent report did not return already-running error: " +
                rejected);
    require_alive(future, host);
    require(dcp_game_is_running(),
            "concurrent report cleared first session running state");
    require(pad_call("scePadGetHandle", 1000) == 1,
            "concurrent report reset first-session adapter state");
    require(pad_call("scePadClose", 1) == 0,
            "cannot close first-session adapter sentinel");
    std::ofstream(root / "concurrent-rejection.json") << rejected << '\n';
  }
  if (cycles) {
    auto resume_until_progress = [&] {
      DcpGameStats before{}, after{};
      dcp_game_snapshot(&before);
      dcp_game_set_paused(0);
      const auto progress_deadline = Clock::now() + 5s;
      do {
        require_alive(future, host);
        require(Clock::now() < progress_deadline,
                "resume did not execute translated code within five seconds");
        std::this_thread::sleep_for(1ms);
        dcp_game_snapshot(&after);
      } while (after.steps <= before.steps);
    };
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
      // Initialization can outlast a one-millisecond resume window. Wait for
      // actual translated progress, rather than fixed timing guesses.
      resume_until_progress();
      dcp_game_set_paused(1);
      hold(future, host, 2ms);
    }
    // Stop from running state as well as testing stop while initially parked.
    resume_until_progress();
  }
  if (!precancel) {
    require_alive(future, host);
    stopped_at = Clock::now();
    dcp_game_request_stop();
  }
  deadline = stopped_at + 3s;
  while (future.wait_for(0ms) != std::future_status::ready) {
    pump_main_handoff(host);
    require(Clock::now() < deadline,
            "stop lost a wakeup or failed to join within 3 seconds");
    std::this_thread::sleep_for(1ms);
  }
  auto report = future.get();
  const double stop_ms =
      std::chrono::duration<double, std::milli>(Clock::now() - stopped_at)
          .count();
  require(report.find("\"stage\":\"stopped\"") != std::string::npos,
          "wrong stop stage: " + report);
  require(report.find("\"error\":\"\"") != std::string::npos,
          "stop reported an error: " + report);
  require(!dcp_game_is_running(), "running remained set after joined teardown");
  require(host.unloads == 1 && host.main_handoffs == 1 && !host.bad_callback,
          "will_unload/main-thread handoff ordering failed");
  const auto steps = integer_field(report, "steps");
  if (!cycles)
    require(steps == 0, "initially paused session executed guest instructions");
  else
    require(steps > 0, "resume cycles never executed translated code");
  require(dcp_game_configure({}), "host configuration rejected after teardown");
  return {report, stop_ms};
}
unsigned count(const char *text) {
  auto n = std::stoul(text);
  require(n > 0 && n <= 1000, "session count must be between 1 and 1000");
  return unsigned(n);
}
} // namespace
int main(int argc, char **argv) {
  require(argc >= 2 && argc <= 4, "usage: lifecycle-fixture EXTERNAL_OUTPUT "
                                  "[paused_sessions=20] [cycle_sessions=5]");
  const std::filesystem::path output = argv[1];
  unsigned paused = argc > 2 ? count(argv[2]) : 20;
  unsigned cycles = argc > 3 ? count(argv[3]) : 5;
  std::filesystem::create_directories(output / "assets");
  require(!setenv("DCP_GAME_ROOT", (output / "assets").c_str(), 1),
          "cannot configure empty asset root");
  require(!setenv("DCP_GAME_MODE", "play", 1),
          "cannot enable interactive runtime mode");
  std::ofstream reports(output / "lifecycle-results.jsonl");
  require(bool(reports), "cannot create report log");
  double maximum_stop_ms = 0;
  for (unsigned index = 0; index < paused + cycles; ++index) {
    const bool cycling = index >= paused;
    auto result =
        session(output, index, cycling ? Mode::ResumeCycles : Mode::ParkedStop);
    maximum_stop_ms = std::max(maximum_stop_ms, result.stop_ms);
    reports << result.report << '\n';
  }
  auto cancelled = session(output, paused + cycles, Mode::ConfiguredCancel);
  reports << cancelled.report << '\n';
  auto concurrent =
      session(output, paused + cycles + 1, Mode::ConcurrentReport);
  reports << concurrent.report << '\n';
  maximum_stop_ms = std::max(maximum_stop_ms, concurrent.stop_ms);
  std::ofstream summary(output / "lifecycle-summary.json");
  summary << "{\"passed\":true,\"paused_stop_sessions\":" << paused
          << ",\"resume_cycle_sessions\":" << cycles
          << ",\"cycles_per_session\":3,\"maximum_stop_ms\":" << maximum_stop_ms
          << ",\"configured_idle_cancel_ms\":" << cancelled.stop_ms
          << ",\"concurrent_report_rejected\":true,\"unload_main_handoffs\":"
          << paused + cycles + 2 << "}\n";
  std::cout << "PASS: " << paused << " parked stops, " << cycles
            << " resume/pause/resume/stop sessions, configured idle "
               "cancellation, concurrent report rejection, "
            << paused + cycles + 2 << " main-thread unload handoffs; max stop "
            << maximum_stop_ms << " ms\n";
}
