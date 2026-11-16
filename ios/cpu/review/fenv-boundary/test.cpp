#include "boundary.h"
#include <atomic>
#include <cstdio>
#include <thread>

#pragma STDC FENV_ACCESS ON
static std::atomic<unsigned> checks{0}, failures{0};
static void check(bool pass) {
  ++checks;
  if (!pass)
    ++failures;
}
static bool state(int rounding, int flags) {
  return std::fegetround() == rounding &&
         std::fetestexcept(FE_ALL_EXCEPT) == flags;
}
static void run(int rounding, int alternate) {
  for (int i = 0; i < 1000; ++i) {
    std::fesetround(rounding);
    std::feclearexcept(FE_ALL_EXCEPT);
    std::feraiseexcept(FE_INVALID);
    {
      ExperimentalNativeBoundary outer;
      check(state(rounding, 0));
      std::feraiseexcept(FE_OVERFLOW);
      int native_flags = std::fetestexcept(FE_ALL_EXCEPT);
      {
        ExperimentalNativeBoundary::GuestCallback callback(outer);
        check(state(rounding, FE_INVALID));
        std::fesetround(alternate);
        std::feraiseexcept(FE_DIVBYZERO);
        {
          ExperimentalNativeBoundary inner;
          check(state(alternate, 0));
          std::feraiseexcept(FE_UNDERFLOW);
          {
            ExperimentalNativeBoundary::GuestCallback nested(inner);
            check(state(alternate, FE_INVALID | FE_DIVBYZERO));
            std::feraiseexcept(FE_INEXACT);
          }
          check((std::fetestexcept(FE_ALL_EXCEPT) & FE_UNDERFLOW) != 0);
        }
        check(state(alternate, FE_INVALID | FE_DIVBYZERO | FE_INEXACT));
      }
      check(state(rounding, native_flags));
      {
        ExperimentalNativeBoundary::GuestCallback callback(outer);
        check(state(alternate, FE_INVALID | FE_DIVBYZERO | FE_INEXACT));
      }
      check(state(rounding, native_flags));
    }
    check(state(alternate, FE_INVALID | FE_DIVBYZERO | FE_INEXACT));
    try {
      ExperimentalNativeBoundary outer;
      ExperimentalNativeBoundary::GuestCallback callback(outer);
      std::feraiseexcept(FE_OVERFLOW);
      throw 1;
    } catch (int) {
    }
    check((std::fetestexcept(FE_ALL_EXCEPT) &
           (FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW)) ==
          (FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW));
  }
}
int main() {
  std::fenv_t original;
  std::fegetenv(&original);
  std::thread a(run, FE_DOWNWARD, FE_UPWARD),
      b(run, FE_TOWARDZERO, FE_TONEAREST);
  a.join();
  b.join();
  std::fesetenv(&original);
  std::printf("checks=%u failures=%u\n", checks.load(), failures.load());
  return failures ? 1 : 0;
}
