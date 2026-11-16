#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
struct SampleCase { uint32_t start, end, count, rounding = 0; };
inline constexpr SampleCase samples[] = {
    {0x00000000,0x3f800000,0}, {0x00000000,0x3f800000,1},
    {0x3f800000,0x40000000,2}, {0xbf800000,0x3f800000,7},
    {0x3eaaaaab,0x40400000,16}, {0x80000000,0x00000000,3},
    {0x00800000,0x00000001,7}, {0x7f800000,0x3f800000,3},
    {0x7fc01234,0x3f800000,2}, {0x7f801234,0x3f800000,2}};
inline constexpr unsigned case_count = sizeof(samples)/sizeof(*samples) + 32768;
inline SampleCase sample_at(unsigned i) {
  if (i < sizeof(samples)/sizeof(*samples)) return samples[i];
  constexpr uint32_t values[] = {0,0x80000000,1,0x80000001,0x00800000,
    0x3f800000,0x3fa00000,0xc0600000,0x7f7fffff,0xff7fffff,
    0x7f800000,0xff800000,0x7fc01234,0xffc05678,0x7f801234,0xff800001};
  unsigned j=i-sizeof(samples)/sizeof(*samples);
  return {values[(j>>4)&15],values[j&15],(j>>8)&31,(j>>13)&3};
}
inline void seedvec(uint8_t *v,unsigned which) {
  for(unsigned i=0;i<32;++i)v[i]=uint8_t(0x23 + which*37 + i*13);
}
inline uint32_t source_word(unsigned i) {
  constexpr uint32_t bits[] = {0x3f800000,0xbf000000,0x00000001,
      0x7f800000,0x00000000,0x7fc05678,0x40000000};
  return bits[i % (sizeof(bits)/sizeof(*bits))];
}
inline void show(unsigned i, uint64_t rax, uint64_t rdi_offset,
                 uint64_t rsi_offset, uint64_t rdx, uint32_t flags,
                 const uint8_t *v0, const uint8_t *v1, const uint8_t *v2,
                 const uint32_t *out, unsigned n) {
  std::printf("%u %llx %llx %llx %llx %x ",i,
      (unsigned long long)rax,(unsigned long long)rdi_offset,
      (unsigned long long)rsi_offset,(unsigned long long)rdx,flags);
  for (auto v : {v0,v1,v2}) { for(unsigned k=0;k<32;++k) std::printf("%02x",v[k]); std::printf(" "); }
  for(unsigned k=0;k<n;++k) std::printf("%08x",out[k]);
  std::puts("");
}
