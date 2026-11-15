#pragma once
#include "render/backend.h"

namespace dcp::mobile {
struct ReplayFrame {
  uint64_t vblank = 0, flip = 0;
  size_t draws = 0;
  int width = 0, height = 0;
};
enum class PumpResult { Empty, Frame, Error };
// Own on the SDL render thread. Guest producers feed capture observers and GNM
// markers.
class Replay {
public:
  bool initialize(void *sdl_window, bool debug, std::string *error);
  PumpResult pump(uint32_t timeout_ms, ReplayFrame *frame, std::string *error);
  bool write_png(const std::string &path, std::string *error);
  bool write_png(const std::string &path, std::string *error,
                 bool *spatial_variation);
  bool present(int width, int height);
  bool suspend(bool value);
  uint64_t decoded_uploads() const;
  render::TextureStats texture_stats() const;

private:
  std::unique_ptr<render::Backend> backend_;
  render::Device *device_ = nullptr;
};
} // namespace dcp::mobile
