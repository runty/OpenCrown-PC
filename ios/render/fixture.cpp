#include "metal-device.h"
#include "render/png.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr, "Usage: metal-fixture OUTPUT_PREFIX\n");
    return 2;
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "%s\n", SDL_GetError());
    return 1;
  }
  std::string error;
  auto device = dcp::mobile::make_metal_device(nullptr, true, &error);
  if (!device) {
    std::fprintf(stderr, "%s\n", error.c_str());
    SDL_Quit();
    return 1;
  }
  using namespace dcp::render;
  constexpr int width = 64, height = 64;
  Vtx vertices[3]{};
  const float positions[3][2] = {{-1, -1}, {-1, 3}, {3, -1}};
  for (int i = 0; i < 3; ++i) {
    vertices[i].pos[0] = positions[i][0];
    vertices[i].pos[1] = positions[i][1];
    vertices[i].pos[3] = 1;
    for (float &c : vertices[i].col)
      c = 1;
  }
  Draw draw{};
  draw.target = device->create_target(width, height);
  draw.tw = width;
  draw.th = height;
  draw.raw = true;
  draw.v = vertices;
  draw.n = 3;
  draw.flags = 4;
  draw.cst[0] = draw.cst[3] = 1;
  device->draw(draw);
  draw.scissor = true;
  draw.sc[2] = width;
  draw.sc[3] = height / 2;
  draw.blend.on = true;
  draw.blend.cs = 4;
  draw.blend.cd = 5;
  draw.blend.ad = 5;
  draw.cst[0] = 0;
  draw.cst[2] = 1;
  draw.cst[3] = 0.5f;
  device->draw(draw);
  device->end_frame();
  std::vector<uint8_t> pixels;
  if (!device->read_rgba(draw.target, width, height, &pixels))
    return 1;
  unsigned checked = 0, failed = 0;
  auto inspect = [&](const int expected[4], int x, int y) {
    for (int c = 0; c < 4; ++c) {
      ++checked;
      if (std::abs(int(pixels[(y * width + x) * 4 + c]) - expected[c]) > 1)
        ++failed;
    }
  };
  const int top[4] = {127, 0, 128, 255}, bottom[4] = {255, 0, 0, 255};
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      inspect(y < height / 2 ? top : bottom, x, y);
  std::vector<uint8_t> rgb;
  for (size_t i = 0; i < pixels.size(); i += 4)
    rgb.insert(rgb.end(), {pixels[i], pixels[i + 1], pixels[i + 2]});
  if (!write_png(std::string(argv[1]) + "-blend.png", width, height, rgb))
    return 1;
  bool paused = dcp::mobile::set_metal_suspended(*device, true);
  if (!paused || device->create_target(1, 1) != 0 ||
      device->read_rgba(draw.target, width, height, &pixels))
    ++failed;
  if (!dcp::mobile::set_metal_suspended(*device, false) ||
      !device->read_rgba(draw.target, width, height, &pixels))
    ++failed;
  const uint8_t color[4] = {64, 128, 192, 255}, mask[4] = {255, 128, 255, 128};
  draw.tex[0] =
      device->upload(0, TexFormat::RGBA8, 1, 1, color, sizeof(color), true);
  draw.tex[1] =
      device->upload(0, TexFormat::RGBA8, 1, 1, mask, sizeof(mask), true);
  draw.flags = 1 | 32;
  draw.scissor = false;
  draw.blend.on = false;
  auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 120; ++i) {
    device->begin_frame();
    device->draw(draw);
    device->end_frame();
  }
  if (!device->read_rgba(draw.target, width, height, &pixels))
    return 1;
  auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start)
                    .count();
  const int sampled[4] = {64, 64, 192, 128};
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      inspect(sampled, x, y);
  rgb.clear();
  for (size_t i = 0; i < pixels.size(); i += 4)
    rgb.insert(rgb.end(), {pixels[i], pixels[i + 1], pixels[i + 2]});
  if (!write_png(std::string(argv[1]) + "-mask.png", width, height, rgb))
    return 1;
  // Two identical translucent draws must blend in submission order.
  // A pair of incomplete triangle lists must never form a triangle together.
  draw.flags = 4;
  draw.cst[0] = 1;
  draw.cst[1] = draw.cst[2] = 0;
  draw.cst[3] = 1;
  device->draw(draw);
  draw.cst[0] = 0;
  draw.cst[2] = 1;
  draw.cst[3] = 0.5f;
  draw.blend.on = true;
  device->draw(draw);
  device->draw(draw);
  draw.blend.on = false;
  draw.cst[1] = 1;
  draw.n = 2;
  device->draw(draw);
  draw.n = 1;
  draw.v = vertices + 2;
  device->draw(draw);
  device->end_frame();
  if (!device->read_rgba(draw.target, width, height, &pixels))
    return 1;
  const int twiceBlended[4] = {63, 0, 192, 255};
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      inspect(twiceBlended, x, y);
  std::ofstream report(std::string(argv[1]) + ".json");
  report << "{\"pixel_channel_checks\":" << checked << ",\"failed\":" << failed
         << ",\"frames\":120,\"render_readback_us\":" << micros
         << ",\"width\":64,\"height\":64,\"suspend_resume\":true}\n";
  device.reset();
  SDL_Quit();
  return failed ? 1 : 0;
}
