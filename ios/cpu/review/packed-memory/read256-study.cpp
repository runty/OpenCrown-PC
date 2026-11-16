// Private VMOVUPS read study; includes runtime to reuse its existing lock and
// pointer validation. This file is never linked into an app.
#include "../../runtime.cpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>
#ifndef DCP_EXTERNAL_DISPATCH
Memory *dispatch(State &,uint64_t,Memory *m){return m;}
#endif
#ifdef DCP_READ256_TEST_HOOK
static void read256_test_hook();
#endif
static bool try_read256(Memory &m,uint64_t address,uint32_t (&out)[8]) {
  auto *cache=m.signalCache;
  if(!cache)return false;
  const auto generation=cache->generation.load(std::memory_order_acquire);
  if(cache->lastClear.load(std::memory_order_relaxed)!=generation)return false;
  try {
    auto *p=checkedPointer(m,address,32);
    if(cache->generation.load(std::memory_order_acquire)!=generation)return false;
    { AccessLock lock(p,32);std::memcpy(out,p,32); }
#ifdef DCP_READ256_TEST_HOOK
    read256_test_hook(); // deterministic signal injection, outside the lock
#endif
    // Discard the speculative value and use the original per-lane sequence if
    // a stop/pause/cancel signal changed during the grouped read.
    return cache->generation.load(std::memory_order_acquire)==generation;
  } catch(const std::runtime_error &) {return false;}
}
static void original(Memory &m,uint64_t address,uint32_t (&out)[8]) {
  for(unsigned i=0;i<8;++i){float v=__remill_read_memory_f32(&m,address+i*4);std::memcpy(&out[i],&v,4);}
}
static void candidate(Memory &m,uint64_t address,uint32_t (&out)[8]) {
  uint32_t temp[8];if(try_read256(m,address,temp))std::memcpy(out,temp,32);else original(m,address,out);
}
#ifdef DCP_READ256_IR
extern "C" void dcp_read256(Memory *m,uint64_t address,void *out) {
  uint32_t result[8];candidate(*m,address,result);std::memcpy(out,result,32);
}
#endif
static std::atomic<unsigned> callbacks{0};
static bool poll(){++callbacks;return false;}
#ifndef DCP_READ256_BENCH
int main(){
  std::atomic<uint64_t> generation{1};Memory::SignalCache cache{generation};cache.lastClear.store(1);
  Memory m;m.stopOnFault=true;m.signalCache=&cache;std::mt19937 rng(0x1234);unsigned checks=0;
  for(unsigned n=0;n<10000;++n){for(unsigned i=0;i<32;++i)m.bytes[256+i]=rng();uint32_t a[8]{},b[8]{};original(m,Memory::base+256,a);candidate(m,Memory::base+256,b);if(std::memcmp(a,b,32))return 1;++checks;}
  // Partial/out-of-range accesses must take the original sequence, preserving
  // the partial result and fault text rather than committing a full read.
  for(uint64_t address:{Memory::base-4,Memory::base+65536-12,Memory::base+65536-4,Memory::base+65536}){
    Memory a,b;a.stopOnFault=b.stopOnFault=true;a.signalCache=b.signalCache=&cache;
    for(unsigned i=0;i<16;++i)a.bytes[65520+i]=b.bytes[65520+i]=uint8_t(i);
    uint32_t x[8],y[8];std::fill_n(x,8,0xdeadbeef);std::fill_n(y,8,0xdeadbeef);bool ax=false,bx=false;
    try{original(a,address,x);}catch(const std::runtime_error &){ax=true;}
    try{candidate(b,address,y);}catch(const std::runtime_error &){bx=true;}
    if(!ax||!bx||std::memcmp(x,y,32)||a.fault!=b.fault)return 2;++checks;
  }
  // A stale cache must preserve all per-lane callback polling.
  Memory p;p.stopOnFault=true;p.signalCache=&cache;p.cancelled=poll;cache.lastClear.store(0);uint32_t x[8]{},y[8]{};
  callbacks=0;original(p,Memory::base+32,x);auto a=callbacks.load();callbacks=0;candidate(p,Memory::base+32,y);auto b=callbacks.load();
  if(a!=8||b!=8||std::memcmp(x,y,32))return 3;++checks;cache.lastClear.store(1);
  // Checked bulk publication must exclude a grouped read over the same span.
  Memory shared;shared.signalCache=&cache;std::atomic<bool> go{false},bad{false};
  std::thread writer([&]{while(!go.load())std::this_thread::yield();for(uint32_t n=1;n<=20000;++n){uint32_t block[8];std::fill_n(block,8,n);guestWrite(shared,Memory::base+128,block,32);}});
  std::thread reader([&]{while(!go.load())std::this_thread::yield();for(unsigned n=0;n<20000;++n){uint32_t block[8];candidate(shared,Memory::base+128,block);for(unsigned i=1;i<8;++i)if(block[i]!=block[0])bad=true;}});
  go=true;writer.join();reader.join();if(bad)return 4;++checks;
  std::printf("PASS: %u vector reads, partial faults and stale-cache callbacks\n",checks);return 0;
}
#endif
