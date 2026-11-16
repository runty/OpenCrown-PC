#define DCP_READ256_BENCH 1
#include "read256-study.cpp"
#include <cstdlib>
int main(int argc,char **argv){
 if(argc!=2)return 2;const bool grouped=std::atoi(argv[1]);Memory m;std::atomic<uint64_t> generation{1};Memory::SignalCache cache{generation};cache.lastClear=1;m.signalCache=&cache;
 for(unsigned i=0;i<32;++i)m.bytes[256+i]=uint8_t(i);uint64_t sum=0;uint32_t out[8];
 auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<500000;++i){if(grouped)candidate(m,Memory::base+256,out);else original(m,Memory::base+256,out);sum+=out[i&7];}
 auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
 std::printf("{\"grouped\":%s,\"iterations\":500000,\"wall_ns\":%llu,\"checksum\":%llu}\n",grouped?"true":"false",(unsigned long long)ns,(unsigned long long)sum);
}
