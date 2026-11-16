#ifndef DCP_PRESEED_HOST_INVALID
#define DCP_PRESEED_HOST_INVALID 0
#endif
#include "runtime.h"
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <initializer_list>
struct Case {
  const char *name;
  int op;
  uint32_t lhs, rhs;
};
Case cases[] = {{"add_exact", 0, 0x3f800000, 0x40000000},
                {"add_invalid", 0, 0x7f800000, 0xff800000},
                {"add_snan", 0, 0x7f801234, 0x3f800000},
                {"add_qnan", 0, 0x7fc01234, 0x3f800000},
                {"sub_invalid", 1, 0x7f800000, 0x7f800000},
                {"mul_invalid", 2, 0x00000000, 0x7f800000},
                {"mul_overflow", 2, 0x7f7fffff, 0x40000000},
                {"div_invalid", 3, 0, 0},
                {"div_zero", 3, 0x3f800000, 0},
                {"div_underflow", 3, 0x00800000, 0x40400000},
                {"div_inexact", 3, 0x3f800000, 0x40400000}};
extern "C" float width_reference_read_f32(Memory *, uint64_t);
extern "C" float __remill_read_memory_f32(Memory *m, uint64_t a) {
  return width_reference_read_f32(m, a);
}
extern "C" double width_reference_read_f64(Memory *, uint64_t);
extern "C" double __remill_read_memory_f64(Memory *m, uint64_t a) {
  return width_reference_read_f64(m, a);
}
extern "C" Memory *sub_40000000(State &, uint64_t, Memory *),
    *sub_40014000(State &, uint64_t, Memory *),
    *sub_40028000(State &, uint64_t, Memory *),
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
  Lifted fn[] = {sub_40000000, sub_40014000, sub_40028000, sub_4003c000};
  uint64_t pc[] = {0x40000000, 0x40014000, 0x40028000, 0x4003c000};
  for (auto &c : cases)
    for (uint32_t sticky : {0u, 1u}) {
      State s{};
      Memory m;
      uint32_t result = 0;
      s.x87.fxsave.mxcsr.flat = 0x1f80 | sticky;
      s.gpr.rdi.qword = Memory::base + 0x100;
      s.gpr.rsp.qword = Memory::base + 0xff00;
      std::memcpy(&s.vec[2], &c.lhs, 4);
      std::memcpy(m.bytes.data() + 0x100, &c.rhs, 4);
      write64(m, s.gpr.rsp.qword, 0xfeedface);
      std::feclearexcept(FE_ALL_EXCEPT);
      if (DCP_PRESEED_HOST_INVALID && sticky == 0)
        std::feraiseexcept(FE_INVALID);
      fn[c.op](s, pc[c.op], &m);
      int h = std::fetestexcept(FE_ALL_EXCEPT);
      std::memcpy(&result, &s.vec[2], 4);
      unsigned flags = !!(h & FE_INVALID) | ((!!(h & FE_DIVBYZERO)) << 2) |
                       ((!!(h & FE_OVERFLOW)) << 3) |
                       ((!!(h & FE_UNDERFLOW)) << 4) |
                       ((!!(h & FE_INEXACT)) << 5);
      if (!m.fault.empty())
        return 2;
      std::printf("%s %u %08x %08x %02x\n", c.name, sticky, result,
                  s.x87.fxsave.mxcsr.flat, flags);
    }
}
