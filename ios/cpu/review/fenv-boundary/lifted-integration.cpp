#include "boundary.h"
#include "runtime.h"
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
extern "C" Memory *sub_40000000(State &, uint64_t, Memory *);
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
  State state{};
  Memory memory;
  uint32_t lhs = 0x7f800000, rhs = 0xff800000, result = 0;
  state.x87.fxsave.mxcsr.flat = 0x1f80;
  state.gpr.rdi.qword = Memory::base + 0x100;
  state.gpr.rsp.qword = Memory::base + 0xff00;
  std::memcpy(&state.vec[2], &lhs, 4);
  std::memcpy(memory.bytes.data() + 0x100, &rhs, 4);
  write64(memory, state.gpr.rsp.qword, 0xfeedface);
  std::feclearexcept(FE_ALL_EXCEPT);
  sub_40000000(state, 0x40000000, &memory);
  if (!(std::fetestexcept(FE_INVALID)))
    return 1;
  {
    ExperimentalNativeBoundary host;
    if (std::fetestexcept(FE_ALL_EXCEPT))
      return 2;
    std::feraiseexcept(FE_OVERFLOW);
    const int host_flags = std::fetestexcept(FE_ALL_EXCEPT);
    {
      ExperimentalNativeBoundary::GuestCallback callback(host);
      if (!(std::fetestexcept(FE_INVALID)) || (std::fetestexcept(FE_OVERFLOW)))
        return 3;
      std::feraiseexcept(FE_DIVBYZERO);
    }
    if (std::fetestexcept(FE_ALL_EXCEPT) != host_flags)
      return 4;
  }
  int flags = std::fetestexcept(FE_ALL_EXCEPT);
  std::memcpy(&result, &state.vec[2], 4);
  if ((flags & (FE_INVALID | FE_DIVBYZERO)) != (FE_INVALID | FE_DIVBYZERO) ||
      (flags & FE_OVERFLOW) || result != 0xffc00000 ||
      state.x87.fxsave.mxcsr.flat != 0x1f80 || !memory.fault.empty())
    return 5;
  std::printf(
      "lifted result=%08x guest_mxcsr=%08x host_invalid_and_callback_divzero=1 "
      "native_overflow_isolated=1\n",
      result, state.x87.fxsave.mxcsr.flat);
}
