#include <cstdint>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <csetjmp>
#include <random>
#include <sys/mman.h>
#include <unistd.h>
extern "C" void native_load(const void *,void *);
static sigjmp_buf jump;
static void fault(int){siglongjmp(jump,1);}
int main(){
  std::mt19937 rng(0x517a);alignas(32) uint8_t input[64],output[32];
  for(unsigned n=0;n<10000;++n){for(auto &x:input)x=uint8_t(rng());native_load(input+n%4,output);if(std::memcmp(input+n%4,output,32))return 1;}
  auto page=size_t(sysconf(_SC_PAGESIZE));auto *region=static_cast<uint8_t*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));if(region==MAP_FAILED)return 2;
  if(mprotect(region+page,page,PROT_NONE))return 3;for(unsigned i=0;i<32;++i)region[page-32+i]=uint8_t(i);
  struct sigaction old{},action{};action.sa_handler=fault;sigemptyset(&action.sa_mask);if(sigaction(SIGSEGV,&action,&old))return 4;
  native_load(region+page-32,output);if(std::memcmp(region+page-32,output,32))return 5;
  unsigned faults=0;for(int offset:{31,12,4,0}){if(sigsetjmp(jump,1)==0)native_load(region+page-offset,output);else ++faults;}
  sigaction(SIGSEGV,&old,nullptr);munmap(region,page*2);if(faults!=4)return 6;
  std::printf("PASS: 10000 exact-byte VMOVUPS loads, valid 32-byte boundary, 4 guarded faults\n");
}
