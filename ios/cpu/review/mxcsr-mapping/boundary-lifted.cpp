#include "../fenv-boundary/boundary.h"
#include "runtime.h"
#include "status.h"
#include <cfenv>
#include <cstdio>
#include <cstring>
extern "C" float width_reference_read_f32(Memory *, uint64_t);
extern "C" float __remill_read_memory_f32(Memory *m, uint64_t a) {
  return width_reference_read_f32(m, a);
}
extern "C" double width_reference_read_f64(Memory *, uint64_t);
extern "C" double __remill_read_memory_f64(Memory *m, uint64_t a) {
  return width_reference_read_f64(m, a);
}
extern "C" Memory *sub_40000000(State &, uint64_t, Memory *),
    *sub_4003c000(State &, uint64_t, Memory *);
Memory *dispatch(State &, uint64_t, Memory *) {
  throw std::runtime_error("unexpected dispatch");
}
extern "C" Memory *__remill_sync_hyper_call(State &, Memory *m, uint32_t) {
  m->fault = "unexpected hypercall";
  return m;
}
extern "C" Memory *__remill_error(State &, uint64_t, Memory *m) {
  m->fault = "unexpected error";
  return m;
}
int main() {
  std::fenv_t saved;
  if (std::fegetenv(&saved))
    return 2;
  State s{};
  Memory m;
  s.x87.fxsave.mxcsr.flat = 0x1f80;
  s.gpr.rdi.qword = Memory::base + 0x100;
  auto invoke = [&](Lifted fn, uint64_t pc, uint32_t lhs, uint32_t rhs) {
    s.gpr.rsp.qword = Memory::base + 0xff00;
    write64(m, s.gpr.rsp.qword, 0xfeedface);
    std::memcpy(&s.vec[2], &lhs, 4);
    std::memcpy(m.bytes.data() + 0x100, &rhs, 4);
    fn(s, pc, &m);
  };
  std::feclearexcept(FE_ALL_EXCEPT);
  experimental_mxcsr::write_fpsr(experimental_mxcsr::load_fpsr(
      s.x87.fxsave.mxcsr.flat, experimental_mxcsr::read_fpsr()));
  invoke(sub_40000000, 0x40000000, 0x7f800000, 0xff800000);
  {
    ExperimentalNativeBoundary host;
    std::feraiseexcept(FE_OVERFLOW);
    int native = std::fetestexcept(FE_ALL_EXCEPT);
    {
      ExperimentalNativeBoundary::GuestCallback guest(host);
      if (!(std::fetestexcept(FE_INVALID)) || (std::fetestexcept(FE_OVERFLOW)))
        return 3;
      invoke(sub_4003c000, 0x4003c000, 0x3f800000, 0);
    }
    if (std::fetestexcept(FE_ALL_EXCEPT) != native)
      return 4;
  }
  auto status = experimental_mxcsr::store(s.x87.fxsave.mxcsr.flat,
                                          experimental_mxcsr::read_fpsr());
  if (std::fesetenv(&saved) || !m.fault.empty())
    return 5;
  std::printf("guest_mxcsr=%08x\n", status);
  return status == 0x1f85 ? 0 : 6;
}
