#include "runtime.h"
#include <cfenv>
#include <cstdio>
#include <cstring>
extern "C" Memory *sub_20619220(State &, uint64_t, Memory *);
extern "C" Memory *scale_prototype(State &, uint64_t, Memory *);
extern "C" void dcp_trace_enter(State &, uint64_t) {}
Memory *dispatch(State &, uint64_t, Memory *) {throw std::runtime_error("unexpected dispatch");}
static uint64_t fpsr(){uint64_t x;asm volatile("mrs %0, fpsr":"=r"(x));return x;}
static void setfpsr(uint64_t x){asm volatile("msr fpsr, %0"::"r"(x):"memory");}
int main(){
  const uint32_t values[]={0,0x80000000,1,0x80000001,0x00800000,0x3f800000,0x3fa00000,0xc0600000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0xffc05678,0x7f801234,0xff800001};
  uint64_t saved=fpsr();unsigned cases=0,mismatch=0;
  for(unsigned a=0;a<16;++a)for(unsigned b=0;b<16;++b)for(unsigned c=0;c<16;++c){
    uint64_t status[2]{};unsigned mode=0;
    for(auto fn:{sub_20619220,scale_prototype}){
      Memory m;State s{};std::atomic<uint64_t> gen{1};Memory::SignalCache cache{gen};cache.lastClear.store(1);m.signalCache=&cache;
      s.x87.fxsave.mxcsr.flat=0x1f80;s.gpr.rdi.qword=Memory::base+0x200;s.gpr.rsi.qword=Memory::base+0x100;s.gpr.rdx.qword=1;s.gpr.rsp.qword=Memory::base+0xff00;
      std::memcpy(&s.vec[0],&values[a],4);std::memcpy(&s.vec[1],&values[b],4);std::memcpy(m.bytes.data()+0x100,&values[c],4);write64(m,s.gpr.rsp.qword,0xfeedface);
      setfpsr(0);fn(s,0x20619220,&m);status[mode++]=fpsr()&0x9f;
    }
    if(status[0]!=status[1]){if(mismatch<12)std::printf("start=%08x end=%08x input=%08x original=%02llx prototype=%02llx\n",values[a],values[b],values[c],(unsigned long long)status[0],(unsigned long long)status[1]);++mismatch;}
    ++cases;
  }
  setfpsr(saved);std::printf("host FPSR cases=%u mismatches=%u\n",cases,mismatch);return mismatch?1:0;
}
