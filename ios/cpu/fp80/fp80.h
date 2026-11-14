#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Inputs contain 10 little-endian bytes. Output buffers require 16 bytes;
// bytes 10..15 are zeroed. No host long-double or FP80 calling convention.
// Binary operations: 0 add, 1 subtract, 2 multiply, 3 divide, 4 FSCALE,
// 5 IEEE remainder, 6 truncating fmod. Remainders retain full precision.
void dcp_fp80_binary(void *out, const void *a, const void *b,
                     uint32_t operation);
void dcp_fp80_unary(void *out, const void *a, uint32_t operation);
void dcp_fp80_from_i64(void *out, int64_t value);
void dcp_fp80_from_u64(void *out, uint64_t value);
void dcp_fp80_from_f32(void *out, uint32_t bits);
void dcp_fp80_from_f64(void *out, uint64_t bits);
uint32_t dcp_fp80_to_f32(const void *a);
uint64_t dcp_fp80_to_f64(const void *a);
uint64_t dcp_fp80_to_int(const void *a, uint32_t is_unsigned);
uint32_t dcp_fp80_compare(const void *a, const void *b,
                          uint32_t llvm_predicate);
// Per-thread x87 control: RC bits10..11, PC bits8..9. Pass 0xffff to use
// host fenv rounding with full 64-bit significand precision (LLVM f80 default).
void dcp_fp80_set_control(uint16_t control);
uint32_t dcp_fp80_exception_flags(void);
void dcp_fp80_clear_exceptions(void);
uint32_t dcp_fp80_x87_exception_flags(void);
void dcp_fp80_clear_x87_exceptions(uint32_t mask);
#ifdef __cplusplus
}
#endif
