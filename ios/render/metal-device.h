#pragma once
#include "render/device.h"

namespace dcp::mobile {
// SDL video must already be initialized. Create/destroy and use on the render
// thread. window is an SDL_Window*, or nullptr for offscreen replay. Caller
// owns the window.
std::unique_ptr<render::Device> make_metal_device(void *window, bool debug,
                                                  std::string *error,
                                                  bool force_bc_decode = false);
// Stop producers first. Suspend drains pending work before the app enters
// background. While suspended, drawing/upload/present/readback are disabled.
// Existing textures survive.
uint64_t metal_decoded_uploads(const render::Device &device);
bool set_metal_suspended(render::Device &device, bool suspended);
} // namespace dcp::mobile
