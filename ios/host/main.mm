#include "game/game.h"
#include "render/capture.h"
#include "render/replay.h"
#import <Foundation/Foundation.h>
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <sys/stat.h>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
namespace {
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
struct Options {
  fs::path game, save, output;
  double seconds = 120;
  unsigned every = 60;
  bool offscreen = false;
};
void usage() {
  std::puts(
      "Usage: opencrown-aot-host --game-root DUMP --save-root DISPOSABLE_SAVES "
      "--output DIRECTORY [--seconds 120] [--offscreen] [--every 60]\nAll "
      "paths must be on mounted external disks. This runs the AOT checkpoint "
      "script, without live audio/controller support.");
}
fs::path external(const fs::path &input) {
  auto path = fs::weakly_canonical(input);
  auto relative = path.lexically_relative("/Volumes");
  if (relative.empty() || *relative.begin() == "..")
    throw std::runtime_error("Path must be on a mounted external disk: " +
                             path.string());
  auto volume = fs::path("/Volumes") / *relative.begin();
  struct stat current{}, parent{};
  if (stat(volume.c_str(), &current) || stat("/Volumes", &parent) ||
      current.st_dev == parent.st_dev)
    throw std::runtime_error("External volume is not mounted: " +
                             volume.string());
  return path;
}
bool within(const fs::path &path, const fs::path &parent) {
  auto r = path.lexically_relative(parent);
  return !r.empty() && *r.begin() != "..";
}
Options parse(int argc, char **argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    std::string key = argv[i];
    if (key == "--help") {
      usage();
      std::exit(0);
    }
    if (key == "--offscreen") {
      options.offscreen = true;
      continue;
    }
    if (i + 1 == argc)
      throw std::runtime_error("Missing value for " + key);
    std::string value = argv[++i];
    if (key == "--game-root")
      options.game = value;
    else if (key == "--save-root")
      options.save = value;
    else if (key == "--output")
      options.output = value;
    else if (key == "--seconds") {
      size_t end = 0;
      options.seconds = std::stod(value, &end);
      if (end != value.size() || !std::isfinite(options.seconds) ||
          options.seconds <= 0)
        throw std::runtime_error("seconds must be positive and finite");
    } else if (key == "--every") {
      size_t end = 0;
      auto number = std::stoul(value, &end);
      if (end != value.size() || value.front() == '-' || number > 1000000)
        throw std::runtime_error("every must be 0..1000000");
      options.every = unsigned(number);
    } else
      throw std::runtime_error("Unknown option " + key);
  }
  if (options.game.empty() || options.save.empty() || options.output.empty())
    throw std::runtime_error("game-root, save-root and output are required");
  options.game = external(options.game);
  options.save = external(options.save);
  options.output = external(options.output);
  if (within(options.save, options.game) ||
      within(options.output, options.game) ||
      within(options.game, options.save) ||
      within(options.game, options.output))
    throw std::runtime_error(
        "Save/output trees must be separate from the game dump");
  if (!fs::is_regular_file(options.game / "eboot.bin"))
    throw std::runtime_error("Game dump lacks eboot.bin");
  fs::create_directories(options.save);
  fs::create_directories(options.output);
  return options;
}
struct Handoff {
  std::atomic<bool> requested{false};
  std::mutex mutex;
  std::condition_variable changed;
  bool acknowledged = false;
  static void unload(void *context) {
    auto &self = *static_cast<Handoff *>(context);
    self.requested.store(true, std::memory_order_release);
    std::unique_lock lock(self.mutex);
    self.changed.wait(lock, [&] { return self.acknowledged; });
  }
  void acknowledge() {
    std::lock_guard lock(mutex);
    acknowledged = true;
    changed.notify_all();
  }
};
NSString *string(const std::string &value) {
  return [[NSString alloc] initWithBytes:value.data()
                                  length:value.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"Invalid UTF-8 diagnostic";
}
} // namespace

int main(int argc, char **argv) {
  @autoreleasepool {
    try {
      auto options = parse(argc, argv);
      setenv("DCP_GAME_ROOT", options.game.c_str(), 1);
      setenv("DCP_SAVE_ROOT", options.save.c_str(), 1);
      // Explicit checkpoint mode enables the runtime's Options/Cross script.
      setenv("DCP_GAME_MODE", "checkpoint", 1);
      if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
      SDL_Window *window = options.offscreen
                               ? nullptr
                               : SDL_CreateWindow("OpenCrown AOT host", 1280,
                                                  720, SDL_WINDOW_RESIZABLE);
      if (!options.offscreen && !window) {
        SDL_Quit();
        throw std::runtime_error(SDL_GetError());
      }
      Handoff handoff;
      DcpGameHostHooks hooks{};
      hooks.context = &handoff;
      hooks.will_unload = Handoff::unload;
      if (!dcp_game_configure(hooks)) {
        if (window)
          SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error(
            "A game session is already configured/running");
      }
      std::signal(SIGINT, interrupt);
      std::signal(SIGTERM, interrupt);
      auto started = Clock::now();
      auto worker = std::async(std::launch::async,
                               [] { return std::string(dcp_cpu_report()); });
      std::unique_ptr<dcp::mobile::Replay> replay;
      std::string hostError, guestText;
      uint64_t rendered = 0, consumed = 0, decoded = 0;
      uint64_t drawnFrames = 0, drawCalls = 0;
      bool nonuniformSeen = false;
      dcp::mobile::ReplayFrame last{};
      bool stopSent = false, timedOut = false, graceExpired = false,
           unloaded = false;
      Clock::time_point stopAt{};
      auto requestStop = [&] {
        if (!stopSent) {
          stopSent = true;
          stopAt = Clock::now();
          dcp_game_request_stop();
        }
      };
      auto pump = [&] {
        if (!replay)
          return false;
        dcp::mobile::ReplayFrame frame;
        std::string error;
        auto result = replay->pump(0, &frame, &error);
        if (result == dcp::mobile::PumpResult::Error) {
          if (hostError.empty())
            hostError = error;
          requestStop();
          return false;
        }
        if (result != dcp::mobile::PumpResult::Frame)
          return false;
        ++consumed;
        if (frame.width > 0 && frame.height > 0) {
          last = frame;
          ++rendered;
          if (frame.draws) {
            ++drawnFrames;
            drawCalls += frame.draws;
          }
          if (rendered == 1 ||
              (frame.draws && drawnFrames == 1) ||
              (options.every && rendered % options.every == 0)) {
            char filename[80];
            std::snprintf(filename, sizeof(filename), "frame-%06llu.png",
                          static_cast<unsigned long long>(rendered));
            bool varied = false;
            if (!replay->write_png((options.output / filename).string(),
                                   &error, &varied) &&
                hostError.empty()) {
              hostError = error;
              requestStop();
            }
            nonuniformSeen |= varied;
          }
          if (window) {
            int width = 0, height = 0;
            SDL_GetWindowSizeInPixels(window, &width, &height);
            if (!replay->present(width, height) && hostError.empty()) {
              hostError = "Metal presentation failed";
              requestStop();
            }
          }
        }
        return true;
      };
      for (;;) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
          if (event.type == SDL_EVENT_QUIT ||
              event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            requestStop();
        if (interrupted)
          requestStop();
        if (!stopSent &&
            std::chrono::duration<double>(Clock::now() - started).count() >=
                options.seconds) {
          timedOut = true;
          requestStop();
        }
        if (!unloaded && !replay && hostError.empty() &&
            dcp::render::capture_enabled()) {
          replay = std::make_unique<dcp::mobile::Replay>();
          if (!replay->initialize(window, false, &hostError)) {
            replay.reset();
            requestStop();
          }
        }
        if (!unloaded) {
          for (unsigned i = 0; i < 16 && pump(); ++i) {
          }
        }
        if (handoff.requested.load(std::memory_order_acquire) && !unloaded) {
          // Guest producers have joined, and the worker keeps all memory alive.
          // Consume remaining complete frames before releasing the GPU
          // consumer.
          while (pump()) {
          }
          if (replay) {
            if (rendered) {
              std::string error;
              bool varied = false;
              if (!replay->write_png((options.output / "frame-final.png").string(),
                                     &error, &varied) && hostError.empty())
                hostError = error;
              nonuniformSeen |= varied;
            }
            decoded = replay->decoded_uploads();
            if (!replay->suspend(true) && hostError.empty())
              hostError = "Metal GPU drain failed";
            replay.reset();
          }
          unloaded = true;
          handoff.acknowledge();
        }
        if (worker.wait_for(std::chrono::milliseconds(0)) ==
            std::future_status::ready) {
          try {
            guestText = worker.get();
          } catch (const std::exception &error) {
            hostError = error.what();
          }
          break;
        }
        if (stopSent && Clock::now() - stopAt > std::chrono::seconds(15)) {
          graceExpired = true;
          if (hostError.empty())
            hostError =
                "Guest did not stop within 15 seconds after cancellation";
          break;
        }
        SDL_Delay(1);
      }
      if (replay) {
        decoded = replay->decoded_uploads();
        replay->suspend(true);
        replay.reset();
      }
      if (window)
        SDL_DestroyWindow(window);
      SDL_Quit();
      NSData *guestData =
          [string(guestText) dataUsingEncoding:NSUTF8StringEncoding];
      id guest = guestData.length
                     ? [NSJSONSerialization JSONObjectWithData:guestData
                                                       options:0
                                                         error:nullptr]
                     : nil;
      if (guest && ![guest isKindOfClass:NSDictionary.class])
        guest = nil;
      if (!guest && hostError.empty())
        hostError = "Guest returned no valid JSON report";
      bool booted = guest && [guest[@"booted"] boolValue];
      bool guestError = guest &&
                        [guest[@"error"] isKindOfClass:NSString.class] &&
                        [guest[@"error"] length] > 0;
#ifdef DCP_HOST_SYNTHETIC_FIXTURE
      constexpr bool contentRequired = false;
#else
      constexpr bool contentRequired = true;
#endif
      bool success = booted && rendered > 0 && drawnFrames > 0 &&
                     (!contentRequired || nonuniformSeen) && hostError.empty() &&
                     !guestError && !timedOut && !graceExpired;
      NSDictionary *report = @{
        @"success" : @(success),
        @"guest" : guest ?: NSNull.null,
        @"host_error" : string(hostError),
        @"rendered_frames" : @(rendered),
        @"consumed_frames" : @(consumed),
        @"drawn_frames" : @(drawnFrames),
        @"draw_calls" : @(drawCalls),
        @"nonuniform_frame_seen" : @(nonuniformSeen),
        @"nonuniform_content_required" : @(contentRequired),
        @"width" : @(last.width),
        @"height" : @(last.height),
        @"last_draws" : @(last.draws),
        @"last_flip" : @(last.flip),
        @"decoded_bc_uploads" : @(decoded),
        @"timeout" : @(timedOut),
        @"stop_requested" : @(stopSent),
        @"stop_grace_expired" : @(graceExpired),
        @"unload_acknowledged" : @(unloaded),
        @"offscreen" : @(options.offscreen),
        @"elapsed_seconds" :
            @(std::chrono::duration<double>(Clock::now() - started).count())
      };
      NSError *jsonError = nil;
      NSData *json =
          [NSJSONSerialization dataWithJSONObject:report
                                          options:NSJSONWritingPrettyPrinted |
                                                  NSJSONWritingSortedKeys
                                            error:&jsonError];
      if (!json ||
          ![json writeToFile:string((options.output / "report.json").string())
                     options:NSDataWritingAtomic
                       error:&jsonError]) {
        std::fprintf(stderr, "Cannot write report: %s\n",
                     jsonError.localizedDescription.UTF8String);
        if (graceExpired)
          std::_Exit(2);
        return 2;
      }
      std::fwrite(json.bytes, 1, json.length, stdout);
      std::putchar('\n');
      std::fflush(stdout);
      // Avoid the std::async destructor waiting forever for a broken guest.
      // Process exit reclaims a timed-out engine; no guest memory is freed
      // early.
      if (graceExpired)
        std::_Exit(2);
      return success ? 0 : 1;
    } catch (const std::exception &error) {
      std::fprintf(stderr, "%s\n", error.what());
      return 2;
    }
  }
}
