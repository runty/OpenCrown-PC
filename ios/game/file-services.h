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
};
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
