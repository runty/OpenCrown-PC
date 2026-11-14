// SDL3 audio output for dcp_run (replaces the silent paced sink). Call before the game thread starts.
#pragma once

#include "hle/audio.h"
struct SDL_AudioStream;

// Takes ownership of a stream accepting f32 stereo 48 kHz. Also used by the
// device-independent mixing regression; normal callers use install_sdl_audio.
std::unique_ptr<dcp::hle::AudioSink> make_sdl_audio_sink(SDL_AudioStream* stream, int target_frames);

bool install_sdl_audio();
