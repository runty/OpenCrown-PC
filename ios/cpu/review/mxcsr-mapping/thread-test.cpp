#include "status.h"
#include <atomic>
#include <cfenv>
#include <cstdio>
#include <thread>
static std::atomic<unsigned> checks{0}, failures{0};
static void worker(unsigned seed) {
  std::fenv_t saved;
  if (std::fegetenv(&saved)) {
    ++failures;
    return;
  }
  for (unsigned i = 0; i < 1000; ++i) {
    uint32_t status = (i * 17 + seed) & 63, guest = 0x1f80 | status;
    uint32_t old = experimental_mxcsr::read_fpsr();
    experimental_mxcsr::write_fpsr(experimental_mxcsr::load_fpsr(guest, old));
    uint32_t actual = experimental_mxcsr::read_fpsr();
    ++checks;
    if ((actual & 0x1f) != experimental_mxcsr::guest_to_host(guest))
      ++failures;
    ++checks;
    if ((experimental_mxcsr::store(guest & ~0x3du, actual) & 0x3f) != status)
      ++failures;
  }
  if (std::fesetenv(&saved))
    ++failures;
}
int main() {
  std::thread a(worker, 3), b(worker, 29);
  a.join();
  b.join();
  std::printf("checks=%u failures=%u\n", checks.load(), failures.load());
  return failures ? 1 : 0;
}
