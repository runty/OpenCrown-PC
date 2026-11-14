// Remill's x87 bit positions are independent of Darwin arm64's FE_* constants.
#include "fp80.h"
#include <fenv.h>
#include <stdint.h>
static _Thread_local uint32_t extra_flags;
static int native_mask(uint32_t mask) {
  return ((mask & 1) ? FE_INVALID : 0) | ((mask & 4) ? FE_DIVBYZERO : 0) |
         ((mask & 8) ? FE_OVERFLOW : 0) | ((mask & 16) ? FE_UNDERFLOW : 0) |
         ((mask & 32) ? FE_INEXACT : 0);
}
static uint32_t guest_mask(int flags) {
  return ((flags & FE_INVALID) ? 1 : 0) | ((flags & FE_DIVBYZERO) ? 4 : 0) |
         ((flags & FE_OVERFLOW) ? 8 : 0) | ((flags & FE_UNDERFLOW) ? 16 : 0) |
         ((flags & FE_INEXACT) ? 32 : 0);
}
int32_t __remill_fpu_exception_test(int32_t mask) {
  return (int32_t)((guest_mask(fetestexcept(native_mask((uint32_t)mask))) |
                    dcp_fp80_x87_exception_flags() | extra_flags) &
                   (uint32_t)mask & 127);
}
void __remill_fpu_exception_clear(int32_t mask) {
  feclearexcept(native_mask((uint32_t)mask));
  dcp_fp80_clear_x87_exceptions((uint32_t)mask);
  extra_flags &= ~(uint32_t)mask;
}
void __remill_fpu_exception_raise(int32_t mask) {
  feraiseexcept(native_mask((uint32_t)mask));
  extra_flags |= (uint32_t)mask & 0x42;
}
