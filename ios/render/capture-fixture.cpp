// Synthetic AOT wrapper observations produce the same DrawRec/marker queue used
// by Metal replay.
#include "render/capture.h"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <thread>
#include <vector>

namespace dcp::hle {
uint64_t vblank_count_public() { return 123; }
} // namespace dcp::hle
namespace {
struct Region {
  uint64_t address, size;
};
struct Memory {
  std::vector<Region> regions;
};
bool valid(void *context, uint64_t address, size_t bytes, bool) {
  auto &memory = *static_cast<Memory *>(context);
  for (auto &region : memory.regions)
    if (address >= region.address && bytes <= region.size &&
        address - region.address <= region.size - bytes)
      return true;
  return false;
}
void observe(uint64_t offset, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0,
             uint64_t d = 0, uint64_t e = 0, uint64_t f = 0) {
  uint64_t arguments[6] = {a, b, c, d, e, f};
  assert(dcp::render::is_aot_entry(offset));
  assert(dcp::render::observe_aot_entry(offset, arguments));
}
} // namespace
int main() {
  using namespace dcp::render;
  std::vector<uint8_t> image(0xec4000);
  auto *mapping = static_cast<uint8_t *>(
      mmap(reinterpret_cast<void *>(0x200000000ULL), 0x10000,
           PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0));
  assert(mapping != MAP_FAILED &&
         reinterpret_cast<uint64_t>(mapping) < (1ULL << 40));
  Memory memory{{{reinterpret_cast<uint64_t>(image.data()), image.size()},
                 {reinterpret_cast<uint64_t>(mapping), 0x10000}}};
  assert(initialize_aot_capture_v2(image.data(), image.size(), {&memory, valid}));
  assert(!install_hooks(image.data()));
  const uint64_t base = reinterpret_cast<uint64_t>(mapping), dcb = base + 0x100,
                 cue = dcb + 0x2e8;
  const uint64_t vertex = base + 0x1000, program = base + 0x2000;
  std::memcpy(image.data() + 0xe97350, &program, 8);
  std::memcpy(image.data() + 0xe97ad0, &program, 8);
  auto *resource = reinterpret_cast<uint32_t *>(mapping + 0x3000);
  resource[0] = uint32_t(vertex);
  resource[1] = uint32_t(vertex >> 32) | (16u << 16);
  resource[2] = 3;
  resource[3] = (13u << 15) | (7u << 12);
  const float vertices[] = {-1, -1, 0, 1, -1, 3, 0, 1, 3, -1, 0, 1};
  std::memcpy(mapping + 0x1000, vertices, sizeof(vertices));
  auto *target = reinterpret_cast<uint32_t *>(mapping + 0x3100);
  target[0] = uint32_t((base + 0x4000) >> 8);
  target[18] = 63 | (63u << 14);
  observe(0x55fef0, dcb);
  observe(0x5650d0, cue, program);
  observe(0x565280, cue, program);
  observe(0x564b30, cue, 2, 0, 1, base + 0x3000);
  observe(0x638870, dcb, 0, base + 0x3100);
  observe(0x5a7260);
  auto *command = reinterpret_cast<uint32_t *>(mapping + 0x3200);
  on_draw(command, 7, 3, 0, false);
  assert(command[1] == kMarkerMagic);
  on_marker(command + 1, 6);
  void *display[] = {mapping + 0x4000};
  set_display_buffers(display, 0, 1);
  on_flip(0);
  auto frame = take_frame(0);
  assert(frame && frame->draws.size() == 1 && frame->vblank == 123 &&
         frame->display == base + 0x4000);
  auto &draw = *frame->draws[0];
  assert(draw.cls == Cls::Sprite && draw.vs == 0 && draw.ps == 0 &&
         draw.rt_w == 64 && draw.rt_h == 64 && draw.count == 3 &&
         draw.vb.size() == 1);
  assert(draw.vb[0].bytes.size() == sizeof(vertices) &&
         std::memcmp(draw.vb[0].bytes.data(), vertices, sizeof(vertices)) == 0);
  uint64_t none[6]{};
  assert(!is_aot_entry(0x123));
  assert(!is_aot_entry(0x1005650d0ULL)); // never truncate a non-image PC
  assert(!observe_aot_entry(0x123, none));
  on_draw(reinterpret_cast<uint32_t *>(1), 7, 3, 0,
          false); // rejected without touching unregistered memory
  shutdown_aot_capture();
  assert(!capture_enabled());
  assert(initialize_aot_capture_v2(image.data(), image.size(), {&memory, valid}));
  assert(!take_frame(0));
  shutdown_aot_capture();
  std::atomic<bool> finished{false};
  std::atomic<unsigned> polled{0};
  std::thread consumer([&] {
    while (!finished) {
      auto next = take_frame(0);
      sprite_ps_table(0);
      ++polled;
    }
  });
  for (unsigned i = 0; i < 1000; ++i) {
    assert(
        initialize_aot_capture_v2(image.data(), image.size(), {&memory, valid}));
    on_flip(0);
    shutdown_aot_capture();
  }
  finished = true;
  consumer.join();
  assert(polled > 0);
  assert(initialize_aot_capture_v2(image.data(), image.size(), {&memory, valid}));
  std::atomic<bool> waiting{false};
  std::thread sleeper([&] {
    waiting = true;
    auto next = take_frame(1000);
    assert(!next);
  });
  while (!waiting)
    std::this_thread::yield();
  shutdown_aot_capture();
  sleeper.join();
  munmap(mapping, 0x10000);
  std::puts("AOT capture pass: wrapper state, shader pools, vertex copy, "
            "marker order, display/flip, checked memory, reset");
}
