#pragma once
#include "render/device.h"
namespace dcp::mobile {
bool decode_bc(render::TexFormat format, int width, int height,
               const void *blocks, size_t bytes, std::vector<uint8_t> *rgba);
}
