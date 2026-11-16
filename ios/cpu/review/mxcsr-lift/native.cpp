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
                {"add_denormal", 0, 0x3f800000, 0x00000001},
                {"add_invalid", 0, 0x7f800000, 0xff800000},
                {"add_snan", 0, 0x7f801234, 0x3f800000},
                {"add_qnan", 0, 0x7fc01234, 0x3f800000},
                {"add_rhs_snan", 0, 0x3f800000, 0x7f801234},
                {"add_qnan_rhs_snan", 0, 0x7fc01234, 0x7f801234},
                {"sub_lhs_snan", 1, 0x7f801234, 0x3f800000},
                {"sub_rhs_snan", 1, 0x3f800000, 0x7f801234},
                {"mul_lhs_snan", 2, 0x7f801234, 0x3f800000},
                {"mul_rhs_snan", 2, 0x3f800000, 0x7f801234},
                {"div_lhs_snan", 3, 0x7f801234, 0x3f800000},
                {"div_rhs_snan", 3, 0x3f800000, 0x7f801234},
                {"sub_invalid", 1, 0x7f800000, 0x7f800000},
                {"mul_invalid", 2, 0x00000000, 0x7f800000},
                {"mul_overflow", 2, 0x7f7fffff, 0x40000000},
                {"div_invalid", 3, 0, 0},
                {"div_zero", 3, 0x3f800000, 0},
                {"div_underflow", 3, 0x00800000, 0x40400000},
                {"div_inexact", 3, 0x3f800000, 0x40400000}};
extern "C" void seq_add(const uint32_t *, uint32_t *);
extern "C" void seq_sub(const uint32_t *, uint32_t *);
extern "C" void seq_mul(const uint32_t *, uint32_t *);
extern "C" void seq_div(const uint32_t *, uint32_t *);
int main() {
  for (auto &c : cases)
    for (uint32_t sticky : {0u, 1u}) {
      auto original = _mm_getcsr();
      uint32_t input[] = {0x1f80 | sticky, c.lhs, c.rhs};
      uint32_t output[2] = {};
      void (*const fn[])(const uint32_t *, uint32_t *) = {seq_add, seq_sub, seq_mul, seq_div};
      fn[c.op](input, output);
      uint32_t csr = output[0], result = output[1];
      _mm_setcsr(original);
      std::printf("%s %u %08x %08x\n", c.name, sticky, result, csr);
    }
}
