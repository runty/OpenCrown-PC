#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

struct DcpImeHooks {
  void *context = nullptr;
  void *(*pointer)(void *, uint64_t, size_t, bool) = nullptr;
  bool host_present = false;
};
struct DcpImeRequest {
  uint64_t id = 0;
  std::u16string title, text;
  uint32_t max_units = 0;
};
// Init requires quiescent guest callers. Host callbacks never access guest
// memory: completion is copied into the guest on its next status/result poll.
void dcp_ime_init(DcpImeHooks hooks);
bool dcp_ime_service(const char *, const uint64_t arguments[6], uint64_t *result);
bool dcp_ime_pending(DcpImeRequest *request);
// nullptr cancels and preserves the original buffer. Stale ids are ignored.
bool dcp_ime_finish(uint64_t id, const std::u16string *text);
