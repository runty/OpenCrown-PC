#pragma once
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
};
// Call with no active guest workers. Keeps all saves under the supplied
// disposable root.
void dcp_offline_init(DcpOfflineHooks hooks);
bool dcp_offline_savedata_access(bool writing);
std::string dcp_offline_savedata_directory();
bool dcp_offline_service(const char *name, const uint64_t arguments[6],
                         uint64_t *result);
