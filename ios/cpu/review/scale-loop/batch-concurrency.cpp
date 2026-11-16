#include "runtime.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
extern "C" Memory *sub_20619220(State &, uint64_t, Memory *);
extern "C" Memory *scale_prototype(State &, uint64_t, Memory *);
extern "C" Memory *__remill_atomic_begin(Memory *);
extern "C" Memory *__remill_atomic_end(Memory *);
extern "C" void dcp_trace_enter(State &, uint64_t) {}
Memory *dispatch(State &, uint64_t, Memory *) {throw std::runtime_error("unexpected dispatch");}
int main(){
  configureGuestSnapshots(true,true);
  std::vector<uint8_t> data(65536);Memory owner,worker;
  addSnapshotRegion(owner,data.data(),data.size());worker.regions=owner.regions;
  uint64_t base=reinterpret_cast<uint64_t>(data.data());
  for(unsigned i=0;i<256;++i){float f=float(i+1);std::memcpy(data.data()+0x1000+i*4,&f,4);}
  auto snapshot=guestSnapshot(worker,base+0x4000,1024);
  std::atomic<uint64_t> generation{1};Memory::SignalCache cache{generation};
  cache.lastClear.store(1);worker.signalCache=&cache;worker.stopOnFault=true;
  write64(worker,base+0xff00,0xfeedface);
  State state{};state.x87.fxsave.mxcsr.flat=0x1f80;state.gpr.rdi.qword=base+0x4000;state.gpr.rsi.qword=base+0x1000;
  state.gpr.rdx.qword=256;state.gpr.rsp.qword=base+0xff00;
  float one=1;std::memcpy(&state.vec[0],&one,4);std::memcpy(&state.vec[1],&one,4);
  std::atomic<bool> attempted{false},done{false},failed{false};
  __remill_atomic_begin(&owner);
  std::thread t([&]{bindScalarThread(worker);attempted.store(true,std::memory_order_release);
    try {scale_prototype(state,0x20619220,&worker);}catch(...){failed=true;}done=true;});
  while(!attempted.load(std::memory_order_acquire))std::this_thread::yield();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  bool excluded=!done.load();
  __remill_atomic_end(&owner);t.join();
  bool changed=snapshot->matches(worker,base+0x4000)==GuestSnapshotMatch::Different;
  auto fresh=guestSnapshot(worker,base+0x4000,1024);
  bool version=fresh->matches(worker,base+0x4000)==GuestSnapshotMatch::Version;
  auto stats=guestSnapshotDiagnostics();
  bool ok=excluded && done && !failed && changed && version && !stats.proof_failures && state.gpr.rip.qword==0xfeedface;
  std::printf("atomic_exclusion=%d batch_completed=%d invalidated=%d fresh_version=%d proof_failures=%llu\n",excluded,done.load(),changed,version,(unsigned long long)stats.proof_failures);
  return ok?0:1;
}
