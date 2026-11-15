#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

struct DcpFileHooks {
  void *context = nullptr;
  void *(*pointer)(void *context, uint64_t address, size_t bytes,
                   bool writing) = nullptr;
  void (*set_errno)(void *context, int32_t error) = nullptr;
  std::string game_root;
  std::string save_root;
  bool (*savedata_access)(void *context, bool writing) = nullptr;
  std::string (*savedata_directory)(void *context) = nullptr;
  // Supply all three for synchronized copies without escaping guest pointers.
  // Validation must not expose a mutable pointer. Copy callbacks own their
  // synchronization only for the copy; no guest lock is held across host I/O.
  // Read copies guest -> host; write publishes host -> guest. A false result
  // rejects access. Init/reset and mapping lifetimes retain their usual rules.
  bool (*validate_memory)(void *, uint64_t, size_t, bool writing) = nullptr;
  bool (*read_memory)(void *, uint64_t, void *, size_t) = nullptr;
  bool (*write_memory)(void *, uint64_t, const void *, size_t) = nullptr;
};
struct DcpFileMemoryStats {
  bool synchronized_copies = false;
  uint64_t read_bytes = 0, write_bytes = 0;
};
DcpFileMemoryStats dcp_file_memory_stats();
// Initialize/reset only after guest workers have joined. Throws on invalid host
// roots.
void dcp_file_init(DcpFileHooks hooks);
void dcp_file_reset();
// Diagnostic count only; use the quiescent callback for snapshot boundaries.
size_t dcp_file_savedata_writers();
// Acquires exclusive admission against every in-flight file service call, then
// rejects open writable savedata descriptors before running operation. The
// callback must not reenter file/offline services. Init/reset require idle
// users.
bool dcp_file_with_savedata_quiescent(bool (*operation)(void *), void *context);
bool dcp_file_service(const char *name, const uint64_t arguments[6],
                      uint64_t *result);
