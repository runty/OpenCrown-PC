#include "libc-bulk.h"
#include "runtime.h"
#include <atomic>
#include <limits>

namespace {
std::atomic<uint64_t> copies{0}, moves{0}, fills{0}, bytes{0};
}
extern "C" Memory *dcp_libc_bulk(State &state, uint64_t kind, Memory *memory) {
  if (!memory || kind > 2)
    throw std::runtime_error("invalid bulk libc adapter invocation");
  const uint64_t destination = state.gpr.rdi.qword;
  const uint64_t source = state.gpr.rsi.qword;
  const uint64_t count = state.gpr.rdx.qword;
  const uint64_t stack = state.gpr.rsp.qword;
  if (count > std::numeric_limits<size_t>::max() || stack > UINT64_MAX - 8)
    throw std::runtime_error("bulk libc count or return stack overflow");
  // Validate the return slot before committing any write, then read its actual
  // value after the operation: destination may legally overwrite this slot.
  (void)guestPointer(*memory, stack, 8);
  if (kind == 2)
    guestFill(*memory, destination, static_cast<uint8_t>(source),
              size_t(count));
  else
    guestMove(*memory, destination, source, size_t(count));
  const uint64_t returnPC = read64(*memory, stack);
  state.gpr.rax.qword = destination;
  state.gpr.rip.qword = returnPC;
  state.gpr.rsp.qword = stack + 8;
  if (kind == 0)
    copies.fetch_add(1, std::memory_order_relaxed);
  else if (kind == 1)
    moves.fetch_add(1, std::memory_order_relaxed);
  else
    fills.fetch_add(1, std::memory_order_relaxed);
  bytes.fetch_add(count, std::memory_order_relaxed);
  return memory;
}
extern "C" void dcp_libc_bulk_reset() {
  copies.store(0, std::memory_order_relaxed);
  moves.store(0, std::memory_order_relaxed);
  fills.store(0, std::memory_order_relaxed);
  bytes.store(0, std::memory_order_relaxed);
}
extern "C" DcpLibcBulkStats dcp_libc_bulk_stats() {
  return {copies.load(std::memory_order_relaxed),
          moves.load(std::memory_order_relaxed),
          fills.load(std::memory_order_relaxed),
          bytes.load(std::memory_order_relaxed)};
}
