#include <cstdint>
#include <cstdio>
#include <xmmintrin.h>
int main() {
  uint32_t old = _mm_getcsr(), csr = 0, inf = 0x7f800000, neginf = 0xff800000,
           one = 0x3f800000, zero = 0;
  _mm_setcsr(0x1f80);
  asm volatile("movd %1, %%xmm2; addss %2, %%xmm2; movd %3, %%xmm2; divss %4, "
               "%%xmm2; stmxcsr %0"
               : "=m"(csr)
               : "r"(inf), "m"(neginf), "r"(one), "m"(zero)
               : "xmm2", "memory");
  _mm_setcsr(old);
  std::printf("guest_mxcsr=%08x\n", csr);
  return csr == 0x1f85 ? 0 : 1;
}
