#pragma once
#include <SDL3/SDL.h>
#include "imgui.h"
#include <string>
struct Tex {
#ifdef __APPLE__
    SDL_GPUTexture* id = nullptr;
    SDL_GPUTextureSamplerBinding binding{};
#else
    unsigned int id = 0;
#endif
    int w = 0, h = 0;
};
bool launcher_graphics_init(SDL_Window* window);
bool launcher_gui_init();
void launcher_gui_new_frame();
bool launcher_present(const std::string& screenshot);
void launcher_graphics_shutdown();
Tex load_texture(const std::string& path, SDL_Window* window = nullptr);
void free_texture(Tex& texture);
ImTextureID tex_id(const Tex& texture);
