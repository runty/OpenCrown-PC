// A translated worker may rewrite a texture between the synchronized copy and
// hashing/detiling. Replay must use only its owned snapshot for both
// operations.
#include "render/backend.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace dcp::hle {
uint64_t vblank_count_public() { return 0; }
} // namespace dcp::hle
namespace {
struct Memory {
  uint64_t image, image_size, texture, texture_size;
  std::vector<uint8_t> texture_bytes = std::vector<uint8_t>(16384);
  std::mutex lock;
  std::atomic<uint8_t> expected{0};
  std::atomic<unsigned> snapshots{0};
  bool mutateAfterCopy = true;
};
bool valid(void *context, uint64_t address, size_t bytes, bool) {
  auto &m = *static_cast<Memory *>(context);
  return (address >= m.image && bytes <= m.image_size &&
          address - m.image <= m.image_size - bytes) ||
         (address >= m.texture && bytes <= m.texture_size &&
          address - m.texture <= m.texture_size - bytes);
}
bool copy(void *context, uint64_t address, void *destination, size_t bytes) {
  auto &m = *static_cast<Memory *>(context);
  std::lock_guard<std::mutex> guard(m.lock);
  if (!valid(context, address, bytes, false))
    return false;
  const void *source = address >= m.texture && bytes <= m.texture_size &&
                               address - m.texture <= m.texture_size - bytes
                           ? static_cast<const void *>(m.texture_bytes.data() +
                                                       address - m.texture)
                           : reinterpret_cast<const void *>(address);
  std::memcpy(destination, source, bytes);
  if (address == m.texture && bytes == m.texture_size) {
    const auto expected = *static_cast<uint8_t *>(destination);
    m.expected = expected;
    ++m.snapshots;
    // Deliberately change the source before returning. A later raw detile
    // would deterministically disagree with the hash's captured payload.
    if (m.mutateAfterCopy) std::memset(m.texture_bytes.data(), expected ^ 0x7f, bytes);
  }
  return true;
}
bool matches(void *context, uint64_t address, const void *snapshot, size_t bytes) {
  auto &m = *static_cast<Memory *>(context);
  std::lock_guard<std::mutex> guard(m.lock);
  return address == m.texture && bytes == m.texture_size &&
         std::memcmp(snapshot, m.texture_bytes.data(), bytes) == 0;
}
class CountDevice final : public dcp::render::Device {
public:
  explicit CountDevice(Memory &memory) : memory_(memory) {}
  const char *name() const override { return "synchronized texture fixture"; }
  bool init() override { return true; }
  dcp::render::TexId create_target(int, int) override { return ++next_; }
  dcp::render::TexId upload(dcp::render::TexId reuse,
                            dcp::render::TexFormat format, int width,
                            int height, const void *data, size_t bytes,
                            bool) override {
    assert(format == dcp::render::TexFormat::RGBA8 && width == 64 &&
           height == 64 && bytes == 16384);
    const auto expected = memory_.expected.load();
    const auto *input = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < bytes; ++i)
      assert(input[i] == expected);
    checked += bytes;
    ++uploads;
    return reuse ? reuse : ++next_;
  }
  void destroy(dcp::render::TexId) override {}
  void draw(const dcp::render::Draw &) override { ++draws; }
  bool read_rgba(dcp::render::TexId, int, int,
                 std::vector<uint8_t> *) override {
    return false;
  }
  bool present(dcp::render::TexId, int, int, int, int, Overlay *) override {
    return false;
  }
  unsigned uploads = 0, draws = 0;
  uint64_t checked = 0;

private:
  Memory &memory_;
  uint32_t next_ = 0;
};
} // namespace
int main() {
  using namespace dcp::render;
  std::vector<uint8_t> image(0xec4000);
  std::memset(image.data() + 0x79f890, 0xff, 48 * 4);
  // The guest texture address is deliberately not a native pointer. Any raw
  // backend read bypassing the synchronized callback would be invalid.
  const uint64_t base = 0x230000000ULL;
  Memory memory{reinterpret_cast<uint64_t>(image.data()), image.size(), base,
                16384};
  assert(initialize_aot_capture(image.data(), image.size(),
                                {&memory, valid, copy}));
  uint8_t probe = 0;
  assert(!aot_capture_copy(UINT64_MAX, &probe, 1));
  assert(!aot_capture_copy(base, nullptr, 1));
  assert(!aot_capture_copy(base, &probe, (64ULL << 20) + 1));
  auto counted = std::make_unique<CountDevice>(memory);
  auto *count = counted.get();
  unsigned uploads = 0, snapshots = 0;
  uint64_t checked = 0;
  {
    Backend backend;
    assert(backend.init(std::move(counted)));
    Frame frame;
    frame.display = 0x10000;
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
    // Many UI glyphs use the same atlas. The callback deliberately mutates
    // guest bytes after every snapshot: all repeated draws must still share
    // the first snapshot of this frame, with a fresh check next frame.
    for (unsigned i = 0; i < 128; ++i)
      frame.draws.push_back(std::make_unique<DrawRec>(*draw));
    std::atomic<bool> stop{false};
    std::atomic<unsigned> writes{0};
    std::thread writer([&] {
      while (!stop.load()) {
        {
          std::lock_guard<std::mutex> guard(memory.lock);
          std::memset(memory.texture_bytes.data(),
                      (writes.load() & 1) ? 0x33 : 0x77, 16384);
          ++writes;
        }
        std::this_thread::yield();
      }
    });
    for (unsigned i = 0; i < 200; ++i)
      backend.draw(frame);
    stop = true;
    writer.join();
    uploads = count->uploads;
    snapshots = memory.snapshots.load();
    checked = count->checked;
    assert(count->draws == 200 * 128 && snapshots == 200 && uploads > 0 &&
           writes > 0 && checked >= 16384);
    shutdown_aot_capture();
    memory.mutateAfterCopy = false;
    assert(initialize_aot_capture(image.data(), image.size(), {&memory, valid, copy, matches}));
    std::fill(memory.texture_bytes.begin(), memory.texture_bytes.end(), 0x41);
    backend.draw(frame);
    const auto previousCopies = memory.snapshots.load();
    const auto previousUploads = count->uploads;
    backend.draw(frame);
    assert(memory.snapshots == previousCopies && count->uploads == previousUploads);
    // Check the final byte of the atlas as well as its head.
    memory.texture_bytes.back() ^= 1;
    std::vector<uint8_t> original(memory.texture_size, 0x41);
    assert(!aot_capture_matches(base, original.data(), original.size()));
    std::fill(memory.texture_bytes.begin(), memory.texture_bytes.end(), 0x52);
    backend.draw(frame);
    assert(memory.snapshots == previousCopies + 1 && count->uploads == previousUploads + 1);
  }
  shutdown_aot_capture();
  std::printf("{\"frames\":200,\"draws_per_frame\":128,\"snapshots\":%u,\"uploads\":%u,\"checked_"
              "bytes\":%llu,\"failed\":0}\n",
              snapshots, uploads, static_cast<unsigned long long>(checked));
}
