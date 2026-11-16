// Synthetic lock-contention benchmark; no game or device performance claim.
#include "runtime.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
Memory *dispatch(State &, uint64_t, Memory *m) { return m; }
using Clock=std::chrono::steady_clock;
static uint64_t elapsed(Clock::time_point a){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-a).count();}
int main(){
  constexpr int readers=4,reads=300000,writes=12000;Memory m;const auto address=Memory::base+128;write64(m,address,0);
  uint64_t sum=0;auto start=Clock::now();for(int i=0;i<1000000;++i)sum+=read64(m,address);auto single=elapsed(start);if(sum)return 1;
  std::atomic<int> ready{0};std::atomic<bool> go{false},bad{false};std::atomic<uint64_t> maxWriter{0};std::vector<std::thread> threads;
  for(int k=0;k<readers;++k)threads.emplace_back([&]{ready.fetch_add(1);while(!go.load(std::memory_order_acquire))std::this_thread::yield();uint64_t local=0;for(int i=0;i<reads;++i){auto v=read64(m,address);if(v>writes)bad=true;local+=v;}if(local>uint64_t(reads)*writes)bad=true;});
  threads.emplace_back([&]{ready.fetch_add(1);while(!go.load(std::memory_order_acquire))std::this_thread::yield();uint64_t max=0;for(int i=1;i<=writes;++i){auto t=Clock::now();write64(m,address,i);auto delta=elapsed(t);if(delta>max)max=delta;}maxWriter=max;});
  while(ready.load()!=readers+1)std::this_thread::yield();start=Clock::now();go.store(true,std::memory_order_release);for(auto &t:threads)t.join();auto mixed=elapsed(start);
  if(bad||read64(m,address)!=writes)return 2;
  std::printf("{\"single_ns\":%llu,\"mixed_ns\":%llu,\"writer_max_ns\":%llu,\"readers\":%d,\"reads_each\":%d,\"writes\":%d}\n",(unsigned long long)single,(unsigned long long)mixed,(unsigned long long)maxWriter.load(),readers,reads,writes);
}
