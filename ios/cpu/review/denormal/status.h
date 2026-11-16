#pragma once
#include <cstdint>

// Experimental 32-bit scalar ADD/SUB/MUL/DIV only, with every x86 FP
// exception masked. This reports a new denormal-operand bit; it does not
// implement DAZ input conversion, FTZ result handling or unmasked traps.
namespace experimental_denormal {
enum class Op { Add, Sub, Mul, Div };
constexpr bool is_denormal(uint32_t value) {
  const uint32_t magnitude = value & 0x7fffffffu;
  return magnitude != 0 && magnitude < 0x00800000u;
}
constexpr bool is_nan(uint32_t value) {
  return (value & 0x7fffffffu) > 0x7f800000u;
}
constexpr bool add_denormal_status(Op op, uint32_t lhs, uint32_t rhs,
                                   uint32_t mxcsr) {
  if (mxcsr & 0x40u)
    return false; // DAZ suppresses denormal-input status.
  if (is_nan(lhs) || is_nan(rhs))
    return false; // NaN handling has priority.
  if (op == Op::Div && is_denormal(lhs) && !(rhs & 0x7fffffffu))
    return false; // Divide-by-zero has priority for this scalar combination.
  return is_denormal(lhs) || is_denormal(rhs);
}
} // namespace experimental_denormal
