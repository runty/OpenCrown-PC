#include <stdint.h>
#include <string.h>
// The explicit x87 J bit is significand bit63, not part of the fraction.
uint32_t integer144(const void *pointer) {
  uint64_t significand;
  uint16_t signExponent;
  memcpy(&significand, pointer, 8);
  memcpy(&signExponent, (const uint8_t *)pointer + 8, 2);
  return (signExponent & 0x7fff) == 0 && significand != 0 &&
         (significand >> 63) == 0;
}
