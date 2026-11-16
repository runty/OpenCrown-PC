#include "status.h"
#include <cfenv>
#include <cstdio>
int main() {
  std::fenv_t saved;
  if (std::fegetenv(&saved))
    return 2;
  const uint32_t original = experimental_mxcsr::read_fpsr();
  unsigned checks = 0, failures = 0;
  for (uint32_t status = 0; status < 64; ++status) {
    experimental_mxcsr::write_fpsr(original | 0x9f);
    uint32_t before = experimental_mxcsr::read_fpsr();
    uint32_t guest = 0x1f80 | status;
    experimental_mxcsr::write_fpsr(
        experimental_mxcsr::load_fpsr(guest, before));
    uint32_t after = experimental_mxcsr::read_fpsr();
    ++checks;
    if ((after & 0x1f) != experimental_mxcsr::guest_to_host(guest) ||
        (after & ~0x1f) != (before & ~0x1f))
      ++failures;
    uint32_t stored = experimental_mxcsr::store(guest & ~0x3du, after);
    ++checks;
    if ((stored & 0x3f) != status)
      ++failures;
  }
  experimental_mxcsr::write_fpsr(original);
  if (std::fesetenv(&saved))
    return 3;
  std::printf("checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
