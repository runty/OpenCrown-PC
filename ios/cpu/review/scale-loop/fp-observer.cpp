// The real exception bridge is linked, with x87 softfloat state stubbed clear
// to isolate the effect of the host FPSR. This is a host-only investigation.
#include "runtime.h"
#include <cstdio>
#include <cstring>
extern "C" Memory *sub_20619220(State &,uint64_t,Memory *);
extern "C" Memory *scale_prototype(State &,uint64_t,Memory *);
extern "C" int32_t __remill_fpu_exception_test(int32_t);
extern "C" uint32_t dcp_fp80_x87_exception_flags() { return 0; }
extern "C" void dcp_fp80_clear_x87_exceptions(uint32_t) {}
extern "C" void dcp_trace_enter(State &,uint64_t) {}
Memory *dispatch(State &,uint64_t,Memory *) {throw std::runtime_error("unexpected dispatch");}
static uint64_t get_fpsr(){uint64_t x;asm volatile("mrs %0, fpsr":"=r"(x));return x;}
static void set_fpsr(uint64_t x){asm volatile("msr fpsr, %0"::"r"(x):"memory");}
int main(){
  const auto saved=get_fpsr();unsigned failures=0;
  for(unsigned lane=0;lane<2;++lane) {
    unsigned flags[2]{},fpsr[2]{},csr[2]{},mode=0;
    for(auto fn:{sub_20619220,scale_prototype}) {
      Memory m;State s{};std::atomic<uint64_t> gen{1};Memory::SignalCache cache{gen};cache.lastClear.store(1);m.signalCache=&cache;
      s.x87.fxsave.mxcsr.flat=0x1f80;s.gpr.rdi.qword=Memory::base+0x200;s.gpr.rsi.qword=Memory::base+0x100;s.gpr.rdx.qword=1;s.gpr.rsp.qword=Memory::base+0xff00;
      uint32_t upper=lane ? 0x3f800000 : 0;
      std::memcpy(reinterpret_cast<char*>(&s.vec[0])+4,&upper,4);
      std::memcpy(reinterpret_cast<char*>(&s.vec[1])+4,&upper,4);
      write64(m,s.gpr.rsp.qword,0xfeedface);
      set_fpsr(0);fn(s,0x20619220,&m);fpsr[mode]=get_fpsr()&0x9f;
      flags[mode]=__remill_fpu_exception_test(1);csr[mode]=s.x87.fxsave.mxcsr.flat;++mode;
    }
    std::printf("upper=%u original FPSR=%02x helper=%u mxcsr=%04x; prototype FPSR=%02x helper=%u mxcsr=%04x\n",lane,fpsr[0],flags[0],csr[0],fpsr[1],flags[1],csr[1]);
    if(fpsr[0]!=(lane?0u:1u)||flags[0]!=(lane?0u:1u)||fpsr[1]||flags[1]||csr[0]!=0x1f80||csr[1]!=0x1f80)++failures;
  }
  set_fpsr(saved);return failures?1:0;
}
