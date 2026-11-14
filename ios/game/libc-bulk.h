#pragma once
#include <stdint.h>
typedef struct DcpLibcBulkStats {
  uint64_t copy_calls;
  uint64_t move_calls;
  uint64_t fill_calls;
  uint64_t bytes;
} DcpLibcBulkStats;
#ifdef __cplusplus
struct State;
struct Memory;
// SysV arguments come from guest RDI/RSI/RDX. Kinds: 0 memcpy, 1 memmove,
// 2 memset. Only RAX, RIP and RSP change on a completed guest return.
extern "C" Memory *dcp_libc_bulk(State &, uint64_t kind, Memory *);
extern "C" {
#endif
// Reset only at a session boundary after all previous workers have stopped.
void dcp_libc_bulk_reset(void);
DcpLibcBulkStats dcp_libc_bulk_stats(void);
#ifdef __cplusplus
}
#endif
