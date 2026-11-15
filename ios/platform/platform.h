#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Action values match ScePadData; axes are centred at 128. */
typedef struct DcpPlatformPad {
  uint32_t buttons;
  uint8_t connected, lx, ly, rx, ry, l2, r2, touch_count;
  uint16_t tx[2], ty[2];
  uint8_t tid[2];
} DcpPlatformPad;

typedef struct DcpPlatformHooks {
  void *context;
  /* Main-thread notification. Host must suspend/resume guest workers and GPU.
   */
  void (*set_active)(void *context, int active);
} DcpPlatformHooks;

typedef struct DcpPlatformAudioStats {
  uint64_t submitted_frames, rendered_frames, underrun_frames;
  uint64_t clipped_samples, flushed_frames;
  uint32_t queued_frames, target_frames, callback_frames;
  int running, paused;
  double device_sample_rate, device_buffer_seconds;
} DcpPlatformAudioStats;

/* Lifecycle and overlay operations must run on the main thread. */
int dcp_platform_initialize(DcpPlatformHooks hooks);
void dcp_platform_shutdown(void);
void dcp_platform_set_paused(int paused);
int dcp_platform_active(void);
/* UIView*, borrowed; overlay follows its bounds. Pass NULL to detach. */
void dcp_platform_attach_touch_overlay(void *view);
/* Optional custom frontend. Replaces the virtual slot-zero state. */
void dcp_platform_input_set_touch(const DcpPlatformPad *pad);
void dcp_platform_input_read(unsigned slot, DcpPlatformPad *pad);
/* Metadata query: never advances a virtual finger or pending interaction. */
int dcp_platform_input_connected(unsigned slot);
/* Guest-thread read-only feedback for direct screen pointing. Sequence 0
 * means no sample. Coordinates are centred 1920x1080 game pixels, y down. */
void dcp_platform_input_cursor(uint32_t sequence, float x, float y);

/* May run on guest threads; input is interleaved float stereo at 48 kHz.
 * Submit paces the producer. A short return means timeout, pause, or stop;
 * callers must handle it and must not spin while inactive. */
int dcp_platform_audio_start(void);
void dcp_platform_audio_stop(void);
size_t dcp_platform_audio_submit(const float *stereo, size_t frames,
                                 uint32_t timeout_ms);
void dcp_platform_audio_stats(DcpPlatformAudioStats *stats);
/* Output mode captured by the last source-node configuration; retained on
 * stop so final diagnostics describe the stream that was actually consumed.
 * Separate query preserves the existing audio-stats struct ABI. */
int dcp_platform_audio_muted(void);
void dcp_platform_copy_error(char *destination, size_t capacity);

#ifdef __cplusplus
}
#endif
