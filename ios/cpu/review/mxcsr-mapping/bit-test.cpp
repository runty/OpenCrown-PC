#include "status.h"
#include <cstdio>
#include <initializer_list>
int main() {
  unsigned checks = 0, failures = 0;
  for (unsigned status = 0; status < 64; ++status) {
    for (uint32_t other : {0u, 0x08000080u}) {
      uint32_t guest = 0x1f80u | status;
      uint32_t loaded = experimental_mxcsr::load_fpsr(guest, other | 0x1fu);
      uint32_t roundtrip = experimental_mxcsr::store(guest & ~0x3du, loaded);
      ++checks;
      if ((roundtrip & 0x3fu) != status ||
          (loaded & ~0x1fu) != (other & ~0x1fu))
        ++failures;
      uint32_t cleared = experimental_mxcsr::load_fpsr(0x1f80u, loaded);
      ++checks;
      if ((cleared & 0x1fu) || (cleared & ~0x1fu) != (loaded & ~0x1fu))
        ++failures;
    }
  }
  std::printf("checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
