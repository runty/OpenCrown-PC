#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <xmmintrin.h>
struct Case {
  const char *name;
  int op;
  uint32_t lhs, rhs;
};
Case cases[] = {{"add_exact", 0, 0x3f800000, 0x40000000},
                {"add_invalid", 0, 0x7f800000, 0xff800000},
                {"add_snan", 0, 0x7f801234, 0x3f800000},
                {"add_qnan", 0, 0x7fc01234, 0x3f800000},
                {"sub_invalid", 1, 0x7f800000, 0x7f800000},
                {"mul_invalid", 2, 0x00000000, 0x7f800000},
                {"mul_overflow", 2, 0x7f7fffff, 0x40000000},
                {"div_invalid", 3, 0, 0},
                {"div_zero", 3, 0x3f800000, 0},
                {"div_underflow", 3, 0x00800000, 0x40400000},
                {"div_inexact", 3, 0x3f800000, 0x40400000}};
#define EXEC(OP)                                                               \
  asm volatile("movd %2, %%xmm2; " OP                                          \
               " %3, %%xmm2; movd %%xmm2, %1; stmxcsr %0"                      \
               : "=m"(csr), "=r"(result)                                       \
               : "r"(c.lhs), "m"(c.rhs)                                        \
               : "xmm2", "memory")
int main() {
  for (auto &c : cases)
    for (uint32_t sticky : {0u, 1u}) {
      auto original = _mm_getcsr();
      _mm_setcsr(0x1f80 | sticky);
      uint32_t result = 0, csr = 0;
      switch (c.op) {
      case 0:
        EXEC("addss");
        break;
      case 1:
        EXEC("subss");
        break;
      case 2:
        EXEC("mulss");
        break;
      case 3:
        EXEC("divss");
        break;
      }
      _mm_setcsr(original);
      std::printf("%s %u %08x %08x\n", c.name, sticky, result, csr);
    }
}
