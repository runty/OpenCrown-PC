#include <initializer_list>
#include "cases.h"
#include "runtime.h"
#include <cfenv>
extern "C" Memory *sub_20619220(State &, uint64_t, Memory *);
extern "C" Memory *scale_prototype(State &, uint64_t, Memory *);
extern "C" void dcp_trace_enter(State &, uint64_t) {}
Memory *dispatch(State &, uint64_t, Memory *) {
  throw std::runtime_error("unexpected dispatch");
}
int main() {
  for(unsigned i=0;i<case_count;++i) {
    SampleCase cse=sample_at(i);
    Memory m;State s{};s.x87.fxsave.mxcsr.flat=0x1f80 | (cse.rounding<<13);
#ifdef DCP_BATCH_TEST
    std::atomic<uint64_t> generation{1};Memory::SignalCache cache{generation};
    cache.lastClear.store(1);m.signalCache=&cache;
#endif
    const uint64_t src=Memory::base+0x100,dst=Memory::base+0x200;
    for(unsigned k=0;k<32;++k) {uint32_t x=source_word(k);std::memcpy(m.bytes.data()+0x100+k*4,&x,4);}
    s.gpr.rsi.qword=src;s.gpr.rdi.qword=dst;s.gpr.rdx.qword=cse.count;
    s.gpr.rsp.qword=Memory::base+0xff00;
    for(unsigned k=0;k<3;++k)seedvec(reinterpret_cast<uint8_t*>(&s.vec[k]),k);
    std::memcpy(&s.vec[0],&cse.start,4);
    std::memcpy(&s.vec[1],&cse.end,4);
    write64(m,s.gpr.rsp.qword,0xfeedface);
    std::fenv_t old;std::fegetenv(&old);
    const int rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    std::fesetround(rounds[cse.rounding]);std::feclearexcept(FE_ALL_EXCEPT);
#ifdef DCP_SCALE_PROTOTYPE
    scale_prototype(s,0x20619220,&m);
#else
    sub_20619220(s,0x20619220,&m);
#endif
    std::fesetenv(&old);
    if(!m.fault.empty())return 3;
    uint32_t flags=s.aflag.cf | (s.aflag.pf<<2) | (s.aflag.zf<<6) | (s.aflag.sf<<7) | (s.aflag.of<<11);
    uint32_t out[32];std::memcpy(out,m.bytes.data()+0x200,sizeof(out));
    show(i,s.gpr.rax.qword,s.gpr.rdi.qword-dst,s.gpr.rsi.qword-src,s.gpr.rdx.qword,flags,
         reinterpret_cast<uint8_t*>(&s.vec[0]),reinterpret_cast<uint8_t*>(&s.vec[1]),reinterpret_cast<uint8_t*>(&s.vec[2]),out,cse.count);
  }
}
