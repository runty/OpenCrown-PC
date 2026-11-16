#include <cstdint>
#include <cstdio>
#include <cstring>
#include <xmmintrin.h>
struct Context {
  uint32_t *dst,*src; uint64_t count; uint32_t start,end;
  uint64_t rax,rdi,rsi,rdx,flags; uint8_t vec[3][32];
};
extern "C" void execute_original(Context *);
int main() {
  const uint32_t values[]={0,0x80000000,1,0x80000001,0x00800000,0x3f800000,0x3fa00000,0xc0600000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0xffc05678,0x7f801234,0xff800001};
  const unsigned old=_mm_getcsr();
  for(uint32_t a:values)for(uint32_t b:values)for(uint32_t c:values) {
    uint32_t out=0; Context ctx{}; ctx.dst=&out;ctx.src=&c;ctx.count=1;
    std::memcpy(ctx.vec[0],&a,4);std::memcpy(ctx.vec[1],&b,4);
    asm volatile("fnclex" ::: "memory");
    _mm_setcsr(0x1f80); execute_original(&ctx); unsigned flags=_mm_getcsr()&0x3f;
    uint16_t x87;asm volatile("fnstsw %0":"=am"(x87)::"memory");
    std::printf("%08x %08x %08x %02x %02x\n",a,b,c,flags,x87&0x3f);
  }
  // Changing only the upper lane must not raise an x86 scalar exception.
  uint32_t in=0,out=0,upper=0x3f800000;Context check{};
  check.dst=&out;check.src=&in;check.count=1;
  std::memcpy(check.vec[0]+4,&upper,4);std::memcpy(check.vec[1]+4,&upper,4);
  asm volatile("fnclex" ::: "memory");_mm_setcsr(0x1f80);execute_original(&check);
  unsigned upper_flags=_mm_getcsr()&0x3f;uint16_t upper_x87;
  asm volatile("fnstsw %0":"=am"(upper_x87)::"memory");
  _mm_setcsr(old);
  return (upper_flags || (upper_x87&0x3f)) ? 1 : 0;
}
