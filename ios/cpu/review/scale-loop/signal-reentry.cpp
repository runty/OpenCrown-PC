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
static Memory *context;
static std::atomic<uint64_t> *generation;
static unsigned callbacks=0,changes=0;
static bool callback() {
  uint32_t value;
  guestRead(*context,Memory::base+0x100,&value,4);
  ++callbacks;return false;
}
extern "C" void dcp_batch_test_poll(unsigned phase,unsigned index) {
  if(phase==1 && index==0 && !changes++) generation->store(2,std::memory_order_release);
}
int main(){
  alarm(5);Memory m;State s{};s.x87.fxsave.mxcsr.flat=0x1f80;context=&m;
  std::atomic<uint64_t> gen{1};generation=&gen;Memory::SignalCache cache{gen};
  cache.lastClear.store(1);m.signalCache=&cache;
  s.gpr.rdi.qword=Memory::base+0x200;s.gpr.rsi.qword=Memory::base+0x100;
  s.gpr.rdx.qword=2;s.gpr.rsp.qword=Memory::base+0xff00;
  float a=1,b=2;std::memcpy(&s.vec[0],&a,4);std::memcpy(&s.vec[1],&b,4);
  write64(m,s.gpr.rsp.qword,0xfeedface);m.cancelled=callback;
  scale_prototype(s,0x20619220,&m);
  if(callbacks<4 || s.gpr.rip.qword!=0xfeedface || !m.fault.empty()) return 1;
  std::printf("generation change=%u reentrant callbacks=%u return=ok\n",changes,callbacks);
}
