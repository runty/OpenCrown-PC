// Exercise the actual runner sink against SDL streams and a paced dummy device.
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <memory>
#include <vector>
#include "../tools/dcp_run/sdl_audio.h"

namespace dcp::hle {
std::unique_ptr<AudioSink> installed;
void set_audio_sink(std::unique_ptr<AudioSink> s) { installed = std::move(s); }
}
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #x, SDL_GetError()); return 1; } } while (0)
int main() {
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
    CHECK(SDL_Init(SDL_INIT_AUDIO));
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
    std::string capture = "sdl-audio-capture-" + std::to_string(SDL_GetPerformanceCounter()) + ".wav";
    CHECK(!std::filesystem::exists(capture));
    CHECK(setenv("DCP_AUDIO_CAPTURE", capture.c_str(), 1) == 0);
    SDL_AudioStream* stream = SDL_CreateAudioStream(&spec, &spec);
    CHECK(stream);
    auto sink = make_sdl_audio_sink(stream, 4096);
    CHECK(unsetenv("DCP_AUDIO_CAPTURE") == 0); // the sink owns its path
    sink->open(0, 1, 48000); sink->open(1, 2, 48000); sink->open(2, 8, 48000);
    constexpr int frames = 256;
    std::vector<float> mono(frames, .1f), stereo(frames * 2), surround(frames * 8);
    const float eight[8] = {.1f, -.1f, .1f, 1.f, .2f, .3f, .1f, .2f};
    for (int i = 0; i < frames; ++i) {
        stereo[i * 2] = .2f; stereo[i * 2 + 1] = -.3f;
        for (int c = 0; c < 8; ++c) surround[i * 8 + c] = eight[c];
    }
    int ports[] = {0, 1, 2};
    const float* data[] = {mono.data(), stereo.data(), surround.data()};
    sink->submit(3, ports, data, frames);
    CHECK(SDL_FlushAudioStream(stream));
    float out[frames * 2];
    CHECK(SDL_GetAudioStreamData(stream, out, sizeof out) == sizeof out);
    for (int i = 0; i < frames; ++i) {
        CHECK(std::fabs(out[i * 2] - .6621f) < .0001f);
        CHECK(std::fabs(out[i * 2 + 1] - .0828f) < .0001f);
    }
    // Null ports are silence; out-of-range mixes are clamped, not wrapped.
    for (float& x : mono) x = 2.f;
    const float* clip[] = {mono.data(), nullptr, nullptr};
    sink->submit(3, ports, clip, frames); CHECK(SDL_FlushAudioStream(stream));
    CHECK(SDL_GetAudioStreamData(stream, out, sizeof out) == sizeof out);
    for (float x : out) CHECK(x == 1.f);
    const float* silence[] = {nullptr};
    sink->submit(1, ports, silence, frames); CHECK(SDL_FlushAudioStream(stream));
    CHECK(SDL_GetAudioStreamData(stream, out, sizeof out) == sizeof out);
    for (float x : out) CHECK(x == 0.f);
    // Fill the 30-second diagnostic cap without a playback device or wall-clock
    // wait. Excess input must not grow the capture past its bounded allocation.
    for (int i = 0; i < 5630; ++i) {
        sink->submit(1, ports, silence, frames); CHECK(SDL_FlushAudioStream(stream));
        CHECK(SDL_GetAudioStreamData(stream, out, sizeof out) == sizeof out);
    }
    sink.reset(); // owns and destroys the stream
    CHECK(std::filesystem::file_size(capture) == 44 + 30 * 48000 * 8);
    std::ifstream wav(capture, std::ios::binary); char header[44]; wav.read(header, sizeof header);
    CHECK(wav.gcount() == sizeof header && std::string(header, 4) == "RIFF" && header[20] == 3 && header[22] == 2);
    float sample[2]; wav.read(reinterpret_cast<char*>(sample), sizeof sample);
    CHECK(std::fabs(sample[0] - .6621f) < .0001f && std::fabs(sample[1] - .0828f) < .0001f);
    wav.close(); CHECK(std::filesystem::remove(capture));

    // The Linux preference must not override an explicit driver; verify real
    // queue-driven pacing through a device without producing audible test tones.
    CHECK(install_sdl_audio());
    CHECK(SDL_strcmp(SDL_GetCurrentAudioDriver(), "dummy") == 0);
#ifdef __APPLE__
    int devices = 0, period = 0;
    auto* ids = SDL_GetAudioPlaybackDevices(&devices);
    CHECK(ids && devices == 1);
    CHECK(SDL_GetAudioDeviceFormat(ids[0], &spec, &period));
    SDL_free(ids);
    CHECK(period == 256); // the Apple default must match the guest producer period
#endif
    auto& live = dcp::hle::installed;
    live->open(0, 1, 48000);
    uint64_t start = SDL_GetTicksNS();
    for (int i = 0; i < 300; ++i) live->submit(1, ports, silence, frames);
    double elapsed = (SDL_GetTicksNS() - start) / 1e9;
    CHECK(elapsed > 1.0 && elapsed < 4.0);
    live.reset(); SDL_Quit();
    std::printf("SDL audio: mono/stereo/8ch, clipping, silence, bounded capture, driver override and pacing passed (%.3fs)\n", elapsed);
}
