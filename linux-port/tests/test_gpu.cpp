// Metal translation regressions using exact synthetic pixels through the real Device.
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include "render/device.h"

using namespace dcp::render;

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    auto* gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_METALLIB, true, "metal");
    if (!gpu) { std::fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    std::string error;
    auto device = make_gpu_device(gpu, nullptr, &error);
    if (!device) { std::fprintf(stderr, "%s\n", error.c_str()); SDL_DestroyGPUDevice(gpu); return 1; }
    Vtx vertices[3]{};
    const float positions[3][2] = {{-1, -1}, {-1, 3}, {3, -1}};
    for (int i = 0; i < 3; ++i) {
        vertices[i].pos[0] = positions[i][0];
        vertices[i].pos[1] = positions[i][1];
        vertices[i].pos[3] = 1;
        for (float& channel : vertices[i].col) channel = 1;
    }
    Draw draw{};
    draw.target = device->create_target(16, 16);
    draw.tw = draw.th = 16;
    draw.v = vertices;
    draw.n = 3;
    draw.raw = true;
    draw.flags = 4;
    draw.cst[0] = draw.cst[3] = 1;
    device->draw(draw);
    draw.scissor = true;
    draw.sc[2] = 16;
    draw.sc[3] = 8;
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
    int failures = 0;
    auto expect = [&](int x, int y, int r, int g, int b, int a) {
        const int expected[4] = {r, g, b, a};
        for (int c = 0; c < 4; ++c)
            if (std::abs(int(pixels[(y * 16 + x) * 4 + c]) - expected[c]) > 1) {
                std::fprintf(stderr, "pixel %d,%d channel %d differs\n", x, y, c);
                ++failures;
            }
    };
    if (!device->read_rgba(draw.target, 16, 16, &pixels) || pixels.size() != 16 * 16 * 4) return 1;
    expect(8, 2, 127, 0, 128, 255);
    expect(8, 13, 255, 0, 0, 255);

    const uint8_t color[4] = {64, 128, 192, 255}, mask[4] = {255, 128, 255, 128};
    draw.tex[0] = device->upload(0, TexFormat::RGBA8, 1, 1, color, sizeof color, true);
    draw.tex[1] = device->upload(0, TexFormat::RGBA8, 1, 1, mask, sizeof mask, true);
    draw.flags = 1 | 32;
    draw.scissor = false;
    draw.blend.on = false;
    device->draw(draw);
    device->end_frame();
    if (!device->read_rgba(draw.target, 16, 16, &pixels) || pixels.size() != 16 * 16 * 4) return 1;
    expect(8, 8, 64, 64, 192, 128);
    device.reset();
    SDL_DestroyGPUDevice(gpu);
    SDL_Quit();
    std::printf("Metal pixel tests: %d failures\n", failures);
    return failures ? 1 : 0;
}
