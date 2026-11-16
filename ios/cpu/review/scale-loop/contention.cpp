#include "runtime.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
extern "C" Memory *sub_20619220(State &, uint64_t, Memory *);
extern "C" Memory *scale_prototype(State &, uint64_t, Memory *);
extern "C" Memory *__remill_atomic_begin(Memory *);
extern "C" Memory *__remill_atomic_end(Memory *);
extern "C" void dcp_trace_enter(State &, uint64_t) {}
Memory *dispatch(State &, uint64_t, Memory *) {throw std::runtime_error("unexpected dispatch");}
using Clock=std::chrono::steady_clock;
static uint64_t ns(Clock::duration d){return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();}
static uint64_t quantile(std::vector<uint64_t> &v,unsigned percent){
  if(v.empty())return 0;std::sort(v.begin(),v.end());return v[(v.size()-1)*percent/100];
}
static bool trial(const char *name,Lifted fn,unsigned iterations,unsigned serial){
  std::vector<uint8_t> data(65536);Memory owner,worker,reader;
  addRegion(owner,data.data(),data.size());worker.regions=owner.regions;reader.regions=owner.regions;
  const uint64_t base=reinterpret_cast<uint64_t>(data.data());
  for(unsigned i=0;i<256;++i){float f=float(i+1);std::memcpy(data.data()+0x1000+i*4,&f,4);}
  write64(worker,base+0xff00,0xfeedface);
  std::atomic<uint64_t> generation{1};Memory::SignalCache cache{generation};cache.lastClear.store(1);worker.signalCache=&cache;
  std::atomic<bool> ready{false},start{false},done{false},failed{false};
  std::vector<uint64_t> reads,atomics;reads.reserve(iterations*2);atomics.reserve(iterations/16+1);
  std::thread observer([&]{
    bindScalarThread(reader);ready.store(true,std::memory_order_release);
    while(!start.load(std::memory_order_acquire))std::this_thread::yield();
    unsigned k=0;
    while(!done.load(std::memory_order_acquire)){
      auto t=Clock::now();uint32_t value=0;guestRead(reader,base+0x4000,&value,4);reads.push_back(ns(Clock::now()-t));
      if((++k%32)==0){t=Clock::now();__remill_atomic_begin(&reader);__remill_atomic_end(&reader);atomics.push_back(ns(Clock::now()-t));}
    }
  });
  while(!ready.load(std::memory_order_acquire))std::this_thread::yield();
  State s{};bindScalarThread(worker);const float a=1,b=1;
  start.store(true,std::memory_order_release);auto begin=Clock::now();
  try {
    for(unsigned i=0;i<iterations;++i){
      s.gpr.rsi.qword=base+0x1000;s.gpr.rdi.qword=base+0x4000;
      s.gpr.rdx.qword=256;s.gpr.rsp.qword=base+0xff00;
      std::memcpy(&s.vec[0],&a,4);std::memcpy(&s.vec[1],&b,4);
      fn(s,0x20619220,&worker);
    }
  }catch(...){failed=true;}
  const auto elapsed=ns(Clock::now()-begin);done.store(true,std::memory_order_release);observer.join();
  bool valid=!failed && worker.fault.empty() && s.gpr.rip.qword==0xfeedface;
  for(unsigned i=0;i<256;++i){float a1,b1;std::memcpy(&a1,data.data()+0x1000+i*4,4);std::memcpy(&b1,data.data()+0x4000+i*4,4);valid &= a1==b1;}
  const auto nr=reads.size(),na=atomics.size();
  const auto r50=quantile(reads,50),r99=quantile(reads,99),rmax=reads.empty()?0:reads.back();
  const auto a50=quantile(atomics,50),a99=quantile(atomics,99),amax=atomics.empty()?0:atomics.back();
  std::printf("%s %u loops=%u wall_ns=%llu read_n=%zu read_p50_ns=%llu read_p99_ns=%llu read_max_ns=%llu atomic_n=%zu atomic_p50_ns=%llu atomic_p99_ns=%llu atomic_max_ns=%llu valid=%d\n",name,serial,iterations,(unsigned long long)elapsed,nr,(unsigned long long)r50,(unsigned long long)r99,(unsigned long long)rmax,na,(unsigned long long)a50,(unsigned long long)a99,(unsigned long long)amax,valid);
  std::fflush(stdout);return valid && nr && na;
}
int main(int argc,char **argv){
  const unsigned iterations=argc>1?unsigned(std::strtoul(argv[1],nullptr,10)):20000;
  bool ok=true;
  for(unsigned i=0;i<4;++i){
    if(i&1){ok &= trial("batch",scale_prototype,iterations,i);ok &= trial("original",sub_20619220,iterations,i);}
    else {ok &= trial("original",sub_20619220,iterations,i);ok &= trial("batch",scale_prototype,iterations,i);}
  }
  return ok?0:1;
}
