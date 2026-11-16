#include <csignal>
#include <cstdint>
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>
#include <xmmintrin.h>
int main() {
  pid_t child = fork();
  if (child < 0)
    return 2;
  if (!child) {
    std::signal(SIGFPE, [](int) { _exit(77); });
    uint32_t lhs = 0x7f800000, rhs = 0xff800000;
    _mm_setcsr(0x1f00);
    asm volatile("movd %0, %%xmm2; addss %1, %%xmm2"
                 :
                 : "r"(lhs), "m"(rhs)
                 : "xmm2", "memory");
    _exit(78);
  }
  int status = 0;
  if (waitpid(child, &status, 0) != child || !WIFEXITED(status))
    return 3;
  int code = WEXITSTATUS(status);
  std::printf("native_child_exit=%d\n", code);
  return code == 77 ? 0 : 1;
}
