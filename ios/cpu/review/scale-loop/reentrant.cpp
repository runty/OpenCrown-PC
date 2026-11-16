#include "runtime.h"
#include <cstdio>
#include <cstring>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
extern "C" Memory *sub_20619220(State &, uint64_t, Memory *);
extern "C" Memory *scale_prototype(State &, uint64_t, Memory *);
extern "C" void dcp_trace_enter(State &, uint64_t) {}
Memory *dispatch(State &, uint64_t, Memory *) {throw std::runtime_error("unexpected dispatch");}
static Memory *context;
static bool callback() {
  uint32_t value;
  guestRead(*context, Memory::base+0x100, &value, sizeof(value));
  return false;
}
static int child(Lifted fn) {
  alarm(2);Memory m;State s{};context=&m;
  s.gpr.rdi.qword=Memory::base+0x200;s.gpr.rsi.qword=Memory::base+0x100;
  s.gpr.rdx.qword=1;s.gpr.rsp.qword=Memory::base+0xff00;
  float a=1,b=2;std::memcpy(&s.vec[0],&a,4);std::memcpy(&s.vec[1],&b,4);
  write64(m,s.gpr.rsp.qword,0xfeedface);m.cancelled=callback;
  fn(s,0x20619220,&m);return 0;
}
int main() {
  unsigned index=0;
  for(auto fn:{sub_20619220,scale_prototype}) {
    pid_t pid=fork();if(pid==0)_exit(child(fn));
    int status=0;if(pid<0 || waitpid(pid,&status,0)!=pid)return 2;
    std::printf("%s exit=%d signal=%d\n",index++?"batch":"original",
      WIFEXITED(status)?WEXITSTATUS(status):-1,WIFSIGNALED(status)?WTERMSIG(status):0);
  }
}
