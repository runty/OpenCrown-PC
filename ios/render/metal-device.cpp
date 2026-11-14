#include "metal-device.h"
#include "bc-textures.h"
#include <SDL3/SDL.h>

namespace dcp::mobile {
namespace {
class MetalDevice final : public render::Device {
public:
  MetalDevice(SDL_GPUDevice *gpu, SDL_Window *window,
              std::unique_ptr<render::Device> replay, bool force_bc_decode)
      : gpu_(gpu), window_(window), replay_(std::move(replay)),
        force_bc_decode_(force_bc_decode) {}
  ~MetalDevice() override {
    replay_.reset();
    if (window_)
      SDL_ReleaseWindowFromGPUDevice(gpu_, window_);
    SDL_DestroyGPUDevice(gpu_);
  }
  const char *name() const override { return "SDL Metal"; }
  bool init() override { return true; }
  render::TexId create_target(int w, int h) override {
    return suspended_ ? 0 : replay_->create_target(w, h);
  }
  render::TexId upload(render::TexId reuse, render::TexFormat format, int w,
                       int h, const void *data, size_t size,
                       bool clamp) override {
    if (suspended_)
      return 0;
    if (format != render::TexFormat::RGBA8) {
      SDL_GPUTextureFormat native = SDL_GPU_TEXTUREFORMAT_INVALID;
      switch (format) {
      case render::TexFormat::BC1:
        native = SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
        break;
      case render::TexFormat::BC2:
        native = SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
        break;
      case render::TexFormat::BC3:
        native = SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
        break;
      case render::TexFormat::BC7:
        native = SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM;
        break;
      default:
        break;
      }
      if (force_bc_decode_ ||
          !SDL_GPUTextureSupportsFormat(gpu_, native, SDL_GPU_TEXTURETYPE_2D,
                                        SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        std::vector<uint8_t> rgba;
        if (!decode_bc(format, w, h, data, size, &rgba))
          return 0;
        ++decoded_uploads_;
        return replay_->upload(reuse, render::TexFormat::RGBA8, w, h,
                               rgba.data(), rgba.size(), clamp);
      }
    }
    return replay_->upload(reuse, format, w, h, data, size, clamp);
  }
  void destroy(render::TexId id) override { replay_->destroy(id); }
  void begin_frame() override {
    if (!suspended_)
      replay_->begin_frame();
  }
  void draw(const render::Draw &draw) override {
    if (!suspended_)
      replay_->draw(draw);
  }
  void end_frame() override {
    if (!suspended_)
      replay_->end_frame();
  }
  bool read_rgba(render::TexId id, int w, int h,
                 std::vector<uint8_t> *out) override {
    return !suspended_ && replay_->read_rgba(id, w, h, out);
  }
  bool read_texture(render::TexId id, int w, int h,
                    std::vector<uint8_t> *out) override {
    return !suspended_ && replay_->read_texture(id, w, h, out);
  }
  void set_upscaler(bool enabled, float sharpness) override {
    replay_->set_upscaler(enabled, sharpness);
  }
  bool present(render::TexId id, int tw, int th, int w, int h,
               Overlay *overlay) override {
    return !suspended_ && replay_->present(id, tw, th, w, h, overlay);
  }
  uint64_t decoded_uploads() const { return decoded_uploads_; }
  bool suspend(bool value) {
    if (value && !suspended_) {
      replay_->end_frame();
      if (!SDL_WaitForGPUIdle(gpu_))
        return false;
    }
    suspended_ = value;
    return true;
  }

private:
  SDL_GPUDevice *gpu_;
  SDL_Window *window_;
  std::unique_ptr<render::Device> replay_;
  bool suspended_ = false;
  bool force_bc_decode_ = false;
  uint64_t decoded_uploads_ = 0;
};
} // namespace
std::unique_ptr<render::Device> make_metal_device(void *window, bool debug,
                                                  std::string *error,
                                                  bool force_bc_decode) {
  auto fail = [&](const char *message) -> std::unique_ptr<render::Device> {
    if (error)
      *error = message;
    return nullptr;
  };
  if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO))
    return fail("SDL video must be initialized before creating Metal replay");
  SDL_GPUDevice *gpu =
      SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_METALLIB, debug, "metal");
  if (!gpu)
    return fail(SDL_GetError());
  auto *sdl_window = static_cast<SDL_Window *>(window);
  if (sdl_window && !SDL_ClaimWindowForGPUDevice(gpu, sdl_window)) {
    std::string message = SDL_GetError();
    SDL_DestroyGPUDevice(gpu);
    return fail(message.c_str());
  }
  std::string message;
  auto replay = render::make_gpu_device(gpu, sdl_window, &message);
  if (!replay) {
    if (sdl_window)
      SDL_ReleaseWindowFromGPUDevice(gpu, sdl_window);
    SDL_DestroyGPUDevice(gpu);
    return fail(message.c_str());
  }
  return std::make_unique<MetalDevice>(gpu, sdl_window, std::move(replay),
                                       force_bc_decode);
}
uint64_t metal_decoded_uploads(const render::Device &device) {
  auto *metal = dynamic_cast<const MetalDevice *>(&device);
  return metal ? metal->decoded_uploads() : 0;
}
bool set_metal_suspended(render::Device &device, bool suspended) {
  auto *metal = dynamic_cast<MetalDevice *>(&device);
  return metal && metal->suspend(suspended);
}
} // namespace dcp::mobile
