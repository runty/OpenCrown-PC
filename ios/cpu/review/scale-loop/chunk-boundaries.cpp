#include "runtime.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
extern "C" Memory *sub_20619220(State &, uint64_t, Memory *);
extern "C" Memory *scale_prototype(State &, uint64_t, Memory *);
extern "C" void dcp_trace_enter(State &, uint64_t) {}
Memory *dispatch(State &, uint64_t, Memory *) {throw std::runtime_error("unexpected dispatch");}
int main(){
  constexpr size_t band=256*1024,total=1024*1024;
  void *allocation=nullptr;if(posix_memalign(&allocation,band,total))return 2;
  auto *bytes=static_cast<uint8_t*>(allocation);Memory m;m.bytes.clear();
  addRegion(m,bytes,total);bindScalarThread(m);
  std::atomic<uint64_t> generation{1};Memory::SignalCache cache{generation};cache.lastClear.store(1);m.signalCache=&cache;
  const uint64_t base=reinterpret_cast<uint64_t>(bytes);unsigned cases=0,failures=0;
  for(unsigned count:{15u,16u,17u,31u,32u,33u}) for(unsigned mode=0;mode<3;++mode){
    State saved{};std::vector<uint8_t> expected;unsigned run=0;
    const uint64_t src=base+band-32;
    const uint64_t dst=mode==0?base+2*band-32:mode==1?src+4:src-4;
    for(auto fn:{sub_20619220,scale_prototype}){
      std::memset(bytes,0,total);
      for(unsigned i=0;i<40;++i){float f=float(i+1)/8;std::memcpy(reinterpret_cast<void*>(src+i*4),&f,4);}
      State s{};s.gpr.rsi.qword=src;s.gpr.rdi.qword=dst;s.gpr.rdx.qword=count;s.gpr.rsp.qword=base+3*band;
      float a=0.5f,b=1.5f;std::memcpy(&s.vec[0],&a,4);std::memcpy(&s.vec[1],&b,4);
      m.fault.clear();m.returns=0;write64(m,s.gpr.rsp.qword,0xfeedface);
      fn(s,0x20619220,&m);
      if(!run++){saved=s;expected.assign(bytes,bytes+total);}
      else {bool same=!std::memcmp(&saved,&s,sizeof(State)) && std::equal(expected.begin(),expected.end(),bytes) && m.fault.empty() && m.returns==1;failures+=!same;}
    }
    ++cases;
  }
  std::printf("chunk/stripe/alias cases=%u failures=%u\n",cases,failures);
  std::free(allocation);return failures?1:0;
}
