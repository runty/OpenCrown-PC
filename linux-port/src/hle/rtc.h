#pragma once
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>

namespace dcp::hle {
// SceRtcDateTime contains six little-endian 16-bit fields followed by
// microseconds as a 32-bit field. The caller provides 16 bytes of storage.
inline int32_t rtc_local_clock(uint16_t* dt) {
    if (!dt) return int32_t(0x80b50002);
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm lt{};
#ifdef _WIN32
    if (localtime_s(&lt, &t)) return int32_t(0x80b50002);
#else
    if (!localtime_r(&t, &lt)) return int32_t(0x80b50002);
#endif
    dt[0] = uint16_t(lt.tm_year + 1900), dt[1] = uint16_t(lt.tm_mon + 1), dt[2] = uint16_t(lt.tm_mday);
    dt[3] = uint16_t(lt.tm_hour), dt[4] = uint16_t(lt.tm_min), dt[5] = uint16_t(lt.tm_sec);
    uint32_t usec = uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() % 1000000);
    std::memcpy(dt + 6, &usec, 4);
    return 0;
}

// sceRtcGetWin32FileTime(const SceRtcDateTime* in, u64* out): the date as 100 ns units since 1601-01-01, no time zone
// change (FUN_005723e0 unpacks a stored date stamp into a SceRtcDateTime first).
inline int32_t rtc_file_time(const uint16_t* dt, uint64_t* out) {
    if (!dt || !out) return int32_t(0x80b50002);
    int64_t y = dt[0], m = dt[1], d = dt[2];
    if (m < 1 || m > 12 || d < 1 || d > 31) return int32_t(0x80b50002);
    y -= m <= 2;  // days_from_civil (H. Hinnant)
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    uint64_t yoe = uint64_t(y - era * 400);
    uint64_t doy = (153 * uint64_t(m > 2 ? m - 3 : m + 9) + 2) / 5 + uint64_t(d) - 1;
    uint64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + int64_t(doe) - 719468 + 134774;  // days since 1601-01-01
    uint32_t usec;
    std::memcpy(&usec, dt + 6, 4);
    if (days < 0) return int32_t(0x80b50002);
    uint64_t sec = uint64_t(days) * 86400 + dt[3] * 3600ull + dt[4] * 60ull + dt[5];
    *out = sec * 10000000ull + usec * 10ull;
    return 0;
}
} // namespace dcp::hle
