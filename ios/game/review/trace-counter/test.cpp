#include "counter.h"
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>
int main(){
  constexpr unsigned workers=32,iterations=100000;TraceCounterStudy a,b;
  std::atomic<unsigned> ready{0};std::atomic<bool> go{false};std::vector<std::thread> threads;
  for(unsigned n=0;n<workers;++n)threads.emplace_back([&]{auto token=a.acquire();++ready;while(!go.load())std::this_thread::yield();for(unsigned i=0;i<iterations;++i)TraceCounterStudy::increment(token);});
  while(ready.load()!=workers)std::this_thread::yield();go=true;uint64_t prior=0;
  while(a.total()<uint64_t(workers)*iterations){auto now=a.total();if(now<prior)return 1;prior=now;std::this_thread::yield();}
  for(auto &thread:threads)thread.join();if(a.total()!=uint64_t(workers)*iterations||b.total()!=0)return 2;
  // More acquisitions than slots use a safe shared fallback without dropping
  // increments; separate counters represent consecutive engine sessions.
  for(unsigned i=0;i<256;++i){auto slot=b.acquire();TraceCounterStudy::increment(slot);}
  if(b.total()!=256||a.total()!=uint64_t(workers)*iterations)return 3;
  auto outer=a.acquire();auto nested=outer;TraceCounterStudy::increment(nested);TraceCounterStudy::increment(outer);
  if(a.total()!=uint64_t(workers)*iterations+2)return 4;
  std::printf("PASS: %u concurrent increments, live monotonic samples, overflow and nested token\n",workers*iterations);
}
