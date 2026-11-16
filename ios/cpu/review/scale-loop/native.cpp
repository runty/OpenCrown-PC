#include <initializer_list>
#include "cases.h"
#include <xmmintrin.h>
struct Context {
  uint32_t *dst, *src;
  uint64_t count;
  uint32_t start, end;
  uint64_t rax, rdi, rsi, rdx, flags;
  uint8_t vec[3][32];
};
extern "C" void execute_original(Context *);
int main() {
  for(unsigned i=0;i<case_count;++i) {
    SampleCase cse=sample_at(i);
    uint32_t src[32],dst[32]{};
    for(unsigned k=0;k<32;++k)src[k]=source_word(k);
    Context c{};c.dst=dst;c.src=src;c.count=cse.count;
    c.start=cse.start;c.end=cse.end;
    for(unsigned k=0;k<3;++k)seedvec(c.vec[k],k);
    std::memcpy(c.vec[0],&c.start,4);std::memcpy(c.vec[1],&c.end,4);
    unsigned old=_mm_getcsr();_mm_setcsr(0x1f80 | (cse.rounding<<13));
    execute_original(&c);_mm_setcsr(old);
    show(i,c.rax,c.rdi-(uintptr_t)dst,c.rsi-(uintptr_t)src,c.rdx,
         (uint32_t)c.flags & 0x8c5,c.vec[0],c.vec[1],c.vec[2],dst,c.count);
  }
}
