// Small checked-scalar benchmark; not a device or whole-game measurement.
#include "../runtime.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
Memory *dispatch(State &,uint64_t,Memory *m){return m;}
extern "C" uint64_t __remill_read_memory_64(Memory *,uint64_t);
extern "C" Memory *__remill_write_memory_64(Memory *,uint64_t,uint64_t);
static uint64_t cpu(){timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
int main(){
 alignas(4096) std::array<uint64_t,512> data{};data[17]=0x12345678;
 Memory m; m.bytes.clear(); addRegion(m,reinterpret_cast<uint8_t*>(data.data()),4096); bindScalarThread(m);
 uint64_t p=reinterpret_cast<uint64_t>(&data[17]); if(__remill_read_memory_64(&m,p)!=data[17])return 1;
 constexpr unsigned n=2000000;auto t=cpu();uint64_t sum=0;
 for(unsigned i=0;i<n;++i)sum+=__remill_read_memory_64(&m,p);
 auto read=cpu()-t;if(sum!=uint64_t(n)*0x12345678)return 2;
 t=cpu();for(unsigned i=0;i<n;++i)__remill_write_memory_64(&m,p,i);auto write=cpu()-t;
 if(data[17]!=n-1)return 3;
 std::printf("{\"count\":%u,\"read_cpu_ns\":%llu,\"write_cpu_ns\":%llu,\"checksum\":%llu}\n",n,(unsigned long long)read,(unsigned long long)write,(unsigned long long)sum);
}
