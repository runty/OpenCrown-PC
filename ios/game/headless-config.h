#pragma once
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace dcp::game {
struct HeadlessConfig {
  std::string scene = "TITLE", after;
  uint64_t trace_budget = 100000000, vblank_budget = 7200;
  uint64_t settle_frames = 120;
};
inline uint64_t bounded_count(const char *text, uint64_t fallback,
                              uint64_t maximum, const char *name) {
  if (!text)
    return fallback;
  const std::string value(text);
  uint64_t count = 0;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), count);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
      !count || count > maximum)
    throw std::invalid_argument(std::string("Invalid ") + name);
  return count;
}
inline HeadlessConfig headless_config(const char *scene, const char *traces,
                                      const char *vblanks,
                                      const char *settle = nullptr,
                                      const char *after = nullptr) {
  HeadlessConfig result;
  auto component = [](const char *text, const char *variable) {
    std::string value(text);
    if (value.empty() || value.size() > 15 ||
        value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
      throw std::invalid_argument(std::string("Invalid ") + variable);
    return value;
  };
  if (scene)
    result.scene = component(scene, "DCP_CHECKPOINT_SCENE");
  if (after) {
    result.after = component(after, "DCP_CHECKPOINT_AFTER");
    if (result.after == result.scene)
      throw std::invalid_argument("DCP_CHECKPOINT_AFTER must differ from DCP_CHECKPOINT_SCENE");
  }
  result.trace_budget = bounded_count(traces, result.trace_budget, 10000000000,
                                      "DCP_MAX_TRACE_ENTRIES");
  result.vblank_budget = bounded_count(vblanks, result.vblank_budget, 216000,
                                       "DCP_MAX_VBLANKS");
  result.settle_frames = bounded_count(settle, result.settle_frames, 3600,
                                       "DCP_CHECKPOINT_SETTLE_FRAMES");
  if (result.settle_frames < 120)
    throw std::invalid_argument("Invalid DCP_CHECKPOINT_SETTLE_FRAMES (minimum 120)");
  return result;
}
} // namespace dcp::game
