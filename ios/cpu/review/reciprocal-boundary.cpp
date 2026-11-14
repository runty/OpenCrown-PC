#include <cstdint>
#include <cstdio>
#include <cstring>
#include <immintrin.h>
int main() {
  uint32_t firstzero = 0, lastnormal = 0, firstnegativezero = 0;
  unsigned mismatches = 0;
  for (uint32_t bits = 0x7e7ff000; bits <= 0x7e801000; ++bits) {
    uint32_t input[4] = {bits, bits | 0x80000000, bits, bits | 0x80000000},
             output[4];
    auto value =
        _mm_castsi128_ps(_mm_loadu_si128(reinterpret_cast<__m128i *>(input)));
    _mm_storeu_si128(reinterpret_cast<__m128i *>(output),
                     _mm_castps_si128(_mm_rcp_ps(value)));
    if (output[0] == 0 && !firstzero)
      firstzero = bits;
    if (output[0] != 0)
      lastnormal = bits;
    if (output[1] == 0x80000000 && !firstnegativezero)
      firstnegativezero = bits;
    float x, y;
    std::memcpy(&x, &bits, 4);
    y = 1.0f / x;
    uint32_t accurate;
    std::memcpy(&accurate, &y, 4);
    if (!(accurate & 0x7f800000))
      accurate = 0;
    if (output[0] == 0 && accurate != 0) {
      if (mismatches < 10)
        std::printf("boundary mismatch input=%08x native=%08x exact=%08x "
                    "negative=%08x\n",
                    bits, output[0], accurate, output[1]);
      ++mismatches;
    }
  }
  std::printf("positive firstzero=%08x lastnormal=%08x negative firstzero=%08x "
              "mismatches=%u\n",
              firstzero, lastnormal, firstnegativezero, mismatches);
}
