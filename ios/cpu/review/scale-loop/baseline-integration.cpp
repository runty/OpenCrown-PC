// Matched-control runtime: same wrapper/renamed trace as batch research, but
// every entry calls the original translated scalar loop. Private benchmark only.
#include "../../runtime.cpp"
extern "C" Memory *dcp_original_scale(State &,uint64_t,Memory *);
extern "C" void dcp_scale_noop_trace(State &,uint64_t) {}
extern "C" Memory *scale_prototype(State &s,uint64_t pc,Memory *m) {
  return dcp_original_scale(s,pc,m);
}
