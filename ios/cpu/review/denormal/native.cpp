#include <cstdint>
#include <cstdio>
#include <xmmintrin.h>
struct Case {
  const char *name;
  uint32_t lhs, rhs;
};
static const Case cases[] = {
    {"one_denorm", 0x3f800000, 0x00000001},
    {"denorm_one", 0x00000001, 0x3f800000},
    {"zero_denorm", 0x00000000, 0x00000001},
    {"denorm_zero", 0x00000001, 0x00000000},
    {"inf_denorm", 0x7f800000, 0x00000001},
    {"denorm_inf", 0x00000001, 0x7f800000},
    {"qnan_denorm", 0x7fc01234, 0x00000001},
    {"denorm_qnan", 0x00000001, 0x7fc01234},
    {"snan_denorm", 0x7f801234, 0x00000001},
    {"denorm_snan", 0x00000001, 0x7f801234},
    {"denorm_denorm", 0x00000001, 0x80000001},
    {"normal_denorm", 0x00800000, 0x00000001},
};
#define EXEC(OP)                                                               \
  asm volatile("movd %2, %%xmm2; " OP                                          \
               " %3, %%xmm2; movd %%xmm2, %1; stmxcsr %0"                      \
               : "=m"(csr), "=r"(result)                                       \
               : "r"(c.lhs), "m"(c.rhs)                                        \
               : "xmm2", "memory")
int main() {
  uint32_t old = _mm_getcsr();
  for (int op = 0; op < 4; ++op)
    for (auto &c : cases)
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
        std::printf("%d %s %u %08x %08x\n", op, c.name, mode, result, csr);
      }
  _mm_setcsr(old);
}
