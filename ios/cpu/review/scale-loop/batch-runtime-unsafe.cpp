// Isolated batch research prototype. Including the runtime keeps its private
// locks and snapshot invalidation visible; this file is never in app builds.
#include "../../runtime.cpp"
extern "C" Memory *sub_20619220(State &, uint64_t, Memory *);
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
extern "C" Memory *scale_prototype(State &s,uint64_t,Memory *m) {
  uint32_t count=s.gpr.rdx.dword;
  uint8_t *src=nullptr,*dst=nullptr;
  if (count && count <= 4096 &&
      s.gpr.rsi.qword <= UINT64_MAX - uint64_t(count)*4 &&
      s.gpr.rdi.qword <= UINT64_MAX - uint64_t(count)*4) {
    try {
      src=checkedPointer(*m,s.gpr.rsi.qword,size_t(count)*4);
      dst=checkedPointer(*m,s.gpr.rdi.qword,size_t(count)*4);
    } catch (const std::runtime_error &) {
      return sub_20619220(s,0x20619220,m);
    }
  } else if (count) return sub_20619220(s,0x20619220,m);
  s.gpr.rax.qword=count;
  s.aflag.cf=0;s.aflag.pf=parity(uint8_t(count));s.aflag.zf=count==0;
  s.aflag.sf=!!(count&0x80000000);s.aflag.of=0;s.aflag.af=0;
  if(count) {
    float start,end;std::memcpy(&start,&s.vec[0],4);std::memcpy(&end,&s.vec[1],4);
    float delta=binary(end,start,'-');update_vec(s,1,1,delta);
    float converted=static_cast<float>(uint64_t(count));update_vec(s,2,0,converted);
    delta=binary(delta,converted,'/');update_vec(s,1,1,delta);
    { AccessLock lock(src,dst,size_t(count)*4);
    for(uint32_t i=0;i<count;++i) {
      s.gpr.rip.qword=0x20619240;
      checkCancelled(*m);
      float source;std::memcpy(&source,src+size_t(i)*4,4);
      float result=binary(start,source,'*');update_vec(s,2,0,result);
      start=binary(delta,start,'+');update_vec(s,0,1,start);
      add4_flags(s,s.gpr.rsi.qword);s.gpr.rsi.qword+=4;
      s.gpr.rip.qword=0x2061924c;
      checkCancelled(*m);
      changed(dst+size_t(i)*4,4);std::memcpy(dst+size_t(i)*4,&result,4);
      add4_flags(s,s.gpr.rdi.qword);s.gpr.rdi.qword+=4;
      dec_flags(s,count-i);s.gpr.rdx.qword=count-i-1;
    }}
  }
  s.gpr.rip.qword=0x20619258;
  uint64_t ret=__remill_read_memory_64(m,s.gpr.rsp.qword);s.gpr.rsp.qword+=8;s.gpr.rip.qword=ret;
  return __remill_function_return(s,ret,m);
}
