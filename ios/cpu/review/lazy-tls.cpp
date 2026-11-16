// Fixture for the experimental lazy TLS fallback. A generated test-only runtime
// counts calls to the fallback; production builds contain no counter.
#include "../runtime.h"
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>
static std::atomic<unsigned> fallback{0};
extern "C" void dcp_test_tls_fallback() { ++fallback; }
Memory *dispatch(State &, uint64_t, Memory *m) { return m; }
extern "C" uint64_t __remill_read_memory_64(Memory *, uint64_t);
extern "C" Memory *__remill_write_memory_64(Memory *, uint64_t, uint64_t);
extern "C" float __remill_read_memory_f32(Memory *, uint64_t);
extern "C" Memory *__remill_write_memory_f32(Memory *, uint64_t, float);
extern "C" double __remill_read_memory_f64(Memory *, uint64_t);
extern "C" Memory *__remill_write_memory_f64(Memory *, uint64_t, double);
static bool exercise(Memory &m) {
  auto a=Memory::base+64; m.stopOnFault=true;
  __remill_write_memory_64(&m,a,0x123456789abcdef0ULL);
  if (__remill_read_memory_64(&m,a)!=0x123456789abcdef0ULL) return false;
  __remill_write_memory_f32(&m,a,1.25f);
  if (__remill_read_memory_f32(&m,a)!=1.25f) return false;
  __remill_write_memory_f64(&m,a,-2.5);
  return __remill_read_memory_f64(&m,a)==-2.5;
}
int main(){
 Memory unbound,bound;
 if(!exercise(unbound)||fallback.load()!=6)return 1;
 bindScalarThread(bound);if(!exercise(bound)||fallback.load()!=6)return 2;
 // Host hooks intentionally ignore the lifted binding and use their caller's
 // TLS, not the noinline lifted-only fallback.
 write64(bound,Memory::base+128,123);if(read64(bound,Memory::base+128)!=123||fallback.load()!=6)return 3;
 std::atomic<bool> fail{false};std::vector<std::thread> workers;
 for(unsigned i=0;i<8;++i)workers.emplace_back([&]{Memory local;bindScalarThread(local);for(unsigned n=0;n<1000;++n)if(!exercise(local))fail=true;});
 for(auto &w:workers)w.join();if(fail||fallback.load()!=6)return 4;
 std::printf("PASS: six unbound fallbacks, no bound/host fallbacks, eight independent workers\n");
}
