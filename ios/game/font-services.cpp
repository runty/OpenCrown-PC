#include "font-services.h"
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
void **output(uint64_t address) {
  return static_cast<void **>(pointer(address, 8, true));
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
  std::lock_guard<std::mutex> lock(adapter_mutex);
  reset_locked();
  hooks = callbacks;
  register_range(dcp::hle::g_lib_sel, sizeof(dcp::hle::g_lib_sel));
  register_range(dcp::hle::g_rend_sel, sizeof(dcp::hle::g_rend_sel));
}
void dcp_font_reset() {
  std::lock_guard<std::mutex> lock(adapter_mutex);
  reset_locked();
  hooks = {};
}
bool dcp_font_service(const char *name, const uint64_t a[6], const float fp[2],
                      uint64_t *result) {
  if (!name || !a || !fp || !result)
    return false;
  std::lock_guard<std::mutex> lock(adapter_mutex);
  using namespace dcp::hle;
  *result = 0;
  if (named(name, "sceFontMemoryInit")) {
    void *mem = a[0] ? pointer(a[0], 0x40, true) : nullptr;
    *result = uint32_t(sceFontMemoryInit(mem, nullptr, uint32_t(a[2]), nullptr,
                                         nullptr, nullptr));
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
    auto **out = output(a[3]);
    int32_t status =
        named(name, "sceFontCreateLibraryWithEdition")
            ? sceFontCreateLibraryWithEdition(nullptr, nullptr, a[2], out)
            : sceFontCreateRendererWithEdition(nullptr, nullptr, a[2], out);
    *result = uint32_t(status);
    if (!status)
      retain_token(*out);
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
    auto *bytes =
        static_cast<const uint8_t *>(pointer(a[1], size_t(a[2]), false));
    auto owned =
        std::make_shared<std::vector<uint8_t>>(bytes, bytes + size_t(a[2]));
    auto **out = output(a[4]);
    *result = uint32_t(sceFontOpenFontMemory(nullptr, owned->data(),
                                             uint32_t(a[2]), nullptr, out));
    if (!*result) {
      blobs.push_back(std::move(owned));
      retain_font(*out);
    }
  } else if (named(name, "sceFontOpenFontSet")) {
    if (!a[4]) {
      *result = invalid;
      return true;
    }
    auto **out = output(a[4]);
    *result = uint32_t(sceFontOpenFontSet(nullptr, uint32_t(a[1]),
                                          uint32_t(a[2]), nullptr, out));
    if (!*result)
      retain_font(*out);
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
    auto *out = static_cast<float *>(pointer(a[1], 12, true));
    *result = uint32_t(named(name, "sceFontGetHorizontalLayout")
                           ? sceFontGetHorizontalLayout(font(a[0]), out)
                           : sceFontGetVerticalLayout(font(a[0]), out));
  } else if (named(name, "sceFontRenderSurfaceInit")) {
    const int32_t pitch = int32_t(a[2]), bpp = int32_t(a[3]),
                  width = int32_t(a[4]), height = int32_t(a[5]);
    if (!a[0] || pitch <= 0 || width < 0 || height < 0 || width > pitch) {
      *result = invalid;
      return true;
    }
    const uint64_t bytes = uint64_t(pitch) * uint32_t(height);
    void *buffer = a[1] ? pointer(a[1], size_t(bytes), true) : nullptr;
    *result = uint32_t(sceFontRenderSurfaceInit(
        pointer(a[0], 0x80, true), buffer, pitch, bpp, width, height));
  } else if (named(name, "sceFontRenderCharGlyphImage")) {
    if (!font(a[0]) || !a[2] || !std::isfinite(fp[0]) ||
        !std::isfinite(fp[1])) {
      *result = invalid;
      return true;
    }
    auto *surface = pointer(a[2], sizeof(Surface), false);
    Surface value;
    std::memcpy(&value, surface, sizeof(value));
    if (value.pitch <= 0 || value.w < 0 || value.h < 0 ||
        value.w > value.pitch) {
      *result = invalid;
      return true;
    }
    if (value.buf)
      pointer(reinterpret_cast<uint64_t>(value.buf),
              size_t(uint64_t(value.pitch) * uint32_t(value.h)), true);
    auto *metrics =
        a[3] ? static_cast<float *>(pointer(a[3], 32, true)) : nullptr;
    void *out = a[4] ? pointer(a[4], 0x30, true) : nullptr;
    *result = uint32_t(sceFontRenderCharGlyphImage(
        font(a[0]), uint32_t(a[1]), surface, fp[0], fp[1], metrics, out));
  } else
    return false;
  return true;
}
