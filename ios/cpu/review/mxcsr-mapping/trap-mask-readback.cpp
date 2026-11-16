#include <cstdint>
#include <cstdio>
#include <xmmintrin.h>
int main() {
  uint32_t old = _mm_getcsr();
  _mm_setcsr(0x1f00);
  uint32_t before = _mm_getcsr(), lhs = 0x7f800000, rhs = 0xff800000, after = 0;
  asm volatile("movd %1, %%xmm2; addss %2, %%xmm2; stmxcsr %0"
               : "=m"(after)
               : "r"(lhs), "m"(rhs)
               : "xmm2", "memory");
  _mm_setcsr(old);
  printf("before=%08x after=%08x\n", before, after);
}
