// Deterministic, fixture-only signal injection after the grouped lock releases.
#define DCP_READ256_BENCH 1
#define DCP_READ256_TEST_HOOK 1
#include "read256-study.cpp"
static std::atomic<uint64_t> *injectedGeneration;
static Memory *callbackMemory;
static bool shouldStop;
static unsigned polls;
static void read256_test_hook(){if(injectedGeneration){++*injectedGeneration;injectedGeneration=nullptr;}}
static bool cancel(){
 ++polls;
 uint32_t replacement=0xdeadbeef;
 guestWrite(*callbackMemory,Memory::base+64,&replacement,4);
 return shouldStop;
}
int main(){
 for(bool stop:{false,true}){
  std::atomic<uint64_t> generation{1};Memory::SignalCache cache{generation};cache.lastClear=1;
  Memory m;m.signalCache=&cache;m.cancelled=cancel;callbackMemory=&m;shouldStop=stop;polls=0;
  for(unsigned i=0;i<32;++i)m.bytes[64+i]=uint8_t(i);
  uint32_t out[8];std::fill_n(out,8,0xaaaaaaaa);injectedGeneration=&generation;bool threw=false;
  try{candidate(m,Memory::base+64,out);}catch(const std::runtime_error &e){threw=std::string(e.what())=="bootstrap stopped";}
  if(stop){if(!threw||polls!=1||out[0]!=0xaaaaaaaa)return 1;}
  else if(threw||polls!=8||out[0]!=0xdeadbeef)return 2;
 }
 std::puts("PASS: deterministic cancellation and reentrant callback after grouped lock");
}
