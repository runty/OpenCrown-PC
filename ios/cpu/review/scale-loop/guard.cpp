#include "runtime.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
extern "C" Memory *sub_20619220(State &, uint64_t, Memory *);
extern "C" Memory *scale_prototype(State &, uint64_t, Memory *);
extern "C" void dcp_trace_enter(State &, uint64_t) {}
Memory *dispatch(State &, uint64_t, Memory *) {throw std::runtime_error("unexpected dispatch");}
static unsigned hooks=0;
extern "C" void dcp_batch_test_poll(unsigned,unsigned){++hooks;}
struct Test {uint32_t mxcsr;unsigned cache;bool fast;};
int main(){
  const Test tests[]={{0x1f80,1,true},{0x3f81,1,true},{0x5fa0,1,true},{0x7f80,1,true},
    {0x1fc0,1,false},{0x9f80,1,false},{0x1f00,1,false},{0x11f80,1,false},
    {0,1,false},{0x1f80,0,false},{0x1f80,2,false}};
  unsigned failures=0;
  for(auto test:tests){
    State ref{};std::vector<uint8_t> bytes;unsigned mode=0;
    for(auto fn:{sub_20619220,scale_prototype}){
      Memory m;State s{};std::atomic<uint64_t> generation{1};Memory::SignalCache cache{generation};
      cache.lastClear.store(test.cache==2?0:1);if(test.cache)m.signalCache=&cache;
      s.x87.fxsave.mxcsr.flat=test.mxcsr;s.gpr.rsi.qword=Memory::base+0x100;
      s.gpr.rdi.qword=Memory::base+0x200;s.gpr.rdx.qword=2;s.gpr.rsp.qword=Memory::base+0xff00;
      float a=1,b=2;std::memcpy(&s.vec[0],&a,4);std::memcpy(&s.vec[1],&b,4);
      write64(m,s.gpr.rsp.qword,0xfeedface);hooks=0;fn(s,0x20619220,&m);
      if(!mode++){ref=s;bytes=m.bytes;}
      else {bool ok=!std::memcmp(&ref,&s,sizeof(State)) && bytes==m.bytes &&
              ((hooks==4)==test.fast) && m.fault.empty();
        std::printf("mxcsr=%08x cache=%u hooks=%u expected_fast=%d same=%d\n",test.mxcsr,test.cache,hooks,test.fast,ok);
        failures+=!ok;}
    }
  }
  std::printf("guard cases=%zu failures=%u\n",sizeof(tests)/sizeof(*tests),failures);return failures?1:0;
}
