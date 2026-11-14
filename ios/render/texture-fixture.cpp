// Regression for guest texture writes outside the old 512-byte head hash.
#include "render/backend.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>

namespace dcp::hle {
uint64_t vblank_count_public() { return 0; }
} // namespace dcp::hle
namespace {
struct Memory {
  uint64_t image, size, texture, texture_size;
};
bool valid(void *context, uint64_t address, size_t bytes, bool) {
  auto &m = *static_cast<Memory *>(context);
  return (address >= m.image && bytes <= m.size &&
          address - m.image <= m.size - bytes) ||
         (address >= m.texture && bytes <= m.texture_size &&
          address - m.texture <= m.texture_size - bytes);
}
class CountDevice final : public dcp::render::Device {
public:
  explicit CountDevice(uint8_t *texture) : texture_(texture) {}
  const char *name() const override { return "texture-cache fixture"; }
  bool init() override { return true; }
  dcp::render::TexId create_target(int, int) override { return ++next_; }
  dcp::render::TexId upload(dcp::render::TexId reuse, dcp::render::TexFormat,
                            int, int, const void *, size_t, bool) override {
    ++uploads;
    return reuse ? reuse : ++next_;
  }
  void destroy(dcp::render::TexId) override {}
  void draw(const dcp::render::Draw &) override {
    if (!draws++)
      texture_[4096] = 99;
  }
  bool read_rgba(dcp::render::TexId, int, int,
                 std::vector<uint8_t> *) override {
    return false;
  }
  bool present(dcp::render::TexId, int, int, int, int, Overlay *) override {
    return false;
  }
  unsigned uploads = 0, draws = 0;

private:
  uint8_t *texture_;
  uint32_t next_ = 0;
};
} // namespace
int main() {
  using namespace dcp::render;
  std::vector<uint8_t> image(0xec4000);
  std::memset(image.data() + 0x79f890, 0xff, 48 * 4);
  auto *texture = static_cast<uint8_t *>(
      mmap(reinterpret_cast<void *>(0x210000000ULL), 16384,
           PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0));
  assert(texture != MAP_FAILED &&
         reinterpret_cast<uint64_t>(texture) < (1ULL << 40));
  const uint64_t base = reinterpret_cast<uint64_t>(texture);
  Memory memory{reinterpret_cast<uint64_t>(image.data()), image.size(), base,
                16384};
  assert(initialize_aot_capture(image.data(), image.size(), {&memory, valid}));
  auto counted = std::make_unique<CountDevice>(texture);
  auto *count = counted.get();
  {
    Backend backend;
    assert(backend.init(std::move(counted)));
    Frame frame;
    frame.display = 0x10000;
    for (unsigned i = 0; i < 2; ++i) {
      auto draw = std::make_unique<DrawRec>();
      draw->rt = frame.display;
      draw->rt_w = draw->rt_h = 64;
      draw->vs = 24;
      draw->ps = 51;
      draw->cls = Cls::Layer;
      draw->count = 3;
      draw->tex[0][0] = uint32_t(base >> 8);
      draw->tex[0][1] = uint32_t(base >> 40) | (10u << 20);
      draw->tex[0][2] = 63 | (63u << 14);
      draw->tex[0][3] = 0xfac | (8u << 20) | (9u << 28);
      draw->tex[0][4] = 63u << 13;
      frame.draws.push_back(std::move(draw));
    }
    backend.draw(frame);
    assert(count->draws == 2 && count->uploads == 2);
    backend.draw(frame);
    assert(count->draws == 4 && count->uploads == 2);
  }
  shutdown_aot_capture();
  munmap(texture, 16384);
  std::puts("AOT texture pass: byte 4096 mutation reuploads in same frame; "
            "unchanged content reuses texture");
}
