#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

// PS4/BSD cancellation join result (distinct from the ECANCELED error code).
inline constexpr uint64_t kDcpThreadCanceledResult = 1;

struct DcpThreadHooks {
  void *context = nullptr;
  uint64_t (*run)(void *context, uint64_t guest_pc,
                  uint64_t argument) = nullptr;
  bool (*stopping)(void *context) = nullptr;
  void *(*pointer)(void *context, uint64_t address, size_t bytes,
                   bool writing) = nullptr;
  void (*register_range)(void *context, void *address, size_t bytes) = nullptr;
  // Called outside service locks after cancellation/global stop changes.
  // Cleared after successful stop/join, before the old context may be freed.
  void (*cancellation_changed)(void *context) = nullptr;
};
// Initialize once before creating guest workers, after all previous workers
// have joined.
void dcp_thread_init(DcpThreadHooks hooks);
uint64_t
dcp_thread_current_handle(); // zero on a caller not created by this adapter
bool dcp_thread_stopping();  // includes global stop, callback stop and
                             // current-thread cancellation
// The service flag is process-lifetime. The worker flag is valid on its host
// thread until the original worker invocation and TLS cleanup return.
const std::atomic<bool> *dcp_thread_stop_flag();
const std::atomic<bool> *dcp_thread_current_cancel_flag();
bool dcp_thread_current_cancelled(); // only this guest worker's cancel request
bool dcp_thread_shutdown_requested(); // adapter/host shutdown, excluding per-worker cancel
bool dcp_thread_attributes(uint64_t handle, void *destination32);
const char *dcp_thread_error();
// Bounded cooperative stop. A false result requires retaining hooks/context and
// guest memory.
bool dcp_thread_stop_and_join(uint32_t timeout_ms);
// Does not handle Self, Attr or Mutex APIs owned by the caller.
bool dcp_thread_service(const char *name, const uint64_t arguments[6],
                        uint64_t *result);
