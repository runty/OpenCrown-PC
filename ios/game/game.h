#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DcpGameHostHooks {
  void *context;
  // Called after guest workers join, before guest memory/capture are released.
  // Stop the renderer consumer and drain all GPU work before returning.
  void (*will_unload)(void *context);
  // Called at a quiescent writable savedata mount/umount boundary.
  int (*save_transaction)(void *context, int begin);
} DcpGameHostHooks;
typedef struct DcpGameStats {
  uint64_t steps, vblanks, workers, submits, flips, label_writes;
  uint64_t libc_checks;
  int64_t libc_result;
  int running, paused, stopping, interactive;
  int entered_game, title_seen, checkpoint_complete;
  char current_scene[16];
  char first_error[256];
} DcpGameStats;
// Thread-safe copy. Always initializes output (idle libc_result=-1); returns 1
// while an engine is alive, 0 when idle or output is null. No borrowed
// pointers.
int dcp_game_snapshot(DcpGameStats *output);
int dcp_game_configure(DcpGameHostHooks hooks);
int dcp_game_is_running(void);
const char *dcp_cpu_report(void);
void dcp_game_set_paused(int paused);
void dcp_game_request_stop(void);
#ifdef __cplusplus
}
#endif
