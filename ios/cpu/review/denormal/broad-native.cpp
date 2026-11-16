#include <cstdint>
#include <cstdio>
#include <xmmintrin.h>
static const uint32_t values[] = {
    0,          0x80000000, 1,          0x80000001, 0x007fffff, 0x807fffff,
    0x00800000, 0x80800000, 0x3f800000, 0xbf800000, 0x7f7fffff, 0x7f800000,
    0xff800000, 0x7fc01234, 0xffc05678, 0x7f801234};
#define EXEC(OP)                                                               \
  asm volatile("movd %2, %%xmm2; " OP                                          \
               " %3, %%xmm2; movd %%xmm2, %1; stmxcsr %0"                      \
               : "=m"(csr), "=r"(result)                                       \
               : "r"(lhs), "m"(rhs)                                            \
               : "xmm2", "memory")
int main() {
  uint32_t old = _mm_getcsr();
  for (int op = 0; op < 4; ++op)
    for (uint32_t lhs : values)
      for (uint32_t rhs : values)
        for (unsigned mode = 0; mode < 4; ++mode) {
          uint32_t init = 0x1f80 | ((mode & 1) ? 0x40 : 0) |
                          ((mode & 2) ? 0x8000 : 0),
                   result = 0, csr = 0;
          _mm_setcsr(init);
          switch (op) {
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
          std::printf("%d %08x %08x %u %08x %08x\n", op, lhs, rhs, mode, result,
                      csr);
        }
  _mm_setcsr(old);
}
