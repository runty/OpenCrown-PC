#include "runtime.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <signal.h>
#include <unistd.h>
extern "C" Memory *sub_20619220(State &, uint64_t, Memory *);
extern "C" Memory *scale_prototype(State &, uint64_t, Memory *);
extern "C" void dcp_trace_enter(State &, uint64_t) {}
Memory *dispatch(State &, uint64_t, Memory *) {throw std::runtime_error("unexpected dispatch");}
static Memory *context;static std::atomic<uint64_t> *generation;
static unsigned trigger=0,which_phase=0,changes=0,callbacks=0,current=0;
static bool callback(){uint32_t v;guestRead(*context,Memory::base+0x100+current*4,&v,4);++callbacks;return false;}
extern "C" void dcp_batch_test_poll(unsigned phase,unsigned index){
  current=index;if(phase==which_phase && index==trigger && !changes++)generation->store(2,std::memory_order_release);
}
int main(){
  alarm(10);unsigned cases=0,failures=0;
  for(unsigned index:{0u,15u,16u,31u,32u})for(unsigned phase:{0u,1u}){
    Memory m;State s{};std::atomic<uint64_t> gen{1};Memory::SignalCache cache{gen};cache.lastClear.store(1);
    m.signalCache=&cache;context=&m;generation=&gen;trigger=index;which_phase=phase;changes=callbacks=current=0;
    s.gpr.rdi.qword=Memory::base+0x300;s.gpr.rsi.qword=Memory::base+0x100;s.gpr.rdx.qword=33;s.gpr.rsp.qword=Memory::base+0xff00;
    float one=1;for(unsigned k=0;k<33;++k)std::memcpy(m.bytes.data()+0x100+k*4,&one,4);
    std::memcpy(&s.vec[0],&one,4);std::memcpy(&s.vec[1],&one,4);write64(m,s.gpr.rsp.qword,0xfeedface);m.cancelled=callback;
    scale_prototype(s,0x20619220,&m);bool ok=changes==1 && callbacks>0 && s.gpr.rip.qword==0xfeedface && m.fault.empty();
    for(unsigned k=0;k<33;++k){float f;std::memcpy(&f,m.bytes.data()+0x300+k*4,4);ok &= f==1;}
    failures+=!ok;++cases;
  }
  std::printf("chunk-boundary reentry cases=%u failures=%u\n",cases,failures);return failures?1:0;
}
