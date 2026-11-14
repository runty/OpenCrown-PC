// Executes owned translated control-flow fixtures on a bounded native stack.
#include "runtime.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <string>

extern "C" Memory *sub_1000(State &, uint64_t, Memory *);
extern "C" Memory *sub_2000(State &, uint64_t, Memory *);
extern "C" Memory *sub_3000(State &, uint64_t, Memory *);
extern "C" Memory *sub_3010(State &, uint64_t, Memory *);
extern "C" Memory *sub_4000(State &, uint64_t, Memory *);
extern "C" Memory *sub_4010(State &, uint64_t, Memory *);
extern "C" Memory *__remill_jump(State &, uint64_t, Memory *);
extern "C" Memory *__remill_missing_block(State &, uint64_t, Memory *);
extern "C" Memory *__remill_function_call(State &, uint64_t, Memory *);
namespace {
constexpr uint64_t exitPC = 0xf00d0100, limit = 1100003;
struct Context {
  unsigned mode = 0;
  bool redirecting = false;
  uint64_t entered = 0, minimum = UINT64_MAX, maximum = 0, serviceCalls = 0;
  uint64_t nativeStack = 0, calls = 0;
  std::string failure;
};
thread_local Context *current = nullptr;
unsigned checks = 0, failed = 0;
void check(bool ok, const char *message) {
  ++checks;
  if (!ok) {
    ++failed;
    std::fprintf(stderr, "FAIL %s\n", message);
  }
}
struct Entry {
  uint64_t pc;
  Lifted function;
};
const Entry entries[] = {{0x1000, sub_1000}, {0x2000, sub_2000},
                         {0x3000, sub_3000}, {0x3010, sub_3010},
                         {0x4000, sub_4000}, {0x4010, sub_4010}};
} // namespace
__attribute__((noinline)) Memory *tail_service(State &state, uint64_t,
                                               Memory *memory) {
  ++current->serviceCalls;
  state.gpr.rax.qword += 7;
  return memory;
}
Memory *dispatch(State &state, uint64_t pc, Memory *memory) {
  if (current && current->mode && !current->redirecting) {
    current->redirecting = true;
    if (current->mode == 1) {
      [[clang::musttail]] return __remill_missing_block(state, pc, memory);
    }
    [[clang::musttail]] return __remill_function_call(state, pc, memory);
  }
  if (current)
    current->redirecting = false;
  if (pc == exitPC)
    return memory;
  if (pc == 0x5000) {
    [[clang::musttail]] return tail_service(state, pc, memory);
  }
  auto found = std::lower_bound(
      std::begin(entries), std::end(entries), pc,
      [](const Entry &entry, uint64_t value) { return entry.pc < value; });
  if (found == std::end(entries) || found->pc != pc)
    throw std::runtime_error("missing tail fixture target");
  [[clang::musttail]] return found->function(state, pc, memory);
}
extern "C" void dcp_trace_enter(State &, uint64_t pc) {
  if (!current)
    return;
  uint64_t sp;
  asm volatile("mov %0, sp" : "=r"(sp));
  current->minimum = std::min(current->minimum, sp);
  current->maximum = std::max(current->maximum, sp);
  if (pc == 0x1000 || pc == 0x2000) {
    ++current->entered;
    const uint64_t expected = current->entered % 2 ? 0x1000 : 0x2000;
    if (pc != expected)
      throw std::runtime_error("translated indirect jump sequence diverged");
    if (current->entered == limit)
      throw std::runtime_error("tail loop complete");
  }
}
namespace {
void *loop(void *argument) {
  auto &context = *static_cast<Context *>(argument);
  current = &context;
  context.nativeStack = pthread_get_stacksize_np(pthread_self());
  Memory memory;
  memory.stopOnFault = true;
  State state{};
  state.gpr.rdi.qword = 0x1000;
  state.gpr.rsi.qword = 0x2000;
  try {
    dispatch(state, 0x1000, &memory);
    context.failure = "loop returned unexpectedly";
  } catch (const std::exception &error) {
    if (std::string(error.what()) != "tail loop complete")
      context.failure = error.what();
  }
  context.calls = memory.calls;
  if (!memory.fault.empty())
    context.failure = "stopOnFault fast path recorded a synthetic fault";
  current = nullptr;
  return nullptr;
}
void longLoops() {
  for (unsigned mode = 0; mode < 3; ++mode) {
    Context context;
    context.mode = mode;
    pthread_attr_t attr;
    check(!pthread_attr_init(&attr), "pthread attributes");
    check(!pthread_attr_setstacksize(&attr, 128 * 1024),
          "bounded128KiB stack request");
    pthread_t thread;
    int error = pthread_create(&thread, &attr, loop, &context);
    pthread_attr_destroy(&attr);
    check(!error, "pthread create");
    if (error)
      continue;
    check(!pthread_join(thread, nullptr), "pthread join");
    check(context.failure.empty(), "long indirect-jump loop completion");
    check(context.entered == limit, "more than1M translated transfers");
    check(context.nativeStack >= 128 * 1024 &&
              context.nativeStack <= 192 * 1024,
          "bounded native stack size");
    check(context.maximum - context.minimum < 8192,
          "constant native stack span through tail transfers");
    check(context.calls == (mode == 2 ? limit : 0),
          "function-call bridge count preserved");
    std::printf("{\"mode\":%u,\"translated_entries\":%llu,\"native_stack_"
                "bytes\":%llu,\"observed_stack_span\":%llu,\"calls\":%llu}\n",
                mode, (unsigned long long)context.entered,
                (unsigned long long)context.nativeStack,
                (unsigned long long)(context.maximum - context.minimum),
                (unsigned long long)context.calls);
  }
}
uint64_t rotate(uint64_t value) { return (value >> 13) | (value << (64 - 13)); }
void continuations() {
  Context context;
  current = &context;
  for (bool indirect : {false, true})
    for (uint64_t a :
         {0ULL, 1ULL, UINT64_MAX, 0x8000000000000000ULL, 0x123456789abcdef0ULL})
      for (uint64_t b : {0ULL, 7ULL, UINT64_MAX}) {
        Memory memory;
        memory.stopOnFault = true;
        State state{};
        state.gpr.rdi.qword = a;
        state.gpr.rsi.qword = b;
        state.gpr.rdx.qword = 0x4010;
        state.gpr.rsp.qword = Memory::base + 0xff00;
        const auto stack = state.gpr.rsp.qword;
        write64(memory, stack, exitPC);
        auto result = dispatch(state, indirect ? 0x4000 : 0x3000, &memory);
        uint64_t expected = indirect ? (rotate(a) ^ 0x1234) + b : (a * 3) ^ b;
        check(result == &memory && state.gpr.rax.qword == expected,
              "direct/indirect guest CALL resumes caller arithmetic");
        check(state.gpr.rsp.qword == stack + 8 && state.gpr.rip.qword == exitPC,
              "callee/caller RET restore stack and PC");
        check(memory.returns == 2 && memory.calls == (indirect ? 1u : 0u),
              "translated return and indirect call counts");
      }
  using Bridge = Memory *(*)(State &, uint64_t, Memory *);
  for (auto bridge : std::array<Bridge, 3>{
           __remill_jump, __remill_missing_block, __remill_function_call}) {
    State state{};
    Memory memory;
    memory.stopOnFault = false;
    check(bridge(state, 0xbad, &memory) == &memory &&
              memory.fault == "missing tail fixture target",
          "synthetic false path records missing-target fault");
    memory.fault.clear();
    memory.stopOnFault = true;
    bool caught = false;
    try {
      bridge(state, 0xbad, &memory);
    } catch (const std::runtime_error &error) {
      caught = std::string(error.what()) == "missing tail fixture target";
    }
    check(caught && memory.fault.empty(),
          "production true path propagates missing-target fault");
    state.gpr.rax.qword = 10;
    check(bridge(state, 0x5000, &memory) == &memory &&
              state.gpr.rax.qword == 17,
          "native3-argument service tail target");
  }
  check(context.serviceCalls == 3, "all bridges reached service");
  current = nullptr;
}
} // namespace
int main() {
  try {
    longLoops();
    continuations();
  } catch (const std::exception &error) {
    std::fprintf(stderr, "Unexpected tail fixture exception: %s\n",
                 error.what());
    return 2;
  }
  std::printf("{\"checks\":%u,\"failed\":%u}\n", checks, failed);
  return failed ? 1 : 0;
}
