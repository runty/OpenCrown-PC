// Synthetic producer solely for host consumer/unload tests. Never linked into
// the real runner.
#include "game/game.h"
#include "render/capture.h"
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <vector>
namespace dcp::hle {
uint64_t vblank_count_public() { return 321; }
} // namespace dcp::hle
namespace {
DcpGameHostHooks host{};
std::atomic<bool> stopping{false};
struct Memory {
  uint64_t image, size, mapping;
};
bool valid(void *context, uint64_t address, size_t bytes, bool) {
  auto &m = *static_cast<Memory *>(context);
  return (address >= m.image && bytes <= m.size &&
          address - m.image <= m.size - bytes) ||
         (address >= m.mapping && bytes <= 65536 &&
          address - m.mapping <= 65536 - bytes);
}
void observe(uint64_t offset, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0,
             uint64_t d = 0) {
  uint64_t arguments[6] = {a, b, c, d, 0, 0};
  if (!dcp::render::observe_aot_entry(offset, arguments))
    throw std::runtime_error("Synthetic capture observation failed");
}
} // namespace
extern "C" int dcp_game_configure(DcpGameHostHooks hooks) {
  host = hooks;
  return 1;
}
extern "C" int dcp_game_is_running() { return 1; }
extern "C" void dcp_game_request_stop() { stopping = true; }
extern "C" void dcp_game_set_paused(int) {}
extern "C" const char *dcp_cpu_report() {
  using namespace dcp::render;
  std::vector<uint8_t> image(0xec4000);
  std::memset(image.data() + 0x79f890, 0xff, 48 * 4);
  auto *mapping = static_cast<uint8_t *>(
      mmap(reinterpret_cast<void *>(0x220000000ULL), 65536,
           PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0));
  if (mapping == MAP_FAILED ||
      reinterpret_cast<uint64_t>(mapping) >= (1ULL << 40))
    throw std::runtime_error("Synthetic mapping failed");
  uint64_t base = reinterpret_cast<uint64_t>(mapping);
  Memory memory{reinterpret_cast<uint64_t>(image.data()), image.size(), base};
  if (!initialize_aot_capture_v2(image.data(), image.size(), {&memory, valid}))
    throw std::runtime_error("Synthetic capture initialization failed");
  const uint64_t program = base + 0x1000;
  std::memcpy(image.data() + 0xe97350 + 51 * 0x20, &program, 8);
  std::memcpy(image.data() + 0xe97ad0 + 24 * 0x30, &program, 8);
  uint32_t *resource = reinterpret_cast<uint32_t *>(mapping + 0x2000);
  resource[0] = uint32_t(base + 0x3000);
  resource[1] = uint32_t((base + 0x3000) >> 32);
  resource[2] = 16;
  const float color[] = {0.25, 0.5, 0.75, 1};
  std::memcpy(mapping + 0x3000, color, sizeof(color));
  uint32_t *target = reinterpret_cast<uint32_t *>(mapping + 0x2100);
  target[0] = uint32_t((base + 0x4000) >> 8);
  target[18] = 63 | (63u << 14);
  const uint64_t dcb = base + 0x100, cue = dcb + 0x2e8;
  observe(0x5650d0, cue, program);
  observe(0x565280, cue, program);
  observe(0x5646d0, cue, 1, 0, base + 0x2000);
  observe(0x638870, dcb, 0, base + 0x2100);
  observe(0x5a7260);
  auto *command = reinterpret_cast<uint32_t *>(mapping + 0x2200);
  on_draw(command, 7, 3, 0, false);
  on_marker(command + 1, 6);
  void *display[] = {mapping + 0x4000};
  set_display_buffers(display, 0, 1);
  on_flip(0);
  // Request release immediately: main must initialize/pump/drain while this
  // memory remains alive, then acknowledge before capture and backing release.
  if (host.will_unload)
    host.will_unload(host.context);
  shutdown_aot_capture();
  munmap(mapping, 65536);
  return stopping ? "{\"fixture\":true,\"booted\":false,\"error\":\"fixture "
                    "stopped\"}"
                  : "{\"fixture\":true,\"booted\":true,\"error\":\"\"}";
}
