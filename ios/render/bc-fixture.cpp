#include "bc-textures.h"
#include "metal-device.h"
#include <SDL3/SDL.h>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

int main() {
  using namespace dcp::render;
  assert(SDL_Init(SDL_INIT_VIDEO));
  unsigned checks = 0;
  for (bool forced : {false, true}) {
    std::string error;
    auto device = dcp::mobile::make_metal_device(nullptr, true, &error, forced);
    assert(device);
    const TexId target = device->create_target(8, 4);
    Vtx vertices[3]{};
    const float positions[3][2] = {{-1, -1}, {-1, 3}, {3, -1}};
    for (int i = 0; i < 3; ++i) {
      vertices[i].pos[0] = positions[i][0];
      vertices[i].pos[1] = positions[i][1];
      vertices[i].pos[3] = 1;
      vertices[i].uv[0] = (positions[i][0] + 1) / 2;
      vertices[i].uv[1] = (1 - positions[i][1]) / 2;
      for (float &c : vertices[i].col)
        c = 1;
    }
    Draw draw{};
    draw.target = target;
    draw.tw = 8;
    draw.th = 4;
    draw.v = vertices;
    draw.n = 3;
    draw.raw = true;
    draw.flags = 1;
    for (TexFormat format :
         {TexFormat::BC1, TexFormat::BC2, TexFormat::BC3, TexFormat::BC7}) {
      const size_t stride = format == TexFormat::BC1 ? 8 : 16;
      std::array<uint8_t, 33> storage{};
      uint8_t *blocks = storage.data() + 1; // decoder accepts unaligned input
      for (int block = 0; block < 2; ++block) {
        uint8_t *data = blocks + size_t(block) * stride;
        if (format == TexFormat::BC7) {
          data[0] = 0xc0;
          for (int i = 1; i < 8; ++i)
            data[i] = 0xff;
          data[8] = 1;
        } else {
          uint8_t *color = data + (format == TexFormat::BC1 ? 0 : 8);
          if (format == TexFormat::BC2)
            std::memset(data, 0xff, 8);
          if (format == TexFormat::BC3)
            data[0] = 0xff;
          color[0] = block ? 0x1f : 0;
          color[1] = block ? 0 : 0xf8;
        }
      }
      draw.tex[0] = device->upload(0, format, 8, 4, blocks, stride * 2, true);
      assert(draw.tex[0]);
      device->draw(draw);
      device->end_frame();
      std::vector<uint8_t> pixels;
      assert(device->read_rgba(target, 8, 4, &pixels));
      assert(pixels.size() == 8 * 4 * 4);
      for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 8; ++x) {
          int color[4] = {x < 4 ? 255 : 0, 0, x < 4 ? 0 : 255, 255};
          if (format == TexFormat::BC7)
            for (int &v : color)
              v = 255;
          for (int c = 0; c < 4; ++c) {
            assert(std::abs(int(pixels[(y * 8 + x) * 4 + c]) - color[c]) <= 1);
            ++checks;
          }
        }
      std::vector<uint8_t> edge;
      assert(dcp::mobile::decode_bc(format, 5, 3, blocks, stride * 2, &edge));
      assert(edge.size() == 5 * 3 * 4);
      for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x) {
          int color[4] = {x < 4 ? 255 : 0, 0, x < 4 ? 0 : 255, 255};
          if (format == TexFormat::BC7)
            for (int &v : color)
              v = 255;
          for (int c = 0; c < 4; ++c) {
            assert(edge[(y * 5 + x) * 4 + c] == color[c]);
            ++checks;
          }
        }
      assert(
          !dcp::mobile::decode_bc(format, 5, 3, blocks, stride * 2 - 1, &edge));
      device->destroy(draw.tex[0]);
    }
    if (forced)
      assert(dcp::mobile::metal_decoded_uploads(*device) == 4);
    std::printf(
        "BC fixture: forced=%d decoded_uploads=%llu\n", forced,
        (unsigned long long)dcp::mobile::metal_decoded_uploads(*device));
  }
  SDL_Quit();
  std::printf(
      "BC1/2/3/7 native/fallback/partial-block pixel checks: %u passed\n",
      checks);
}
