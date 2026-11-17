#include "runtime.h"
#include <cstdint>
#include <cstdio>
extern "C" Memory *sub_2026dfc0(State &, uint64_t, Memory *);
extern "C" void dcp_trace_enter(State &, uint64_t) {}
extern "C" bool __remill_compare_ule(bool v) { return v; }
int main() {
  for (unsigned cf=0; cf<2; ++cf) for (unsigned zf=0; zf<2; ++zf) {
    State s{}; s.aflag.cf=cf; s.aflag.zf=zf;
    Memory *result=sub_2026dfc0(s,0x2026dfc0,nullptr);
    const uint64_t want=(cf||zf)?0x2026e00b:0x2026dfc2;
    if(result || s.gpr.rip.qword!=want || s.aflag.cf!=cf || s.aflag.zf!=zf) return 1;
    std::printf("CF=%u ZF=%u target=%llx\n",cf,zf,(unsigned long long)s.gpr.rip.qword);
  }
}
