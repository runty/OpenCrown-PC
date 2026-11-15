#include "font-services.h"
#include <array>
#include <atomic>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>

// Reuse the existing implementation and layout verbatim. The mobile adapter
// owns dispatch, checked addresses and lifetimes, so source
// registration/logging is inert.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-attributes"
#include "../../linux-port/src/hle/common.h"
#undef DCP_HLE_LOG
#define DCP_HLE_LOG(...) ((void)0)
#undef REG
#define REG(library, function) ((void)(library), (void)(&function))
#include "../../linux-port/src/hle/font.cpp"
#pragma clang diagnostic pop

namespace {
using dcp::hle::Font;
using dcp::hle::Surface;
constexpr uint32_t invalid = 0x80460002;
DcpFontHooks hooks;
std::mutex adapter_mutex;
std::map<uint64_t, Font *> fonts;
std::vector<std::unique_ptr<Font>> retired;
std::vector<std::unique_ptr<uint64_t>> tokens;
std::vector<std::shared_ptr<std::vector<uint8_t>>> blobs;
std::atomic<bool> synchronized_copies{false};
std::atomic<uint64_t> read_bytes{0}, write_bytes{0}, blend_bytes{0},
    glyph_calls{0};

void *pointer(uint64_t address, size_t size, bool writing) {
  if (!address || !hooks.pointer)
    throw std::runtime_error("font service has no valid guest range");
  void *result = hooks.pointer(hooks.context, address, size, writing);
  if (!result)
    throw std::runtime_error("font service rejected guest range");
  return result;
}
void register_range(void *address, size_t size) {
  if (hooks.register_range)
    hooks.register_range(hooks.context, address, size);
}
void validate(uint64_t address, size_t size, bool writing) {
  if (!address ||
      (hooks.validate_memory
           ? !hooks.validate_memory(hooks.context, address, size, writing)
           : !pointer(address, size, writing)))
    throw std::runtime_error("font service rejected guest range");
}
void read_memory(uint64_t address, void *output, size_t size) {
  if (hooks.read_memory) {
    if (!address || !hooks.read_memory(hooks.context, address, output, size))
      throw std::runtime_error("font service rejected guest read");
    read_bytes.fetch_add(size, std::memory_order_relaxed);
  } else
    std::memcpy(output, pointer(address, size, false), size);
}
void write_memory(uint64_t address, const void *input, size_t size) {
  if (hooks.write_memory) {
    if (!address || !hooks.write_memory(hooks.context, address, input, size))
      throw std::runtime_error("font service rejected guest write");
    write_bytes.fetch_add(size, std::memory_order_relaxed);
  } else
    std::memcpy(pointer(address, size, true), input, size);
}
// Match the original font.cpp coverage rule, but publish only clipped bitmap
// rows through a checked read/modify/write. FreeType never receives guest
// bytes.
uint32_t render_checked(Font *f, uint32_t code, const Surface &surface,
                        const float fp[2], uint64_t metrics, uint64_t output) {
  if (surface.buf)
    validate(reinterpret_cast<uint64_t>(surface.buf),
             size_t(uint64_t(surface.pitch) * uint32_t(surface.h)), true);
  if (metrics)
    validate(metrics, 32, true);
  if (output)
    validate(output, 0x30, true);
  std::array<float, 8> measured{};
  std::array<uint8_t, 0x30> rendered{};
  Surface privateSurface = surface;
  privateSurface.buf = nullptr;
  const auto status = uint32_t(dcp::hle::sceFontRenderCharGlyphImage(
      f, code, &privateSurface, fp[0], fp[1], measured.data(),
      rendered.data()));
  if (status)
    return status;
  // adapter_mutex excludes all other uses/reset of this face and its bitmap.
  const auto &bitmap = f->face->glyph->bitmap;
  if (surface.buf && surface.bpp == 1 &&
      bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
    float layout[3];
    dcp::hle::hlayout(f, f->ry > 0 ? f->ry : f->sy, layout);
    const int64_t ox =
        std::lround(fp[0]) + int64_t(f->face->glyph->bitmap_left);
    const int64_t oy =
        std::lround(fp[1] + layout[0]) - int64_t(f->face->glyph->bitmap_top);
    const int64_t first = std::max<int64_t>(0, -ox);
    const int64_t last =
        std::min<int64_t>(bitmap.width, int64_t(surface.w) - ox);
    if (first < last) {
      for (unsigned row = 0; row < bitmap.rows; ++row) {
        const int64_t y = oy + row;
        if (y < 0 || y >= surface.h)
          continue;
        const auto *input = bitmap.buffer + int64_t(row) * bitmap.pitch + first;
        const uint64_t destination = reinterpret_cast<uint64_t>(surface.buf) +
                                     uint64_t(y) * uint32_t(surface.pitch) +
                                     uint64_t(ox + first);
        const size_t count = size_t(last - first);
        if (!hooks.max_memory(hooks.context, destination, input, count))
          throw std::runtime_error(
              "font service rejected coverage publication");
        blend_bytes.fetch_add(count, std::memory_order_relaxed);
      }
    }
  }
  if (metrics)
    write_memory(metrics, measured.data(), sizeof(measured));
  if (output)
    write_memory(output, rendered.data(), rendered.size());
  glyph_calls.fetch_add(1, std::memory_order_relaxed);
  return 0;
}
Font *font(uint64_t address) {
  auto found = fonts.find(address);
  return found == fonts.end() ? nullptr : found->second;
}
void retain_font(void *value) {
  auto *f = static_cast<Font *>(value);
  fonts.emplace(reinterpret_cast<uint64_t>(value), f);
  register_range(value, sizeof(Font));
}
void retain_token(void *value) {
  tokens.emplace_back(static_cast<uint64_t *>(value));
  register_range(value, sizeof(uint64_t));
}
void reset_locked() {
  for (auto &[handle, value] : fonts) {
    (void)handle;
    FT_Done_Face(value->face);
    delete value;
  }
  fonts.clear();
  retired.clear();
  tokens.clear();
  blobs.clear();
  dcp::hle::g_game_otf = nullptr;
  dcp::hle::g_game_otf_size = 0;
  dcp::hle::g_fallback_file.clear();
  dcp::hle::g_missing.clear();
  if (dcp::hle::g_ft)
    FT_Done_FreeType(dcp::hle::g_ft);
  dcp::hle::g_ft = nullptr;
}
bool named(const char *name, const char *expected) {
  return std::strcmp(name, expected) == 0;
}
} // namespace

void dcp_font_init(DcpFontHooks callbacks) {
  const unsigned copies = unsigned(callbacks.validate_memory != nullptr) +
                          unsigned(callbacks.read_memory != nullptr) +
                          unsigned(callbacks.write_memory != nullptr) +
                          unsigned(callbacks.max_memory != nullptr);
  if (copies && copies != 4)
    throw std::runtime_error("font memory callbacks must be supplied together");
  std::lock_guard<std::mutex> lock(adapter_mutex);
  reset_locked();
  hooks = callbacks;
  read_bytes = write_bytes = blend_bytes = glyph_calls = 0;
  synchronized_copies = copies == 4;
  register_range(dcp::hle::g_lib_sel, sizeof(dcp::hle::g_lib_sel));
  register_range(dcp::hle::g_rend_sel, sizeof(dcp::hle::g_rend_sel));
}
void dcp_font_reset() {
  std::lock_guard<std::mutex> lock(adapter_mutex);
  reset_locked();
  hooks = {};
  synchronized_copies = false;
}
DcpFontMemoryStats dcp_font_memory_stats() {
  return {synchronized_copies.load(std::memory_order_relaxed),
          read_bytes.load(std::memory_order_relaxed),
          write_bytes.load(std::memory_order_relaxed),
          blend_bytes.load(std::memory_order_relaxed),
          glyph_calls.load(std::memory_order_relaxed)};
}
bool dcp_font_service(const char *name, const uint64_t a[6], const float fp[2],
                      uint64_t *result) {
  if (!name || !a || !fp || !result)
    return false;
  std::lock_guard<std::mutex> lock(adapter_mutex);
  using namespace dcp::hle;
  *result = 0;
  if (named(name, "sceFontMemoryInit")) {
    std::array<uint8_t, 0x40> mem{};
    if (a[0])
      validate(a[0], mem.size(), true);
    *result =
        uint32_t(sceFontMemoryInit(a[0] ? mem.data() : nullptr, nullptr,
                                   uint32_t(a[2]), nullptr, nullptr, nullptr));
    if (a[0] && !*result)
      write_memory(a[0], mem.data(), mem.size());
  } else if (named(name, "sceFontMemoryTerm")) {
    *result = uint32_t(sceFontMemoryTerm(nullptr));
  } else if (named(name, "sceFontSelectLibraryFt")) {
    *result = reinterpret_cast<uint64_t>(sceFontSelectLibraryFt(int32_t(a[0])));
  } else if (named(name, "sceFontSelectRendererFt")) {
    *result =
        reinterpret_cast<uint64_t>(sceFontSelectRendererFt(int32_t(a[0])));
  } else if (named(name, "sceFontCreateLibraryWithEdition") ||
             named(name, "sceFontCreateRendererWithEdition")) {
    if (!a[3]) {
      *result = invalid;
      return true;
    }
    validate(a[3], 8, true);
    void *out = nullptr;
    int32_t status =
        named(name, "sceFontCreateLibraryWithEdition")
            ? sceFontCreateLibraryWithEdition(nullptr, nullptr, a[2], &out)
            : sceFontCreateRendererWithEdition(nullptr, nullptr, a[2], &out);
    *result = uint32_t(status);
    if (!status) {
      retain_token(out);
      write_memory(a[3], &out, sizeof(out));
    }
  } else if (named(name, "sceFontDestroyLibrary") ||
             named(name, "sceFontDestroyRenderer") ||
             named(name, "sceFontSupportSystemFonts") ||
             named(name, "sceFontSupportExternalFonts") ||
             named(name, "sceFontAttachDeviceCacheBuffer") ||
             named(name, "sceFontUnbindRenderer")) {
    // These existing source implementations return success and have no effect.
  } else if (named(name, "sceFontBindRenderer")) {
    *result = font(a[0]) ? uint32_t(sceFontBindRenderer(font(a[0]), nullptr))
                         : invalid;
  } else if (named(name, "sceFontOpenFontMemory")) {
    if (!a[4] || !a[1] || !a[2] || a[2] > UINT32_MAX) {
      *result = invalid;
      return true;
    }
    validate(a[4], 8, true);
    auto owned = std::make_shared<std::vector<uint8_t>>(size_t(a[2]));
    read_memory(a[1], owned->data(), owned->size());
    void *out = nullptr;
    *result = uint32_t(sceFontOpenFontMemory(nullptr, owned->data(),
                                             uint32_t(a[2]), nullptr, &out));
    if (!*result) {
      blobs.push_back(std::move(owned));
      retain_font(out);
      write_memory(a[4], &out, sizeof(out));
    }
  } else if (named(name, "sceFontOpenFontSet")) {
    if (!a[4]) {
      *result = invalid;
      return true;
    }
    validate(a[4], 8, true);
    void *out = nullptr;
    *result = uint32_t(sceFontOpenFontSet(nullptr, uint32_t(a[1]),
                                          uint32_t(a[2]), nullptr, &out));
    if (!*result) {
      retain_font(out);
      write_memory(a[4], &out, sizeof(out));
    }
  } else if (named(name, "sceFontCloseFont")) {
    if (!a[0])
      return true;
    auto *f = font(a[0]);
    if (!f) {
      *result = invalid;
      return true;
    }
    FT_Done_Face(f->face);
    f->face = nullptr;
    f->magic = 0;
    fonts.erase(a[0]);
    // Root's registered range remains valid until the entire bootstrap is
    // reset.
    retired.emplace_back(f);
  } else if (named(name, "sceFontSetScalePixel") ||
             named(name, "sceFontSetupRenderScalePixel")) {
    if (!font(a[0]) || !std::isfinite(fp[0]) || !std::isfinite(fp[1])) {
      *result = invalid;
      return true;
    }
    *result =
        uint32_t(named(name, "sceFontSetScalePixel")
                     ? sceFontSetScalePixel(font(a[0]), fp[0], fp[1])
                     : sceFontSetupRenderScalePixel(font(a[0]), fp[0], fp[1]));
  } else if (named(name, "sceFontGetHorizontalLayout") ||
             named(name, "sceFontGetVerticalLayout")) {
    if (!font(a[0]) || !a[1]) {
      *result = invalid;
      return true;
    }
    validate(a[1], 12, true);
    std::array<float, 3> out{};
    *result = uint32_t(named(name, "sceFontGetHorizontalLayout")
                           ? sceFontGetHorizontalLayout(font(a[0]), out.data())
                           : sceFontGetVerticalLayout(font(a[0]), out.data()));
    if (!*result)
      write_memory(a[1], out.data(), sizeof(out));
  } else if (named(name, "sceFontRenderSurfaceInit")) {
    const int32_t pitch = int32_t(a[2]), bpp = int32_t(a[3]),
                  width = int32_t(a[4]), height = int32_t(a[5]);
    if (!a[0] || pitch <= 0 || width < 0 || height < 0 || width > pitch) {
      *result = invalid;
      return true;
    }
    const uint64_t bytes = uint64_t(pitch) * uint32_t(height);
    validate(a[0], 0x80, true);
    if (a[1])
      validate(a[1], size_t(bytes), true);
    void *buffer = hooks.write_memory
                       ? reinterpret_cast<void *>(a[1])
                       : (a[1] ? pointer(a[1], size_t(bytes), true) : nullptr);
    std::array<uint8_t, 0x80> surface{};
    *result = uint32_t(sceFontRenderSurfaceInit(surface.data(), buffer, pitch,
                                                bpp, width, height));
    if (!*result)
      write_memory(a[0], surface.data(), surface.size());
  } else if (named(name, "sceFontRenderCharGlyphImage")) {
    if (!font(a[0]) || !a[2] || !std::isfinite(fp[0]) ||
        !std::isfinite(fp[1])) {
      *result = invalid;
      return true;
    }
    Surface value;
    read_memory(a[2], &value, sizeof(value));
    if (value.pitch <= 0 || value.w < 0 || value.h < 0 ||
        value.w > value.pitch) {
      *result = invalid;
      return true;
    }
    if (hooks.write_memory) {
      // The original renderer rounds to int. Reject coordinates outside its
      // representable domain before invoking it, including baseline addition.
      float layout[3];
      auto *f = font(a[0]);
      hlayout(f, f->ry > 0 ? f->ry : f->sy, layout);
      constexpr float coordinateLimit =
          float(std::numeric_limits<int>::max() / 4);
      if (std::abs(fp[0]) > coordinateLimit ||
          std::abs(fp[1]) > coordinateLimit || !std::isfinite(layout[0]) ||
          std::abs(fp[1] + layout[0]) > coordinateLimit) {
        *result = invalid;
        return true;
      }
      *result = render_checked(f, uint32_t(a[1]), value, fp, a[3], a[4]);
      return true;
    }
    if (value.buf)
      pointer(reinterpret_cast<uint64_t>(value.buf),
              size_t(uint64_t(value.pitch) * uint32_t(value.h)), true);
    auto *metrics =
        a[3] ? static_cast<float *>(pointer(a[3], 32, true)) : nullptr;
    void *out = a[4] ? pointer(a[4], 0x30, true) : nullptr;
    *result = uint32_t(sceFontRenderCharGlyphImage(
        font(a[0]), uint32_t(a[1]), &value, fp[0], fp[1], metrics, out));
  } else
    return false;
  return true;
}
