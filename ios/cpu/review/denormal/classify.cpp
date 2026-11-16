#include "status.h"
#include <cstdint>
#include <cstdio>
int main() {
  int op;
  unsigned lhs, rhs, mode, result, csr;
  unsigned checks = 0, failures = 0;
  while (std::scanf("%d %x %x %u %x %x", &op, &lhs, &rhs, &mode, &result,
                    &csr) == 6) {
    if (op < 0 || op > 3 || mode > 3)
      return 2;
    uint32_t init =
        0x1f80 | ((mode & 1) ? 0x40 : 0) | ((mode & 2) ? 0x8000 : 0);
    bool predicted = experimental_denormal::add_denormal_status(
        static_cast<experimental_denormal::Op>(op), lhs, rhs, init);
    ++checks;
    if (predicted != bool(csr & 2)) {
      if (failures < 8)
        std::printf("MISMATCH op=%d lhs=%08x rhs=%08x mode=%u csr=%08x\n", op,
                    lhs, rhs, mode, csr);
      ++failures;
    }
  }
  std::printf("checks=%u failures=%u\n", checks, failures);
  return checks == 4096 && failures == 0 ? 0 : 1;
}
