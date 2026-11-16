#pragma once
#include <cstdint>

// Experimental masked-status mapping only. x86 denormal operand status (bit 1)
// is retained from virtual state, not inferred from ARM IDC. Control modes,
// trap delivery and guest/native environment isolation are not implemented.
namespace experimental_mxcsr {
constexpr uint32_t host_to_guest(uint32_t fpsr) {
  return (fpsr & 1u) | ((fpsr & 0x1eu) << 1);
}
constexpr uint32_t guest_to_host(uint32_t mxcsr) {
  return (mxcsr & 1u) | ((mxcsr & 0x3cu) >> 1);
}
constexpr uint32_t store(uint32_t guest, uint32_t fpsr) {
  return guest | host_to_guest(fpsr);
}
constexpr uint32_t load_fpsr(uint32_t guest, uint32_t fpsr) {
  return (fpsr & ~0x1fu) | guest_to_host(guest);
}
#if defined(__aarch64__) || defined(__arm64__)
inline uint32_t read_fpsr() {
  uint64_t value;
  asm volatile("mrs %0, fpsr" : "=r"(value) : : "memory");
  return static_cast<uint32_t>(value);
}
inline void write_fpsr(uint32_t value) {
  asm volatile("msr fpsr, %0" : : "r"(uint64_t(value)) : "memory");
}
#endif
} // namespace experimental_mxcsr
