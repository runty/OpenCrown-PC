#include "../cpu/runtime.h"
#include "font-services.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <thread>

Memory *dispatch(State &, uint64_t, Memory *m) { return m; }
using namespace std::chrono_literals;
namespace {
unsigned checks = 0;
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
  ++checks;
}
struct Context {
  Memory memory;
  std::vector<uint8_t> bytes;
  unsigned escapes = 0;
  uint64_t rejected = 0;
  bool reject_max = false, unsafe_max = false, pause_max = false;
  std::atomic<bool> entered{false}, released{false}, completed{false};
  explicit Context(const std::vector<uint8_t> &font)
      : bytes(font.size() + 65536, 0xa5) {
    std::copy(font.begin(), font.end(), bytes.begin() + 65536);
    addRegion(memory, bytes.data(), bytes.size());
  }
  uint64_t address(size_t offset) {
    return reinterpret_cast<uint64_t>(bytes.data() + offset);
  }
  bool valid(uint64_t a, size_t n) {
    const uint64_t base = address(0);
    return a >= base && n <= bytes.size() && a - base <= bytes.size() - n;
  }
};
uint64_t service(const char *name, std::array<uint64_t, 6> args = {},
                 std::array<float, 2> fp = {}) {
  uint64_t result;
  if (!dcp_font_service(name, args.data(), fp.data(), &result))
    throw std::runtime_error("font service not handled");
  return result;
}
std::vector<uint8_t> slice(Context &c, size_t offset, size_t count) {
  return {c.bytes.begin() + offset, c.bytes.begin() + offset + count};
}
struct Images {
  std::vector<std::vector<uint8_t>> surfaces, metrics, outputs;
};
Images exercise(const std::vector<uint8_t> &font_bytes, bool copying,
                bool unsafe) {
  Context c(font_bytes);
  c.unsafe_max = unsafe;
  DcpFontHooks hooks;
  hooks.context = &c;
  hooks.pointer = [](void *p, uint64_t a, size_t n, bool) -> void * {
    auto &c = *static_cast<Context *>(p);
    ++c.escapes;
    if (!c.valid(a, n))
      return nullptr;
    return guestPointer(c.memory, a, n);
  };
  hooks.register_range = [](void *p, void *a, size_t n) {
    addRegion(static_cast<Context *>(p)->memory, static_cast<uint8_t *>(a), n);
  };
  if (copying) {
    hooks.validate_memory = [](void *p, uint64_t a, size_t n, bool) {
      auto &c = *static_cast<Context *>(p);
      if (c.rejected == a || !c.valid(a, n))
        return false;
      guestValidate(c.memory, a, n);
      return true;
    };
    hooks.read_memory = [](void *p, uint64_t a, void *out, size_t n) {
      auto &c = *static_cast<Context *>(p);
      if (c.rejected == a || !c.valid(a, n))
        return false;
      guestRead(c.memory, a, out, n);
      return true;
    };
    hooks.write_memory = [](void *p, uint64_t a, const void *in, size_t n) {
      auto &c = *static_cast<Context *>(p);
      if (c.rejected == a || !c.valid(a, n))
        return false;
      guestWrite(c.memory, a, in, n);
      return true;
    };
    hooks.max_memory = [](void *p, uint64_t a, const uint8_t *in, size_t n) {
      auto &c = *static_cast<Context *>(p);
      if (c.reject_max || !c.valid(a, n))
        return false;
      c.entered = true;
      if (c.pause_max)
        while (!c.released.load())
          std::this_thread::yield();
      if (c.unsafe_max) {
        auto *out = reinterpret_cast<uint8_t *>(a);
        for (size_t i = 0; i < n; ++i)
          out[i] = std::max(out[i], in[i]);
      } else
        guestMaxBytes(c.memory, a, in, n);
      c.completed = true;
      return true;
    };
  }
  dcp_font_init(hooks);
  check(service("sceFontMemoryInit", {c.address(0)}) == 0, "font memory init");
  check(std::all_of(c.bytes.begin(), c.bytes.begin() + 64,
                    [](auto b) { return b == 0; }),
        "font memory zeroing");
  check(service("sceFontCreateLibraryWithEdition", {0, 0, 0, c.address(128)}) ==
            0,
        "library token");
  check(service("sceFontCreateRendererWithEdition",
                {0, 0, 0, c.address(136)}) == 0,
        "renderer token");
  check(service("sceFontOpenFontMemory",
                {0, c.address(65536), font_bytes.size(), 0, c.address(144)}) ==
            0,
        "open font memory");
  uint64_t font;
  guestRead(c.memory, c.address(144), &font, 8);
  // FreeType and the fallback set must use the owned copy, not this guest blob.
  std::fill(c.bytes.begin() + 65536, c.bytes.end(), 0);
  check(service("sceFontOpenFontSet", {0, 0, 0, 0, c.address(152)}) == 0,
        "owned fallback font");
  check(service("sceFontSetScalePixel", {font}, {20, 20}) == 0, "scale");
  check(service("sceFontSetupRenderScalePixel", {font}, {24, 24}) == 0,
        "render scale");
  check(service("sceFontGetHorizontalLayout", {font, c.address(256)}) == 0,
        "horizontal layout");
  check(service("sceFontGetVerticalLayout", {font, c.address(272)}) == 0,
        "vertical layout");
  constexpr size_t surface = 512, bitmap = 1024, pitch = 96, width = 72,
                   height = 64;
  check(service("sceFontRenderSurfaceInit",
                {c.address(surface), c.address(bitmap), pitch, 1, width,
                 height}) == 0,
        "surface init");
  check(std::all_of(c.bytes.begin() + surface + 24,
                    c.bytes.begin() + surface + 128,
                    [](auto b) { return b == 0; }),
        "surface padding");
  Images images;
  images.metrics.push_back(slice(c, 256, 28));
  const std::array<std::array<float, 2>, 7> positions{{{2, 2},
                                                       {-13, -9},
                                                       {65, 52},
                                                       {2.4f, -3.6f},
                                                       {100, 100},
                                                       {-100, -100},
                                                       {2, 2}}};
  const std::array<uint32_t, 7> codes{
      {'A', 'g', 'W', 0x3a9, 'A', 'A', 0x10ffff}};
  for (size_t i = 0; i < positions.size(); ++i) {
    for (size_t j = 0; j < pitch * height; ++j)
      c.bytes[bitmap + j] = uint8_t((j * 13) % 127);
    std::fill(c.bytes.begin() + 160, c.bytes.begin() + 240, 0xa5);
    const auto before = slice(c, bitmap - 16, pitch * height + 32);
    const auto result = service(
        "sceFontRenderCharGlyphImage",
        {font, codes[i], c.address(surface), c.address(160), c.address(192)},
        positions[i]);
    if (!(i == 6 ? result != 0 : result == 0))
      throw std::runtime_error("glyph status case " + std::to_string(i) +
                               " result " + std::to_string(result));
    check(true, "glyph status");
    const auto after = slice(c, bitmap - 16, pitch * height + 32);
    check(std::equal(before.begin(), before.begin() + 16, after.begin()) &&
              std::equal(before.end() - 16, before.end(), after.end() - 16),
          "glyph guard overwrite");
    for (size_t row = 0; row < height; ++row)
      check(std::equal(before.begin() + 16 + row * pitch + width,
                       before.begin() + 16 + (row + 1) * pitch,
                       after.begin() + 16 + row * pitch + width),
            "glyph touched pitch padding");
    if (i == 0)
      check(after != before, "glyph fixture drew no pixels");
    if (i == 6)
      check(after == before &&
                std::all_of(c.bytes.begin() + 160, c.bytes.begin() + 240,
                            [](auto b) { return b == 0xa5; }),
            "missing glyph published output");
    images.surfaces.push_back(after);
    images.metrics.push_back(slice(c, 160, 32));
    images.outputs.push_back(slice(c, 192, 48));
  }
  // Unsupported bpp, null buffers and empty clip rectangles still yield
  // metrics in the original renderer, without changing any coverage bytes.
  for (unsigned mode = 0; mode < 3; ++mode) {
    check(service("sceFontRenderSurfaceInit",
                  {c.address(surface), mode == 1 ? 0 : c.address(bitmap), pitch,
                   mode == 0 ? 2u : 1u, mode == 2 ? 0 : width, height}) == 0,
          "non-pixel surface init");
    const auto before = slice(c, bitmap - 16, pitch * height + 32);
    check(
        service("sceFontRenderCharGlyphImage",
                {font, 'A', c.address(surface), c.address(160), c.address(192)},
                {2, 2}) == 0,
        "non-pixel glyph status");
    const auto after = slice(c, bitmap - 16, pitch * height + 32);
    check(before == after, "non-pixel glyph wrote coverage");
    images.surfaces.push_back(after);
    images.metrics.push_back(slice(c, 160, 32));
    images.outputs.push_back(slice(c, 192, 48));
  }
  check(service("sceFontRenderSurfaceInit",
                {c.address(surface), c.address(bitmap), pitch, 1, width,
                 height}) == 0,
        "restore glyph surface");
  if (copying) {
    const auto before = c.bytes;
    check(
        service("sceFontRenderCharGlyphImage",
                {font, 'A', c.address(surface), c.address(160), c.address(192)},
                {std::numeric_limits<float>::max(), 2}) != 0 &&
            c.bytes == before,
        "unrepresentable glyph coordinate accepted");
    c.rejected = c.address(160);
    bool rejected = false;
    try {
      service("sceFontRenderCharGlyphImage",
              {font, 'A', c.address(surface), c.address(160), c.address(192)},
              {2, 2});
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    check(rejected && c.bytes == before, "invalid metrics changed a surface");
    c.rejected = 0;
    c.reject_max = true;
    rejected = false;
    try {
      service("sceFontRenderCharGlyphImage",
              {font, 'A', c.address(surface), c.address(160), c.address(192)},
              {2, 2});
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    check(rejected && c.bytes == before, "rejected coverage changed output");
    c.reject_max = false;
    c.pause_max = true;
    c.entered = c.completed = false;
    auto pending = std::async(std::launch::async, [&] {
      return service(
          "sceFontRenderCharGlyphImage",
          {font, 'A', c.address(surface), c.address(160), c.address(192)},
          {2, 2});
    });
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!c.entered.load() && std::chrono::steady_clock::now() < deadline)
      std::this_thread::yield();
    const bool entered = c.entered.load();
    bool crossed = false;
    GuestAtomicMutex original;
    {
      std::lock_guard<GuestAtomicMutex> lock(original);
      c.released = true;
      const auto until = std::chrono::steady_clock::now() + 30ms;
      while (!(crossed = c.completed.load()) &&
             std::chrono::steady_clock::now() < until)
        std::this_thread::yield();
    }
    check(pending.get() == 0 && entered && !crossed,
          "native glyph output crossed original LOCK");
    check(c.escapes == 0, "checked font path escaped a guest pointer");
    const auto stats = dcp_font_memory_stats();
    check(stats.synchronized_copies && stats.read_bytes >= font_bytes.size() &&
              stats.write_bytes > 0 && stats.blend_bytes > 0 &&
              stats.glyph_calls > 0,
          "font diagnostics");
    hooks.max_memory = nullptr;
    rejected = false;
    try {
      dcp_font_init(hooks);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    check(rejected, "partial font callbacks accepted");
  }
  check(service("sceFontCloseFont", {font}) == 0, "close font");
  check(service("sceFontSetScalePixel", {font}, {20, 20}) != 0,
        "closed font retained");
  dcp_font_reset();
  return images;
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc < 2 || argc > 3)
      throw std::runtime_error(
          "Usage: font-memory-fixture FONT [--unsafe-max]");
    std::ifstream stream(argv[1], std::ios::binary);
    const std::vector<uint8_t> font{std::istreambuf_iterator<char>(stream), {}};
    check(!font.empty(), "font file missing");
    const auto reference = exercise(font, false, false);
    const auto checked = exercise(
        font, true, argc == 3 && !std::strcmp(argv[2], "--unsafe-max"));
    check(reference.surfaces == checked.surfaces &&
              reference.metrics == checked.metrics &&
              reference.outputs == checked.outputs,
          "checked glyph differs from original renderer");
    std::cout << "{\"checks\":" << checks
              << ",\"failed\":0,\"glyph_cases\":10,\"legacy_reference_exact\":"
                 "true,\"original_lock_exclusion\":true}\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
