// Control-word and independent x87/SSE rounding oracle. Synthetic instructions
// only.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#ifdef CONTROL_LIFTED
#include "runtime.h"
extern "C" Memory *sub_1000(State &, uint64_t, Memory *);
extern "C" Memory *sub_2000(State &, uint64_t, Memory *);
Memory *dispatch(State &, uint64_t, Memory *memory) { return memory; }
static void run(bool independence, uint16_t cw, uint32_t *output) {
  State state{};
  Memory memory;
  state.x87.fxsave.cwd.flat = 0x037f;
  state.x87.fxsave.mxcsr.flat = 0x1f80;
  state.gpr.rdi.qword = Memory::base + 16;
  state.gpr.rsi.qword = Memory::base + 48;
  state.gpr.rsp.qword = Memory::base + 128;
  write64(memory, Memory::base + 128, 0xfeedface);
  if (independence) {
    uint32_t mxcsr = 0x5f80;
    std::memcpy(memory.bytes.data() + 48, &mxcsr, 4);
    std::memcpy(memory.bytes.data() + 52, &cw, 2);
    sub_2000(state, 8192, &memory);
  } else {
    std::memcpy(memory.bytes.data() + 48, &cw, 2);
    sub_1000(state, 4096, &memory);
  }
  std::memcpy(output, memory.bytes.data() + 16, 16);
}
#else
static void run(bool independence, uint16_t cw, uint32_t *output) {
  uint16_t oldCW;
  uint32_t oldCSR;
  asm volatile("fnstcw %0; stmxcsr %1" : "=m"(oldCW), "=m"(oldCSR));
  if (independence) {
    uint32_t csr = 0x5f80;
    asm volatile(
        "ldmxcsr %4; fldcw %5; stmxcsr %0; fnstcw %1; fninit; stmxcsr %2; "
        "fnstcw %3"
        : "=m"(output[0]), "=m"(*reinterpret_cast<uint16_t *>(output + 1)),
          "=m"(output[2]), "=m"(*reinterpret_cast<uint16_t *>(output + 3))
        : "m"(csr), "m"(cw));
  } else {
    uint16_t noWait, wait;
    asm volatile("fldcw %2; fnstcw %0; fstcw %1"
                 : "=m"(noWait), "=m"(wait)
                 : "m"(cw));
    if (noWait != wait)
      std::abort();
    std::memcpy(output, &noWait, 2);
  }
  asm volatile("fldcw %0; ldmxcsr %1" ::"m"(oldCW), "m"(oldCSR));
}
#endif
int main() {
  unsigned failed = 0, checks = 0;
  for (unsigned precision : {0u, 2u, 3u})
    for (unsigned rounding = 0; rounding < 4; ++rounding) {
      uint16_t cw = uint16_t(0x7f | (precision << 8) | (rounding << 10));
      uint32_t output[4]{};
      run(false, cw, output);
      ++checks;
      if (uint16_t(output[0]) != cw) {
        ++failed;
        std::printf("CW input%04x actual%04x\n", cw, uint16_t(output[0]));
      }
    }
  uint32_t output[4]{};
  run(true, 0x077f, output);
  uint32_t expected[] = {0x5f80, 0x077f, 0x5f80, 0x037f};
  for (unsigned i = 0; i < 4; ++i) {
    ++checks;
    if (output[i] != expected[i]) {
      ++failed;
      std::printf("independence field%u expected%04x actual%04x\n", i,
                  expected[i], output[i]);
    }
  }
  std::printf("{\"checks\":%u,\"failed\":%u}\n", checks, failed);
  return failed ? 1 : 0;
}
