#pragma once
#include <cstddef>
#include <cstdint>

// Callbacks validate complete guest ranges. Initialize while guest workers are
// idle.
struct DcpGnmMemory {
  void *context = nullptr;
  bool (*read)(void *context, uint64_t address, void *destination,
               size_t bytes) = nullptr;
  bool (*write)(void *context, uint64_t address, const void *source,
                size_t bytes) = nullptr;
};
struct DcpGnmStats {
  uint64_t submits, flips, label_writes, rejected_accesses;
};
void dcp_gnm_init(DcpGnmMemory memory);
DcpGnmStats dcp_gnm_stats();
// Returns false for names this adapter does not implement. Known calls return
// PS4 success.
bool dcp_gnm_service(const char *name, const uint64_t args[6],
                     uint64_t *result);
