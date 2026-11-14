#include "graphics.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <vector>
#include "imgui_impl_sdl3.h"
#ifdef __APPLE__
#include "imgui_impl_sdlgpu3.h"
#else
#include <SDL3/SDL_opengl.h>
#include "imgui_impl_opengl3.h"
#endif

namespace {
SDL_Window* window;
#ifdef __APPLE__
SDL_GPUDevice* gpu;
SDL_GPUSampler* sampler;
SDL_GPUTextureFormat format;
#else
SDL_GLContext context;
// The few GL calls made here, through SDL_GL_GetProcAddress (no -lGL link; ImGui's backend loads its own).
struct Gl {
    void(APIENTRY* GenTextures)(GLsizei, GLuint*);
    void(APIENTRY* DeleteTextures)(GLsizei, const GLuint*);
    void(APIENTRY* BindTexture)(GLenum, GLuint);
    void(APIENTRY* TexParameteri)(GLenum, GLenum, GLint);
    void(APIENTRY* PixelStorei)(GLenum, GLint);
    void(APIENTRY* TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void(APIENTRY* Viewport)(GLint, GLint, GLsizei, GLsizei);
    void(APIENTRY* ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void(APIENTRY* Clear)(GLbitfield);
    void(APIENTRY* ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);  // test screenshots only (optional)
} gl;

bool load_gl() {  // after the context is current
    bool ok = true;
    auto get = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(SDL_GL_GetProcAddress(name));
        ok = ok && fn;
    };
    get(gl.GenTextures, "glGenTextures"), get(gl.DeleteTextures, "glDeleteTextures");
    get(gl.BindTexture, "glBindTexture"), get(gl.TexParameteri, "glTexParameteri");
    get(gl.PixelStorei, "glPixelStorei"), get(gl.TexImage2D, "glTexImage2D"), get(gl.Viewport, "glViewport");
    get(gl.ClearColor, "glClearColor"), get(gl.Clear, "glClear");
    const bool need = ok;
    get(gl.ReadPixels, "glReadPixels");
    return need;
}


#endif

bool save_pixels(const std::string& path, int w, int h, void* pixels) {
    auto* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, pixels, w * 4);
    bool ok = s && SDL_SavePNG(s, path.c_str());
    if (s) SDL_DestroySurface(s);
    std::fprintf(stderr, "launcher: screenshot %s %s\n", path.c_str(), ok ? "saved" : SDL_GetError());
    return ok;
}
}

bool launcher_graphics_init(SDL_Window* win) {
    window = win;
#ifdef __APPLE__
    gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_METALLIB, true, "metal");
    if (!gpu || !SDL_ClaimWindowForGPUDevice(gpu, win)) return false;
    format = SDL_GetGPUSwapchainTextureFormat(gpu, win);
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = info.mag_filter = SDL_GPU_FILTER_LINEAR;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = info.address_mode_v = info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler = SDL_CreateGPUSampler(gpu, &info);
    std::fprintf(stderr, "launcher: SDL GPU %s, swapchain format %d\n", SDL_GetGPUDeviceDriver(gpu), int(format));
    return sampler != nullptr;
#else
    context = SDL_GL_CreateContext(win);
    if (!context || !SDL_GL_MakeCurrent(win, context) || !load_gl()) return false;
    SDL_GL_SetSwapInterval(1);
    return true;
#endif
}

bool launcher_gui_init() {
#ifdef __APPLE__
    ImGui_ImplSDLGPU3_InitInfo info{};
    info.Device = gpu; info.ColorTargetFormat = format; info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    return ImGui_ImplSDL3_InitForSDLGPU(window) && ImGui_ImplSDLGPU3_Init(&info);
#else
    return ImGui_ImplSDL3_InitForOpenGL(window, context) && ImGui_ImplOpenGL3_Init("#version 330");
#endif
}
void launcher_gui_new_frame() {
#ifdef __APPLE__
    ImGui_ImplSDLGPU3_NewFrame();
#else
    ImGui_ImplOpenGL3_NewFrame();
#endif
}

Tex load_texture(const std::string& path, SDL_Window* win) {
    Tex t;
    if (path.empty()) return t;
    SDL_Surface* png = SDL_LoadPNG(path.c_str());
    SDL_Surface* rgba = png ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_ABGR8888) : nullptr;
    if (png) SDL_DestroySurface(png);
    if (!rgba) return t;
    int w = rgba->w, h = rgba->h;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) { SDL_DestroySurface(rgba); return t; }
    std::vector<uint8_t> px(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        std::memcpy(px.data() + size_t(y) * w * 4, static_cast<const uint8_t*>(rgba->pixels) + size_t(y) * rgba->pitch, size_t(w) * 4);
    SDL_DestroySurface(rgba);
#ifdef __APPLE__
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D; info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER; info.width = w; info.height = h;
    info.layer_count_or_depth = info.num_levels = 1; info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    t.id = SDL_CreateGPUTexture(gpu, &info);
    SDL_GPUTransferBufferCreateInfo ti{}; ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; ti.size = Uint32(px.size());
    auto* transfer = SDL_CreateGPUTransferBuffer(gpu, &ti);
    auto* cmd = transfer && t.id ? SDL_AcquireGPUCommandBuffer(gpu) : nullptr;
    void* mapped = cmd ? SDL_MapGPUTransferBuffer(gpu, transfer, false) : nullptr;
    if (!mapped) {
        if (cmd) SDL_CancelGPUCommandBuffer(cmd);
        if (transfer) SDL_ReleaseGPUTransferBuffer(gpu, transfer);
        free_texture(t); return {};
    }
    std::memcpy(mapped, px.data(), px.size()); SDL_UnmapGPUTransferBuffer(gpu, transfer);
    auto* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src{}; src.transfer_buffer = transfer; src.pixels_per_row = w; src.rows_per_layer = h;
    SDL_GPUTextureRegion dst{}; dst.texture = t.id; dst.w = w; dst.h = h; dst.d = 1;
    SDL_UploadToGPUTexture(copy, &src, &dst, false); SDL_EndGPUCopyPass(copy);
    bool ok = SDL_SubmitGPUCommandBuffer(cmd); SDL_ReleaseGPUTransferBuffer(gpu, transfer);
    if (!ok) { free_texture(t); return {}; }
    t.binding = {t.id, sampler};
#else
    gl.GenTextures(1, &t.id); gl.BindTexture(GL_TEXTURE_2D, t.id);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
#endif
    t.w = w; t.h = h;
    if (win) {
        auto* icon = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, px.data(), w * 4);
        if (icon) { SDL_SetWindowIcon(win, icon); SDL_DestroySurface(icon); }
    }
    return t;
}
void free_texture(Tex& t) {
    if (t.id) {
#ifdef __APPLE__
        SDL_ReleaseGPUTexture(gpu, t.id);
#else
        gl.DeleteTextures(1, &t.id);
#endif
    }
    t = {};
}
ImTextureID tex_id(const Tex& t) {
#ifdef __APPLE__
    return ImTextureID(reinterpret_cast<intptr_t>(&t.binding));
#else
    return ImTextureID(intptr_t(t.id));
#endif
}

bool launcher_present(const std::string& shot) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
#ifdef __APPLE__
    auto* cmd = SDL_AcquireGPUCommandBuffer(gpu);
    if (!cmd) return false;
    SDL_GPUTexture* target = nullptr;
    Uint32 width = 0, height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &target, &width, &height)) {
        SDL_CancelGPUCommandBuffer(cmd); return false;
    }
    SDL_GPUTexture* swapchain = target;
    bool temporary = !target || !shot.empty();
    if (temporary) { // hidden selftests render real pixels to an offscreen target
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D; info.format = format;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = std::max(w, 1); info.height = std::max(h, 1);
        info.layer_count_or_depth = info.num_levels = 1; info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        target = SDL_CreateGPUTexture(gpu, &info);
        width = info.width; height = info.height;
        if (!target) { SDL_CancelGPUCommandBuffer(cmd); return false; }
    }
    Imgui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmd);
    SDL_GPUColorTargetInfo color{}; color.texture = target;
    color.clear_color = {0.09f, 0.09f, 0.11f, 1}; color.load_op = SDL_GPU_LOADOP_CLEAR; color.store_op = SDL_GPU_STOREOP_STORE;
    auto* pass = SDL_BeginGPURenderPass(cmd, &color, 1, nullptr);
    ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmd, pass); SDL_EndGPURenderPass(pass);
    if (temporary && swapchain) {
        // Metal drawables are framebuffer-only. Read back our own target and
        // render/blit it to the drawable rather than downloading a drawable.
        SDL_GPUBlitInfo blit{};
        blit.source.texture = target; blit.source.w = width; blit.source.h = height;
        blit.destination.texture = swapchain; blit.destination.w = width; blit.destination.h = height;
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE; blit.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(cmd, &blit);
    }
    bool ok = true;
    if (!shot.empty()) {
        SDL_GPUTransferBufferCreateInfo ti{}; ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; ti.size = width * height * 4;
        auto* transfer = SDL_CreateGPUTransferBuffer(gpu, &ti);
        if (transfer) {
            auto* copy = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUTextureRegion src{}; src.texture = target; src.w = width; src.h = height; src.d = 1;
            SDL_GPUTextureTransferInfo dst{}; dst.transfer_buffer = transfer; dst.pixels_per_row = width; dst.rows_per_layer = height;
            SDL_DownloadFromGPUTexture(copy, &src, &dst); SDL_EndGPUCopyPass(copy);
            auto* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
            ok = fence && SDL_WaitForGPUFences(gpu, true, &fence, 1);
            auto* mapped = ok ? static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(gpu, transfer, false)) : nullptr;
            if (mapped) {
                std::vector<uint8_t> px(mapped, mapped + ti.size);
                if (format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM || format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB)
                    for (size_t i = 0; i < px.size(); i += 4) std::swap(px[i], px[i + 2]);
                for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
                SDL_UnmapGPUTransferBuffer(gpu, transfer); ok = save_pixels(shot, width, height, px.data());
            } else ok = false;
            if (fence) SDL_ReleaseGPUFence(gpu, fence);
            SDL_ReleaseGPUTransferBuffer(gpu, transfer);
        } else { SDL_CancelGPUCommandBuffer(cmd); ok = false; }
    } else ok = SDL_SubmitGPUCommandBuffer(cmd);
    if (temporary) SDL_ReleaseGPUTexture(gpu, target);
    return ok;
#else
    gl.Viewport(0, 0, w, h); gl.ClearColor(0.09f, 0.09f, 0.11f, 1); gl.Clear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    bool ok = true;
    if (!shot.empty() && gl.ReadPixels && w > 0 && h > 0) {
        std::vector<uint8_t> px(size_t(w) * h * 4), out(px.size());
        gl.PixelStorei(GL_PACK_ALIGNMENT, 4); gl.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        for (int y = 0; y < h; ++y) std::memcpy(out.data() + size_t(y) * w * 4, px.data() + size_t(h - 1 - y) * w * 4, size_t(w) * 4);
        for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
        ok = save_pixels(shot, w, h, out.data());
    }
    SDL_GL_SwapWindow(window); return ok;
#endif
}
void launcher_graphics_shutdown() {
#ifdef __APPLE__
    SDL_WaitForGPUIdle(gpu); ImGui_ImplSDLGPU3_Shutdown();
    SDL_ReleaseGPUSampler(gpu, sampler); SDL_ReleaseWindowFromGPUDevice(gpu, window); SDL_DestroyGPUDevice(gpu);
#else
    ImGui_ImplOpenGL3_Shutdown(); SDL_GL_DestroyContext(context);
#endif
}
