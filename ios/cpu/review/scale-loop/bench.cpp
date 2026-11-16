#include "runtime.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
extern "C" Memory *sub_20619220(State &, uint64_t, Memory *);
extern "C" Memory *scale_prototype(State &, uint64_t, Memory *);
extern "C" void dcp_trace_enter(State &, uint64_t) {}
Memory *dispatch(State &, uint64_t, Memory *) {throw std::runtime_error("unexpected dispatch");}
static double trial(Lifted fn, unsigned repeats, unsigned count) {
  Memory m;State s{};bindScalarThread(m);
  for(unsigned i=0;i<count;++i){float x=float(i)/256;std::memcpy(m.bytes.data()+0x1000+i*4,&x,4);}
  write64(m,Memory::base+0xff00,0xfeedface);
  const float a=0.25f,b=0.75f;auto start=std::chrono::steady_clock::now();
  for(unsigned i=0;i<repeats;++i){
    s.gpr.rsi.qword=Memory::base+0x1000;s.gpr.rdi.qword=Memory::base+0x4000;
    s.gpr.rdx.qword=count;s.gpr.rsp.qword=Memory::base+0xff00;
    std::memcpy(&s.vec[0],&a,4);std::memcpy(&s.vec[1],&b,4);
    fn(s,0x20619220,&m);
  }
  auto end=std::chrono::steady_clock::now();if(!m.fault.empty())throw std::runtime_error(m.fault);
  return std::chrono::duration<double,std::nano>(end-start).count()/repeats;
}
int main() {
  constexpr unsigned reps=5000;
  for(unsigned count : {1u,4u,16u,64u,256u}) {
    (void)trial(sub_20619220,100,count);(void)trial(scale_prototype,100,count);
    for(unsigned i=0;i<6;++i){
      double a,b;
      if(i&1){b=trial(scale_prototype,reps,count);a=trial(sub_20619220,reps,count);}
      else {a=trial(sub_20619220,reps,count);b=trial(scale_prototype,reps,count);}
      std::printf("%u %u %.3f %.3f\n",count,i,a,b);std::fflush(stdout);
    }
  }
}
