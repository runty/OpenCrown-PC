// Drives private trace 30082b40; this source contains no game
// instructions/assets.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#ifdef REAL_LIBC_AOT
#include "runtime.h"
extern "C" Memory *sub_30082b40(State &, uint64_t, Memory *);
extern "C" void dcp_trace_enter(State &, uint64_t) {}
Memory *dispatch(State &, uint64_t, Memory *) {
  throw std::runtime_error("Unexpected dispatch in private libc fixture");
}
#endif
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  std::ifstream input(argv[1]);
  std::string line;
  unsigned checks = 0, failed = 0;
  while (std::getline(input, line)) {
    std::istringstream row(line);
    uint64_t bits;
    int exponent;
    unsigned control;
    row >> std::hex >> bits >> std::dec >> exponent >> control;
    uint64_t actual = 0;
#ifdef REAL_LIBC_AOT
    std::string expectedText;
    row >> expectedText;
    uint64_t expected = std::stoull(expectedText, nullptr, 16);
    State state{};
    Memory memory;
    state.x87.fxsave.cwd.flat = uint16_t(control);
    state.x87.fxsave.mxcsr.flat = 0x1f80;
    std::memcpy(&state.vec[0], &bits, 8);
    state.gpr.rdi.qword = uint32_t(exponent);
    state.gpr.rsp.qword = Memory::base + 0xff00;
    write64(memory, state.gpr.rsp.qword, 0xfeedface);
    sub_30082b40(state, 0x30082b40, &memory);
    std::memcpy(&actual, &state.vec[0], 8);
    ++checks;
    if (actual != expected) {
      if (failed++ < 12)
        std::printf("bits%llx n%d cw%x expected%llx actual%llx\n",
                    (unsigned long long)bits, exponent, control,
                    (unsigned long long)expected, (unsigned long long)actual);
    }
#else
    uint16_t old, cw = uint16_t(control);
    double value;
    std::memcpy(&value, &bits, 8);
    asm volatile("fnstcw %0" : "=m"(old));
    asm volatile("fldcw %3; fildl %2; fldl %1; fscale; fstpl %0; fstp %%st(0)"
                 : "=m"(actual)
                 : "m"(value), "m"(exponent), "m"(cw)
                 : "st", "st(1)");
    asm volatile("fnclex; fldcw %0" ::"m"(old));
    std::printf("%s %016llx\n", line.c_str(), (unsigned long long)actual);
#endif
  }
#ifdef REAL_LIBC_AOT
  std::printf("{\"checks\":%u,\"failed\":%u,\"private_trace\":\"30082b40\"}\n",
              checks, failed);
  return failed ? 1 : 0;
#endif
}
