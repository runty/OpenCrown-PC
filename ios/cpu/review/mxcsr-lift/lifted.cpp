#ifndef DCP_PRESEED_HOST_INVALID
#define DCP_PRESEED_HOST_INVALID 0
#endif
#include "runtime.h"
#include "status.h"

#include <cfenv>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <initializer_list>
struct Case {
  std::string name;
  int op;
  uint32_t lhs, rhs;
  uint32_t rounding = 0;
};
Case cases[] = {{"add_exact", 0, 0x3f800000, 0x40000000},
                {"add_denormal", 0, 0x3f800000, 0x00000001},
                {"add_invalid", 0, 0x7f800000, 0xff800000},
                {"add_snan", 0, 0x7f801234, 0x3f800000},
                {"add_qnan", 0, 0x7fc01234, 0x3f800000},
                {"add_rhs_snan", 0, 0x3f800000, 0x7f801234},
                {"add_qnan_rhs_snan", 0, 0x7fc01234, 0x7f801234},
                {"sub_lhs_snan", 1, 0x7f801234, 0x3f800000},
                {"sub_rhs_snan", 1, 0x3f800000, 0x7f801234},
                {"mul_lhs_snan", 2, 0x7f801234, 0x3f800000},
                {"mul_rhs_snan", 2, 0x3f800000, 0x7f801234},
                {"div_lhs_snan", 3, 0x7f801234, 0x3f800000},
                {"div_rhs_snan", 3, 0x3f800000, 0x7f801234},
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
extern "C" Memory *sub_50000000(State &, uint64_t, Memory *),
    *sub_50010000(State &, uint64_t, Memory *),
    *sub_50020000(State &, uint64_t, Memory *),
    *sub_50030000(State &, uint64_t, Memory *);
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
  std::vector<Case> matrix(std::begin(cases), std::end(cases));
  const uint32_t values[] = {0, 0x80000000, 1, 0x80000001, 0x00800000,
      0x3f800000, 0x3fa00000, 0xc0600000, 0x7f7fffff, 0xff7fffff,
      0x7f800000, 0xff800000, 0x7fc01234, 0xffc05678, 0x7f801234, 0xff800001};
  for (unsigned op = 0; op < 4; ++op)
    for (unsigned rounding = 0; rounding < 4; ++rounding)
      for (unsigned left = 0; left < 16; ++left)
        for (unsigned right = 0; right < 16; ++right)
          matrix.push_back({"matrix_" + std::to_string(op) + "_" +
                                std::to_string(rounding) + "_" + std::to_string(left) +
                                "_" + std::to_string(right),
                            int(op), values[left], values[right], rounding});
  Lifted fn[] = {sub_50000000, sub_50010000, sub_50020000, sub_50030000};
  uint64_t pc[] = {0x50000000, 0x50010000, 0x50020000, 0x50030000};
  for (auto &c : matrix)
    for (uint32_t sticky : {0u, 1u}) {
      State s{};
      Memory m;
      uint32_t result = 0;
      s.x87.fxsave.mxcsr.flat = 0x1f80 | sticky;
      s.gpr.rdi.qword = Memory::base + 0x100;
      s.gpr.rsi.qword = Memory::base + 0x200;
      s.gpr.rsp.qword = Memory::base + 0xff00;
      uint32_t initial = 0x1f80 | sticky | (c.rounding << 13);
      std::memcpy(m.bytes.data() + 0x100, &initial, 4);
      std::memcpy(m.bytes.data() + 0x104, &c.lhs, 4);
      std::memcpy(m.bytes.data() + 0x108, &c.rhs, 4);
      write64(m, s.gpr.rsp.qword, 0xfeedface);
      fenv_t old_env;
      std::fegetenv(&old_env);
      fn[c.op](s, pc[c.op], &m);
      std::fesetenv(&old_env);
      uint32_t csr;
      std::memcpy(&csr, m.bytes.data() + 0x200, 4);
      std::memcpy(&result, m.bytes.data() + 0x204, 4);
      if (!m.fault.empty())
        return 2;
      std::printf("%s %u %08x %08x\n", c.name.c_str(), sticky, result, csr);
    }
  // Unsupported guest controls must fail before changing the guest MXCSR or
  // host FPSR. These cases exercise actual lifted LDMXCSR, not just the hook.
  for (uint32_t unsupported : {0x1fc0u, 0x9f80u, 0x1f00u, 0x11f80u}) {
    State s{};
    Memory m;
    s.x87.fxsave.mxcsr.flat = 0x1f80;
    s.gpr.rdi.qword = Memory::base + 0x100;
    s.gpr.rsi.qword = Memory::base + 0x200;
    s.gpr.rsp.qword = Memory::base + 0xff00;
    std::memcpy(m.bytes.data() + 0x100, &unsupported, 4);
    write64(m, s.gpr.rsp.qword, 0xfeedface);
    fenv_t old_env;
    std::fegetenv(&old_env);
    uint64_t fpsr = experimental_mxcsr::read_fpsr();
    bool rejected = false;
    try { fn[0](s, pc[0], &m); }
    catch (const std::runtime_error &) { rejected = true; }
    bool unchanged = fpsr == experimental_mxcsr::read_fpsr() &&
                     s.x87.fxsave.mxcsr.flat == 0x1f80;
    std::fesetenv(&old_env);
    if (!rejected || !unchanged) {
      std::fprintf(stderr, "unsupported=%08x rejected=%d unchanged=%d\n",
                   unsupported, rejected, unchanged);
      return 3;
    }
  }
  std::fprintf(stderr, "unsupported controls: 4 rejected without mutation\n");

}
