#include "runtime.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
extern "C" Memory *sub_20619220(State &, uint64_t, Memory *);
extern "C" Memory *scale_prototype(State &, uint64_t, Memory *);
extern "C" void dcp_trace_enter(State &, uint64_t) {}
Memory *dispatch(State &, uint64_t, Memory *) {throw std::runtime_error("unexpected dispatch");}
static unsigned checks=0, cancel_at=0;
static bool cancel(){return ++checks==cancel_at;}
struct Outcome { State s{};std::vector<uint8_t> bytes;std::string fault;bool caught=false;unsigned returns=0, polls=0;};
int main(){
  unsigned failures=0;
  for(unsigned which=0;which<19;++which){
    Outcome out[2];unsigned mode=0;
    for(auto fn:{sub_20619220,scale_prototype}){
      Memory m;State s{};s.x87.fxsave.mxcsr.flat=0x1f80;m.stopOnFault=true;
#ifdef DCP_BATCH_TEST
      std::atomic<uint64_t> generation{1};Memory::SignalCache cache{generation};
      cache.lastClear.store(1);
      if (which < 7 || which >= 12) m.signalCache=&cache;
#endif
      s.gpr.rdi.qword=(which==15 || which==18)?UINT64_MAX-1:Memory::base+(which==3?0xffff:which==5?0xfffc:which==12?0x104:which==13?0x100:0x200);
      s.gpr.rsi.qword=(which==15 || which==17)?UINT64_MAX-1:Memory::base+(which==2?0xffff:which==4?0xfffc:which==13?0x104:0x100);
      s.gpr.rdx.qword=(which==0 || which==15)?0:which==14?4097:(which==12 || which==13)?7:2;
      s.gpr.rsp.qword=Memory::base+(which==6?0xffff:which==16?0x200:0xff00);
      float a=1,b=2;std::memcpy(&s.vec[0],&a,4);std::memcpy(&s.vec[1],&b,4);
      if(which>=12) for(unsigned k=0;k<8;++k){float v=float(k+1);std::memcpy(m.bytes.data()+0x100+k*4,&v,4);}
      if(which!=6)write64(m,s.gpr.rsp.qword,0xfeedface);
      checks=0;cancel_at=which>=7 && which<=11?which-6:0;if(cancel_at)m.cancelled=cancel;
      try{fn(s,0x20619220,&m);}catch(const std::runtime_error &){out[mode].caught=true;}
      out[mode].s=s;out[mode].bytes=m.bytes;out[mode].fault=m.fault;
      out[mode].returns=m.returns;out[mode].polls=checks;++mode;
    }
    auto a=reinterpret_cast<const uint8_t*>(&out[0].s);auto b=reinterpret_cast<const uint8_t*>(&out[1].s);
    unsigned diffs=0;std::printf("case=%u caught=%d/%d offsets=",which,out[0].caught,out[1].caught);
    for(unsigned i=0;i<sizeof(State);++i)if(a[i]!=b[i]){if(diffs<20)std::printf("%u:%02x/%02x,",i,a[i],b[i]);++diffs;}
    bool same=diffs==0 && out[0].caught==out[1].caught && out[0].bytes==out[1].bytes && out[0].fault==out[1].fault && out[0].returns==out[1].returns && out[0].polls==out[1].polls;
    std::printf(" count=%u bytes=%d fault=%d returns=%u/%u polls=%u/%u same=%d\n",diffs,out[0].bytes==out[1].bytes,out[0].fault==out[1].fault,out[0].returns,out[1].returns,out[0].polls,out[1].polls,same);
    failures+=!same;
  }
  std::printf("failures=%u\n",failures);return failures?1:0;
}
