#include "bc-textures.h"
#include <algorithm>
#include <cstring>
#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include "third_party/bcdec.h"

namespace dcp::mobile {
bool decode_bc(render::TexFormat format, int width, int height,
               const void *data, size_t bytes, std::vector<uint8_t> *rgba) {
  if (!data || !rgba || width <= 0 || height <= 0 || width > 16384 ||
      height > 16384)
    return false;
  void (*decode)(const void *, void *, int) = nullptr;
  switch (format) {
  case render::TexFormat::BC1:
    decode = bcdec_bc1;
    break;
  case render::TexFormat::BC2:
    decode = bcdec_bc2;
    break;
  case render::TexFormat::BC3:
    decode = bcdec_bc3;
    break;
  case render::TexFormat::BC7:
    decode = bcdec_bc7;
    break;
  default:
    return false;
  }
  const size_t stride = format == render::TexFormat::BC1 ? 8 : 16;
  const size_t columns = (size_t(width) + 3) / 4,
               rows = (size_t(height) + 3) / 4;
  if (bytes < columns * rows * stride)
    return false;
  rgba->resize(size_t(width) * size_t(height) * 4);
  const auto *blocks = static_cast<const uint8_t *>(data);
  for (size_t y = 0; y < rows; ++y)
    for (size_t x = 0; x < columns; ++x) {
      // bcdec reads 16/32/64-bit values. Copy to aligned local storage first.
      alignas(8) uint8_t compressed[16]{};
      alignas(8) uint8_t pixels[64];
      std::memcpy(compressed, blocks + (y * columns + x) * stride, stride);
      decode(compressed, pixels, 16);
      const size_t copy_width = std::min<size_t>(4, size_t(width) - x * 4);
      const size_t copy_height = std::min<size_t>(4, size_t(height) - y * 4);
      for (size_t row = 0; row < copy_height; ++row)
        std::memcpy(rgba->data() + ((y * 4 + row) * size_t(width) + x * 4) * 4,
                    pixels + row * 16, copy_width * 4);
    }
  return true;
}
} // namespace dcp::mobile
