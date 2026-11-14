// Native public-API probe: sample ownership concurrently with paused startup,
// stop, destruction and another session. Uses private real-module archive
// inputs.
#include "game.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std::chrono_literals;
namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Usage: stats-fixture EXTERNAL_OUTPUT");
    const auto root = std::filesystem::path(argv[1]) /
                      ("session-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    setenv("DCP_SAVE_ROOT", root.c_str(), 1);
    setenv("DCP_GAME_MODE", "play", 1);
    DcpGameStats idle;
    check(dcp_game_snapshot(&idle) == 0 && !idle.running && !idle.steps &&
              idle.libc_result == -1,
          "idle snapshot not initialized");
    check(dcp_game_snapshot(nullptr) == 0, "null snapshot accepted");
    std::atomic<bool> finish{false}, invalid{false};
    std::atomic<uint64_t> samples{0}, liveSamples{0};
    std::vector<std::thread> readers;
    for (unsigned i = 0; i < 4; ++i)
      readers.emplace_back([&] {
        while (!finish.load()) {
          DcpGameStats snapshot;
          int active = dcp_game_snapshot(&snapshot);
          if (active != snapshot.running || snapshot.current_scene[15] != 0 ||
              snapshot.first_error[255] != 0 ||
              (!active && (snapshot.steps || snapshot.workers ||
                           snapshot.vblanks || snapshot.libc_result != -1)))
            invalid = true;
          if (active)
            ++liveSamples;
          ++samples;
          std::this_thread::yield();
        }
      });
    unsigned sessions = 0;
    try {
      for (; sessions < 4; ++sessions) {
        dcp_game_set_paused(1);
        check(dcp_game_configure({}) != 0, "idle configure failed");
        auto runtime = std::async(std::launch::async,
                                  [] { return std::string(dcp_cpu_report()); });
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        DcpGameStats snapshot{};
        while (!dcp_game_snapshot(&snapshot) &&
               std::chrono::steady_clock::now() < deadline)
          std::this_thread::yield();
        check(snapshot.running && snapshot.paused,
              "session did not publish paused state");
        const auto before = samples.load();
        std::this_thread::sleep_for(20ms);
        check(samples.load() > before,
              "paused guest blocked frontend snapshots");
        dcp_game_request_stop();
        check(runtime.wait_for(5s) == std::future_status::ready,
              "paused stop failed to wake guest");
        const auto result = runtime.get();
        check(result.find("\"stage\":\"stopped\"") != std::string::npos,
              "paused session did not report clean stop");
        check(!dcp_game_snapshot(&idle) && !idle.running && !idle.steps,
              "destroyed session remained visible");
      }
    } catch (...) {
      dcp_game_request_stop();
      finish = true;
      for (auto &reader : readers)
        reader.join();
      throw;
    }
    finish = true;
    for (auto &reader : readers)
      reader.join();
    check(!invalid.load() && liveSamples > 0,
          "concurrent snapshot lost ownership or termination");
    dcp_game_set_paused(0);
    std::filesystem::remove_all(root);
    std::cout << "{\"sessions\":" << sessions
              << ",\"concurrent_readers\":4,\"snapshots\":" << samples
              << ",\"live_snapshots\":" << liveSamples
              << ",\"paused_stop\":true,\"failed\":0}\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
