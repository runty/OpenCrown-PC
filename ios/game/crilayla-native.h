#pragma once
#include "../cpu/runtime.h"
struct DcpCrilaylaStats {
  uint64_t fast, fallback, bytes, inplace;
};
// False means delegate to original AOT with no guest memory/State mutation.
bool dcp_try_native_crilayla(State &, uint64_t pc, Memory &);
void dcp_crilayla_reset();
DcpCrilaylaStats dcp_crilayla_stats();
