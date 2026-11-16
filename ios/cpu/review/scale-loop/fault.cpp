#include "runtime.h"
#include <cstdio>
#include <cstring>
extern "C" Memory *sub_20619220(State &, uint64_t, Memory *);
extern "C" Memory *scale_prototype(State &, uint64_t, Memory *);
extern "C" void dcp_trace_enter(State &, uint64_t) {}
Memory *dispatch(State &, uint64_t, Memory *) {throw std::runtime_error("unexpected dispatch");}
int main() {
  for(auto fn : {sub_20619220, scale_prototype}) {
    Memory m;State s{};m.stopOnFault=true;
    s.gpr.rdi.qword=Memory::base+0x100;s.gpr.rsi.qword=Memory::base+0xffff;
    s.gpr.rdx.qword=2;s.gpr.rsp.qword=Memory::base+0xff00;
    float a=1,b=2;std::memcpy(&s.vec[0],&a,4);std::memcpy(&s.vec[1],&b,4);
    write64(m,s.gpr.rsp.qword,0xfeedface);bool caught=false;
    try {fn(s,0x20619220,&m);} catch(const std::runtime_error &) {caught=true;}
    std::printf("caught=%d rip=%llx rdx=%llx src=%llx dst=%llx fault=%d\n",caught,
      (unsigned long long)s.gpr.rip.qword,(unsigned long long)s.gpr.rdx.qword,
      (unsigned long long)s.gpr.rsi.qword,(unsigned long long)s.gpr.rdi.qword,!m.fault.empty());
  }
}
