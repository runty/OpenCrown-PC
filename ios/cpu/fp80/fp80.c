// SoftFloat platform macros must precede its internal headers.
#include "platform.h"

#include "fp80.h"
#include "internals.h"
#include "softfloat.h"
#include "specialize.h"
#include <fenv.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local uint16_t control_word = 0xffff;
static void mode(void) {
  if (control_word == 0xffff) {
    switch (fegetround()) {
    case FE_DOWNWARD:
      softfloat_roundingMode = softfloat_round_min;
      break;
    case FE_UPWARD:
      softfloat_roundingMode = softfloat_round_max;
      break;
    case FE_TOWARDZERO:
      softfloat_roundingMode = softfloat_round_minMag;
      break;
    default:
      softfloat_roundingMode = softfloat_round_near_even;
      break;
    }
    extF80_roundingPrecision = 80;
  } else {
    static const uint8_t rounding[] = {softfloat_round_near_even,
                                       softfloat_round_min, softfloat_round_max,
                                       softfloat_round_minMag};
    static const uint8_t precision[] = {32, 80, 64, 80};
    softfloat_roundingMode = rounding[(control_word >> 10) & 3];
    extF80_roundingPrecision = precision[(control_word >> 8) & 3];
  }
}
void dcp_fp80_set_control(uint16_t value) {
  control_word = value;
  mode();
}
uint32_t dcp_fp80_exception_flags(void) { return softfloat_exceptionFlags; }
void dcp_fp80_clear_exceptions(void) { softfloat_exceptionFlags = 0; }
static uint_fast8_t from_x87(uint32_t mask) {
  return ((mask & 1) ? softfloat_flag_invalid : 0) |
         ((mask & 4) ? softfloat_flag_infinite : 0) |
         ((mask & 8) ? softfloat_flag_overflow : 0) |
         ((mask & 16) ? softfloat_flag_underflow : 0) |
         ((mask & 32) ? softfloat_flag_inexact : 0);
}
uint32_t dcp_fp80_x87_exception_flags(void) {
  uint_fast8_t f = softfloat_exceptionFlags;
  return ((f & softfloat_flag_invalid) ? 1 : 0) |
         ((f & softfloat_flag_infinite) ? 4 : 0) |
         ((f & softfloat_flag_overflow) ? 8 : 0) |
         ((f & softfloat_flag_underflow) ? 16 : 0) |
         ((f & softfloat_flag_inexact) ? 32 : 0);
}
void dcp_fp80_clear_x87_exceptions(uint32_t mask) {
  softfloat_exceptionFlags &= ~from_x87(mask);
}
static extFloat80_t raw(const void *pointer) {
  extFloat80_t value;
  memset(&value, 0, sizeof(value));
  memcpy(&value.signif, pointer, 8);
  memcpy(&value.signExp, (const uint8_t *)pointer + 8, 2);
  return value;
}
static void put(void *pointer, extFloat80_t value) {
  memset(pointer, 0, 16);
  memcpy(pointer, &value.signif, 8);
  memcpy((uint8_t *)pointer + 8, &value.signExp, 2);
}
static bool nan80(extFloat80_t value) {
  return (value.signExp & 0x7fff) == 0x7fff &&
         (value.signif & UINT64_C(0x7fffffffffffffff));
}
static extFloat80_t invalid(void) {
  softfloat_raiseFlags(softfloat_flag_invalid);
  extFloat80_t value = {.signif = UINT64_C(0xc000000000000000),
                        .signExp = 0xffff};
  return value;
}
static extFloat80_t canonical(extFloat80_t value) {
  unsigned exponent = value.signExp & 0x7fff;
  if (exponent && !(value.signif >> 63))
    return invalid();
  if (!exponent && (value.signif >> 63))
    value.signExp |= 1; // x87 pseudo-denormal
  return value;
}
static extFloat80_t scale(extFloat80_t a, extFloat80_t b) {
  // Match FSCALE directly; materializing 2^N first can overflow while a*2^N is
  // finite.
  if (nan80(a) || nan80(b))
    return extF80_add(a, b);
  const bool infinity_a = (a.signExp & 0x7fff) == 0x7fff;
  const bool infinity_b = (b.signExp & 0x7fff) == 0x7fff;
  const bool zero_a = !a.signif;
  const bool negative_b = (b.signExp >> 15) != 0;
  if (infinity_b) {
    if ((zero_a && !negative_b) || (infinity_a && negative_b))
      return invalid();
    if (zero_a || infinity_a)
      return a;
    // Infinite scale factors select signed zero/infinity exactly, independent
    // of the finite-overflow rounding mode (x87 FSCALE result table).
    extFloat80_t result = {
        .signif = negative_b ? 0 : UINT64_C(0x8000000000000000),
        .signExp =
            (uint16_t)((a.signExp & 0x8000) | (negative_b ? 0 : 0x7fff))};
    return result;
  } else if (zero_a || infinity_a)
    return a;
  int32_t exponent;
  extFloat80_t bound = i32_to_extF80(65536),
               negative_bound = i32_to_extF80(-65536);
  if (infinity_b)
    exponent = negative_b ? -65536 : 65536;
  else if (extF80_lt(bound, b))
    exponent = 65536;
  else if (extF80_lt(b, negative_bound))
    exponent = -65536;
  else
    exponent = (int32_t)extF80_to_i32(b, softfloat_round_minMag, false);
  int32_t value_exponent = a.signExp & 0x7fff;
  uint64_t significand = a.signif;
  if (!value_exponent)
    value_exponent = 1;
  if (!(significand >> 63)) {
    struct exp32_sig64 normalized =
        softfloat_normSubnormalExtF80Sig(significand);
    value_exponent += normalized.exp;
    significand = normalized.sig;
  }
  return softfloat_roundPackToExtF80(a.signExp >> 15, value_exponent + exponent,
                                     significand, 0, 80);
}
void dcp_fp80_binary(void *out, const void *ap, const void *bp, uint32_t op) {
  mode();
  extFloat80_t a = canonical(raw(ap)), b = canonical(raw(bp)), result;
  switch (op) {
  case 0:
    result = extF80_add(a, b);
    break;
  case 1:
    result = extF80_sub(a, b);
    break;
  case 2:
    result = extF80_mul(a, b);
    break;
  case 3:
    result = extF80_div(a, b);
    break;
  case 4:
    result = scale(a, b);
    break;
  default:
    abort();
  }
  put(out, result);
}
void dcp_fp80_unary(void *out, const void *ap, uint32_t op) {
  mode();
  extFloat80_t a = raw(ap), result = a;
  if (op == 0)
    result.signExp ^= 0x8000;
  else if (op == 1)
    result.signExp &= 0x7fff;
  else {
    a = canonical(a);
    switch (op) {
    case 2:
      result = extF80_sqrt(a);
      break;
    case 3:
      result = extF80_roundToInt(a, softfloat_round_minMag, false);
      break;
    case 4:
      result = extF80_roundToInt(a, softfloat_round_min, false);
      break;
    case 5:
      result = extF80_roundToInt(a, softfloat_round_max, false);
      break;
    case 6:
      result = extF80_roundToInt(a, softfloat_round_near_even, false);
      break;
    case 7:
      result = extF80_roundToInt(a, softfloat_round_near_maxMag, false);
      break;
    case 8:
      result = extF80_roundToInt(a, softfloat_roundingMode, true);
      break;
    case 9:
      result = extF80_roundToInt(a, softfloat_roundingMode, false);
      break;
    default:
      abort();
    }
  }
  put(out, result);
}
void dcp_fp80_from_i64(void *out, int64_t value) {
  mode();
  put(out, i64_to_extF80(value));
}
void dcp_fp80_from_u64(void *out, uint64_t value) {
  mode();
  put(out, ui64_to_extF80(value));
}
void dcp_fp80_from_f32(void *out, uint32_t bits) {
  mode();
  float32_t value = {bits};
  put(out, f32_to_extF80(value));
}
void dcp_fp80_from_f64(void *out, uint64_t bits) {
  mode();
  float64_t value = {bits};
  put(out, f64_to_extF80(value));
}
uint32_t dcp_fp80_to_f32(const void *a) {
  mode();
  return extF80_to_f32(canonical(raw(a))).v;
}
uint64_t dcp_fp80_to_f64(const void *a) {
  mode();
  return extF80_to_f64(canonical(raw(a))).v;
}
uint64_t dcp_fp80_to_int(const void *a, uint32_t is_unsigned) {
  mode();
  extFloat80_t value = canonical(raw(a));
  return is_unsigned
             ? extF80_to_ui64(value, softfloat_round_minMag, false)
             : (uint64_t)extF80_to_i64(value, softfloat_round_minMag, false);
}
uint32_t dcp_fp80_compare(const void *ap, const void *bp, uint32_t predicate) {
  mode();
  extFloat80_t a = canonical(raw(ap)), b = canonical(raw(bp));
  bool unordered = nan80(a) || nan80(b);
  bool eq = extF80_eq(a, b), lt = extF80_lt_quiet(a, b),
       gt = extF80_lt_quiet(b, a);
  switch (predicate) {
  case 0:
    return false;
  case 1:
    return eq;
  case 2:
    return gt;
  case 3:
    return gt || eq;
  case 4:
    return lt;
  case 5:
    return lt || eq;
  case 6:
    return !unordered && !eq;
  case 7:
    return !unordered;
  case 8:
    return unordered;
  case 9:
    return unordered || eq;
  case 10:
    return unordered || gt;
  case 11:
    return unordered || gt || eq;
  case 12:
    return unordered || lt;
  case 13:
    return unordered || lt || eq;
  case 14:
    return !eq;
  case 15:
    return true;
  default:
    abort();
  }
}
