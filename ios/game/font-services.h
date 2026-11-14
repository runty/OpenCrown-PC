#pragma once
#include <cstddef>
#include <cstdint>

struct DcpFontHooks {
  void *context = nullptr;
  void *(*pointer)(void *context, uint64_t address, size_t bytes,
                   bool writing) = nullptr;
  void (*register_range)(void *context, void *address, size_t bytes) = nullptr;
};
// Initialize/reset while all guest workers are idle. Font blobs are copied and
// owned until reset, so FreeType never references released guest backing
// storage.
void dcp_font_init(DcpFontHooks hooks);
void dcp_font_reset();
// fp[0] and fp[1] are XMM0/XMM1 scalar float values. In SysV, those parameters
// do not consume integer argument registers (notably RenderCharGlyphImage).
bool dcp_font_service(const char *name, const uint64_t args[6],
                      const float fp[2], uint64_t *result);
