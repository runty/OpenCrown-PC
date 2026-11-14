#pragma once
#include "../platform/platform.h"
#include <cstddef>
#include <cstdint>
#include <string>
struct DcpOfflineHooks {
  void *context = nullptr;
  void *(*pointer)(void *context, uint64_t address, size_t bytes,
                   bool writing) = nullptr;
  uint64_t (*vblank_count)(void *context) = nullptr;
  bool (*stopping)(void *context) = nullptr;
  std::string save_root;
  bool scripted_input = false;
  // Same frame/button format as the desktop runner. Empty keeps the built-in
  // boot script; a nonempty comment/blank script explicitly means idle.
  std::string input_script;
  // Optional portable host services. No UIKit/platform implementation is linked
  // by default. Callbacks run without state_lock; init requires quiescent
  // callers.
  void (*input_read)(void *context, unsigned slot,
                     DcpPlatformPad *pad) = nullptr;
  // Start on the first open port and stop after the last port closes.
  // A failed start may be transient (inactive app), so guest handles remain
  // valid.
  int (*audio_start)(void *context) = nullptr;
  void (*audio_stop)(void *context) = nullptr;
  // Float32 interleaved stereo at 48 kHz. Must respect timeout and return the
  // accepted frame count. A short write is dropped and paced, never
  // busy-retried. Audio callbacks must not reenter audio service dispatch.
  size_t (*audio_submit)(void *context, const float *stereo, size_t frames,
                         uint32_t timeout_ms) = nullptr;
  // Optional root-wide save transaction: true snapshots before writable Mount2;
  // false checkpoints after successful writes at Umount. False rejects the
  // mount or keeps an existing mount open for commit retry. No state_lock is
  // held. Must not reenter this adapter; guest file writers must be quiescent
  // at Umount.
  bool (*savedata_transaction)(void *context, bool begin) = nullptr;
  // Optional checkpoint driver gate. False releases scripted buttons/sticks
  // while keeping the pad connected. A real input producer takes precedence.
  bool (*scripted_input_active)(void *context) = nullptr;
  // Diagnostic input relative to the observed scene. Without a script the
  // settled window keeps its connected, neutral pad.
  std::string scene_input_script;
  uint64_t (*scene_elapsed_frames)(void *context) = nullptr;
};
// Call with no active guest workers. Keeps all saves under the supplied
// disposable root.
void dcp_offline_init(DcpOfflineHooks hooks);
bool dcp_offline_savedata_access(bool writing);
std::string dcp_offline_savedata_directory();
bool dcp_offline_service(const char *name, const uint64_t arguments[6],
                         uint64_t *result);
