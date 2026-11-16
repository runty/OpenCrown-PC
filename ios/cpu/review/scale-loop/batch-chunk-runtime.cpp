// Isolated batch research prototype. Including the runtime keeps its private
// locks and snapshot invalidation visible; this file is never in app builds.
#include "../../runtime.cpp"
#include <optional>
#ifndef DCP_BATCH_CHUNK
#define DCP_BATCH_CHUNK 16
#endif
static_assert(DCP_BATCH_CHUNK > 0 && DCP_BATCH_CHUNK <= 4096);
#ifdef DCP_BATCH_TEST_POLL
extern "C" void dcp_batch_test_poll(unsigned,unsigned);
#endif
#ifdef DCP_BATCH_INTEGRATION
extern "C" Memory *dcp_original_scale(State &, uint64_t, Memory *);
extern "C" void dcp_scale_noop_trace(State &, uint64_t) {}
#define DCP_SCALE_FALLBACK dcp_original_scale
#else
extern "C" Memory *sub_20619220(State &, uint64_t, Memory *);
#define DCP_SCALE_FALLBACK sub_20619220
#endif
#include "runtime.h"
#include <cstdint>
#include <cstring>
extern "C" float __remill_read_memory_f32(Memory *, uint64_t);
extern "C" uint64_t __remill_read_memory_64(Memory *, uint64_t);
extern "C" Memory *__remill_write_memory_f32(Memory *, uint64_t, float);
extern "C" Memory *__remill_function_return(State &, uint64_t, Memory *);
static float binary(float a, float b, char op) {
  uint32_t l,r,z;float value;
  std::memcpy(&l,&a,4);std::memcpy(&r,&b,4);
  if(op=='+') value=a+b; else if(op=='-') value=a-b;
  else if(op=='*') value=a*b; else value=a/b;
  std::memcpy(&z,&value,4);
  if((l&0x7fffffff)>0x7f800000) z=l|0x00400000;
  else if((r&0x7fffffff)>0x7f800000) z=r|0x00400000;
  else if((z&0x7fffffff)>0x7f800000) z=0xffc00000;
  std::memcpy(&value,&z,4);return value;
}
static void update_vec(State &s,unsigned dst,unsigned src,float value) {
  uint8_t temp[16];std::memcpy(temp,&s.vec[src],16);
  std::memcpy(temp,&value,4);std::memcpy(&s.vec[dst],temp,16);
  std::memset(reinterpret_cast<uint8_t*>(&s.vec[dst])+16,0,16);
}
static uint8_t parity(uint8_t x) { return !(__builtin_popcount(unsigned(x)) & 1); }
static void add4_flags(State &s,uint64_t value) {
  uint64_t result=value+4;
  s.aflag.cf=result<value;s.aflag.pf=parity(uint8_t(result));
  s.aflag.af=((value ^ 4 ^ result)&16)!=0;
  s.aflag.zf=result==0;s.aflag.sf=result>>63;
  s.aflag.of=((~(value^uint64_t(4)) & (value^result))>>63)!=0;
}
static void dec_flags(State &s,uint32_t old) {
  uint32_t value=old-1;
  s.aflag.pf=parity(uint8_t(value));s.aflag.af=((old^1^value)&16)!=0;
  s.aflag.zf=value==0;s.aflag.sf=value>>31;s.aflag.of=old==0x80000000u;
}
#ifdef DCP_BATCH_STATS
#include <cstdio>
static std::atomic<uint64_t> batch_calls{0},batch_fast{0},batch_fallback{0},batch_elements{0},batch_slow_polls{0};
struct DcpBatchStatsPrinter { ~DcpBatchStatsPrinter() {
  std::fprintf(stderr,"BATCH_STATS calls=%llu fast=%llu fallback=%llu elements=%llu slow_polls=%llu\n",
    (unsigned long long)batch_calls.load(),(unsigned long long)batch_fast.load(),
    (unsigned long long)batch_fallback.load(),(unsigned long long)batch_elements.load(),
    (unsigned long long)batch_slow_polls.load());
}} dcp_batch_stats_printer;
extern "C" uint64_t dcp_batch_fast_calls() { return batch_fast.load(std::memory_order_relaxed); }
extern "C" uint64_t dcp_batch_fallback_calls() { return batch_fallback.load(std::memory_order_relaxed); }
extern "C" uint64_t dcp_batch_element_count() { return batch_elements.load(std::memory_order_relaxed); }
#define DCP_BATCH_INC(x,v) x.fetch_add(v,std::memory_order_relaxed)
#else
#define DCP_BATCH_INC(x,v) ((void)0)
#endif
extern "C" Memory *scale_prototype(State &s,uint64_t,Memory *m) {
  uint32_t count=s.gpr.rdx.dword;
  DCP_BATCH_INC(batch_calls,1);
  // Only masked exceptions with DAZ/FTZ disabled have been exercised. Preserve
  // the original trace for other guest MXCSR controls, including reserved bits.
  const uint32_t mxcsr=s.x87.fxsave.mxcsr.flat;
  if ((mxcsr & 0xffff0000u) || (mxcsr & 0x8040u) ||
      (mxcsr & 0x1f80u) != 0x1f80u) {
    DCP_BATCH_INC(batch_fallback,1);
    return DCP_SCALE_FALLBACK(s,0x20619220,m);
  }
  // Generic callbacks are allowed to reenter guest memory and run outside
  // the stripe locks. Only take the fast path when the same signal cache
  // that makes the original check a no-op is already clear.
  auto *cache=m->signalCache;
  if (!cache || cache->generation.load(std::memory_order_acquire) !=
                    cache->lastClear.load(std::memory_order_relaxed)) {
    DCP_BATCH_INC(batch_fallback,1);
    return DCP_SCALE_FALLBACK(s,0x20619220,m);
  }
  uint8_t *src=nullptr,*dst=nullptr;
  if (count && count <= 4096 &&
      s.gpr.rsi.qword <= UINT64_MAX - uint64_t(count)*4 &&
      s.gpr.rdi.qword <= UINT64_MAX - uint64_t(count)*4) {
    try {
      src=checkedPointer(*m,s.gpr.rsi.qword,size_t(count)*4);
      dst=checkedPointer(*m,s.gpr.rdi.qword,size_t(count)*4);
    } catch (const std::runtime_error &) {
      DCP_BATCH_INC(batch_fallback,1);
      return DCP_SCALE_FALLBACK(s,0x20619220,m);
    }
  } else if (count) {
    DCP_BATCH_INC(batch_fallback,1);
    return DCP_SCALE_FALLBACK(s,0x20619220,m);
  }
  DCP_BATCH_INC(batch_fast,1);DCP_BATCH_INC(batch_elements,count);
  s.gpr.rax.qword=count;
  s.aflag.cf=0;s.aflag.pf=parity(uint8_t(count));s.aflag.zf=count==0;
  s.aflag.sf=!!(count&0x80000000);s.aflag.of=0;s.aflag.af=0;
  if(count) {
    float start,end;std::memcpy(&start,&s.vec[0],4);std::memcpy(&end,&s.vec[1],4);
    float delta=binary(end,start,'-');update_vec(s,1,1,delta);
    float converted=static_cast<float>(uint64_t(count));update_vec(s,2,0,converted);
    delta=binary(delta,converted,'/');update_vec(s,1,1,delta);
    { std::optional<AccessLock> lock;
    auto poll_and_lock = [&](unsigned phase,unsigned index) {
#ifdef DCP_BATCH_TEST_POLL
      dcp_batch_test_poll(phase,index);
#else
      (void)phase;(void)index;
#endif
      const uint32_t begin=(index/DCP_BATCH_CHUNK)*DCP_BATCH_CHUNK;
      const uint32_t size=std::min<uint32_t>(DCP_BATCH_CHUNK,count-begin);
      if (!lock) {
        checkCancelled(*m);
        lock.emplace(src+size_t(begin)*4,dst+size_t(begin)*4,size_t(size)*4);
      } else if (cache->generation.load(std::memory_order_acquire) !=
                 cache->lastClear.load(std::memory_order_relaxed)) {
        lock.reset();
        DCP_BATCH_INC(batch_slow_polls,1);
        checkCancelled(*m);
        lock.emplace(src+size_t(begin)*4,dst+size_t(begin)*4,size_t(size)*4);
      }
    };
    for(uint32_t i=0;i<count;++i) {
      s.gpr.rip.qword=0x20619240;
      poll_and_lock(0,i);
      float source;std::memcpy(&source,src+size_t(i)*4,4);
      float result=binary(start,source,'*');update_vec(s,2,0,result);
      start=binary(delta,start,'+');update_vec(s,0,1,start);
      add4_flags(s,s.gpr.rsi.qword);s.gpr.rsi.qword+=4;
      s.gpr.rip.qword=0x2061924c;
      poll_and_lock(1,i);
      changed(dst+size_t(i)*4,4);std::memcpy(dst+size_t(i)*4,&result,4);
      add4_flags(s,s.gpr.rdi.qword);s.gpr.rdi.qword+=4;
      dec_flags(s,count-i);s.gpr.rdx.qword=count-i-1;
      if ((i+1)%DCP_BATCH_CHUNK==0) lock.reset();
    }}
  }
  s.gpr.rip.qword=0x20619258;
  uint64_t ret=__remill_read_memory_64(m,s.gpr.rsp.qword);s.gpr.rsp.qword+=8;s.gpr.rip.qword=ret;
  return __remill_function_return(s,ret,m);
}
