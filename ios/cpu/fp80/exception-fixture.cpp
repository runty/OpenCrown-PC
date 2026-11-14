#include "fp80.h"
#include <atomic>
#include <cfenv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
extern "C" int32_t __remill_fpu_exception_test(int32_t);
extern "C" void __remill_fpu_exception_clear(int32_t);
extern "C" void __remill_fpu_exception_raise(int32_t);
int main() {
  std::atomic<unsigned> checks{0}, failed{0};
  std::vector<std::thread> threads;
  for (unsigned worker = 0; worker < 8; ++worker)
    threads.emplace_back([&] {
      auto check = [&](bool value) {
        ++checks;
        if (!value)
          ++failed;
      };
      for (unsigned mask = 0; mask < 128; ++mask) {
        __remill_fpu_exception_clear(127);
        __remill_fpu_exception_raise(mask);
        // Native raising overflow/underflow may also raise inexact.
        check((__remill_fpu_exception_test(mask) & mask) == mask);
        __remill_fpu_exception_clear(mask);
        check(!__remill_fpu_exception_test(mask));
      }
      uint8_t zero[16]{}, one[16]{}, out[16]{};
      dcp_fp80_from_i64(one, 1);
      __remill_fpu_exception_clear(127);
      dcp_fp80_binary(out, one, zero, 3);
      check(__remill_fpu_exception_test(127) == 4);
      std::feraiseexcept(FE_INVALID);
      check(__remill_fpu_exception_test(127) == 5);
      __remill_fpu_exception_clear(1);
      check(__remill_fpu_exception_test(127) == 4);
      __remill_fpu_exception_clear(4);
      check(!__remill_fpu_exception_test(127));
      dcp_fp80_binary(out, zero, zero, 3);
      check(__remill_fpu_exception_test(127) == 1);
      __remill_fpu_exception_clear(127);
    });
  for (auto &thread : threads)
    thread.join();
  std::printf("{\"checks\":%u,\"failed\":%u,\"threads\":8}\n", checks.load(),
              failed.load());
  return failed ? 1 : 0;
}
