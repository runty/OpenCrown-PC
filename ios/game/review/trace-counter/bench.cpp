#include "counter.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
static thread_local std::atomic<uint64_t>* slot;
[[gnu::noinline]] static void increment(){slot->fetch_add(1,std::memory_order_relaxed);}
int main(int n,char **v){
 if(n!=3)return 2;unsigned workers=std::strtoul(v[1],nullptr,10);bool shared=std::strtoul(v[2],nullptr,10);if(!workers||workers>128)return 2;
 constexpr unsigned iterations=500000;TraceCounterStudy counters;std::atomic<uint64_t> global{0};std::atomic<unsigned> ready{0};std::atomic<bool> go{false};std::vector<std::thread> threads;
 for(unsigned n=0;n<workers;++n)threads.emplace_back([&]{slot=shared?&global:counters.acquire();++ready;while(!go.load())std::this_thread::yield();for(unsigned i=0;i<iterations;++i)increment();});
 while(ready.load()!=workers)std::this_thread::yield();auto start=std::chrono::steady_clock::now();go=true;for(auto &thread:threads)thread.join();auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
 auto actual=shared?global.load():counters.total();if(actual!=uint64_t(workers)*iterations)return 1;
 std::printf("{\"workers\":%u,\"shared\":%s,\"increments\":%llu,\"wall_ns\":%llu}\n",workers,shared?"true":"false",(unsigned long long)actual,(unsigned long long)ns);
}
