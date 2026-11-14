#include "fp80.h"
#include "platform.h"
#include "softfloat.h"
#include <fenv.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
extern void __remill_fpu_exception_clear(int32_t);
extern int32_t __remill_fpu_exception_test(int32_t);
#ifdef LOWERED_REMAINDERS
extern void lowered_remainder(void *, const void *, const void *);
extern void lowered_fmod(void *, const void *, const void *);
extern void lowered_frem(void *, const void *, const void *);
#endif
void review_begin(unsigned flags) {
  fenv_t saved;
  fegetenv(&saved);
  __remill_fpu_exception_clear(127);
  fesetenv(&saved);
  softfloat_exceptionFlags = flags;
}
unsigned review_flags(void) { return softfloat_exceptionFlags; }
unsigned review_modes(void) {
  return (unsigned)extF80_roundingPrecision |
         ((unsigned)softfloat_roundingMode << 8);
}
uint32_t review_remainder(unsigned char *out, const unsigned char *a,
                          const unsigned char *b, uint16_t control,
                          unsigned op) {
  dcp_fp80_set_control(control);
#ifdef LOWERED_REMAINDERS
  memset(out, 0, 16);
  if (!op)
    lowered_remainder(out, a, b);
  else {
    unsigned char other[16] = {0};
    lowered_fmod(out, a, b);
    lowered_frem(other, a, b);
    if (memcmp(out, other, 16))
      abort();
  }
#else
  dcp_fp80_binary(out, a, b, op + 5);
#endif
  return (uint32_t)__remill_fpu_exception_test(63);
}
