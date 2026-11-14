#include "replay.h"
#include "metal-device.h"
#include "render/png.h"
#include <exception>

namespace dcp::mobile {
bool Replay::initialize(void *window, bool debug, std::string *error) {
  auto device = make_metal_device(window, debug, error);
  if (!device)
    return false;
  auto backend = std::make_unique<render::Backend>();
  auto *raw = device.get();
  if (!backend->init(std::move(device))) {
    if (error)
      *error = "Captured-frame backend initialization failed";
    return false;
  }
  backend_ = std::move(backend);
  device_ = raw;
  return true;
}
PumpResult Replay::pump(uint32_t timeout_ms, ReplayFrame *info,
                        std::string *error) {
  if (!backend_) {
    if (error)
      *error = "Metal replay has not been initialized";
    return PumpResult::Error;
  }
  auto frame = render::take_frame(timeout_ms);
  if (!frame)
    return PumpResult::Empty;
  try {
    backend_->draw(*frame);
    if (info) {
      info->vblank = frame->vblank;
      info->flip = frame->flip;
      info->draws = frame->draws.size();
      backend_->shown_texture(&info->width, &info->height);
    }
    return PumpResult::Frame;
  } catch (const std::exception &exception) {
    if (error)
      *error = exception.what();
    return PumpResult::Error;
  }
}
bool Replay::write_png(const std::string &path, std::string *error) {
  return write_png(path, error, nullptr);
}
bool Replay::write_png(const std::string &path, std::string *error,
                       bool *spatial_variation) {
  if (spatial_variation)
    *spatial_variation = false;
  if (!backend_) {
    if (error)
      *error = "Metal replay has not been initialized";
    return false;
  }
  int width = 0, height = 0;
  auto rgb = backend_->read_rgb(&width, &height);
  if (width <= 0 || height <= 0 ||
      rgb.size() != size_t(width) * size_t(height) * 3) {
    if (error)
      *error = "Captured frame has no readable display target";
    return false;
  }
  if (!render::write_png(path, width, height, rgb)) {
    if (error)
      *error = "Cannot write the captured frame PNG";
    return false;
  }
  if (spatial_variation)
    for (size_t i = 3; i < rgb.size(); i += 3)
      if (rgb[i] != rgb[0] || rgb[i + 1] != rgb[1] || rgb[i + 2] != rgb[2]) {
        *spatial_variation = true;
        break;
      }
  return true;
}
bool Replay::present(int width, int height) {
  return backend_ && backend_->present(width, height);
}
uint64_t Replay::decoded_uploads() const {
  return device_ ? metal_decoded_uploads(*device_) : 0;
}
bool Replay::suspend(bool value) {
  return device_ && set_metal_suspended(*device_, value);
}
} // namespace dcp::mobile
