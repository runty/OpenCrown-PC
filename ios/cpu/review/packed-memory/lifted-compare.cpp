// Compares one actual lifted VMOVUPS+RET with an isolated grouped-read rewrite.
#define DCP_READ256_BENCH 1
#define DCP_READ256_IR 1
#include "read256-study.cpp"
extern "C" Memory *sub_206373d0(State &,uint64_t,Memory *);
extern "C" Memory *sub_dcp_grouped(State &,uint64_t,Memory *);
int main(){
 std::atomic<uint64_t> gen{1};Memory::SignalCache cache{gen};cache.lastClear=1;
 Memory a,b;a.stopOnFault=b.stopOnFault=true;a.signalCache=b.signalCache=&cache;
 std::mt19937 rng(0x517a);unsigned checks=0;
 for(unsigned i=0;i<10000;++i){
  State x{};x.gpr.rcx.qword=Memory::base+256;x.gpr.r12.qword=i%4;x.gpr.rsp.qword=Memory::base+0xff00;
  for(unsigned n=0;n<32;++n)reinterpret_cast<uint8_t*>(&x.vec[0])[n]=uint8_t(rng());
  for(unsigned n=0;n<64;++n)a.bytes[256+n]=b.bytes[256+n]=uint8_t(rng());
  write64(a,x.gpr.rsp.qword,0xfeedface);write64(b,x.gpr.rsp.qword,0xfeedface);State y=x;
  uint32_t proof[8];if(!try_read256(b,x.gpr.rcx.qword+4*x.gpr.r12.qword,proof))return 1;
  sub_206373d0(x,0x206373d0,&a);sub_dcp_grouped(y,0x206373d0,&b);
  if(std::memcmp(&x,&y,sizeof x)||a.bytes!=b.bytes||a.fault!=b.fault||a.returns!=b.returns||
     std::memcmp(&x.vec[0],a.bytes.data()+256+4*(i%4),32))return 2;++checks;
 }
 for(uint64_t address:{Memory::base-4,Memory::base+65536-12,Memory::base+65536-4,Memory::base+65536}){
  State x{};x.gpr.rcx.qword=address;x.gpr.rsp.qword=Memory::base+0xff00;State y=x;
  a.fault.clear();b.fault.clear();a.returns=b.returns=0;uint32_t proof[8];if(try_read256(b,address,proof))return 3;
  bool fa=false,fb=false;try{sub_206373d0(x,0x206373d0,&a);}catch(const std::runtime_error &){fa=true;}
  try{sub_dcp_grouped(y,0x206373d0,&b);}catch(const std::runtime_error &){fb=true;}
  if(!fa||!fb||std::memcmp(&x,&y,sizeof x)||a.bytes!=b.bytes||a.fault!=b.fault)return 4;++checks;
 }
 std::printf("PASS: %u actual-lift state/memory cases including faults\n",checks);
}
