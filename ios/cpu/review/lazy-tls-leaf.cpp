// Adapted from the prior DSP benchmark; local experimental harness only.
#include "runtime.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>

extern "C" Memory *sub_206364d0(State &, uint64_t, Memory *);
extern "C" Memory *reference_sub_206364d0(State &, uint64_t, Memory *);
extern "C" Memory *__remill_atomic_begin(Memory *);
extern "C" Memory *__remill_atomic_end(Memory *);
extern "C" void dcp_trace_enter(State &, uint64_t) {}
Memory *dispatch(State &, uint64_t, Memory *) {
  throw std::runtime_error("unexpected guest dispatch in fixed leaf fixture");
}
using Clock = std::chrono::steady_clock;
static constexpr uint64_t exitPC = 0x7fff0000;
static std::vector<uint8_t> image;
static unsigned checks;
static void check(bool ok, const char *what) {
  ++checks;
  if (!ok) throw std::runtime_error(what);
}
struct Snapshot {
  std::array<uint8_t, sizeof(State)> state;
  std::array<uint8_t, 704> a, b, c;
  std::array<uint8_t, 1024> stack;
};
struct Fixture {
  alignas(64) std::array<uint8_t, 704> a{}, b{}, c{};
  alignas(64) std::array<uint8_t, 1024> stack{};
  State state{};
  Memory memory;
  std::atomic<uint64_t> generation{1};
  Memory::SignalCache signalCache{generation};
  unsigned offset = 0, alias = 0, pattern = 0;
  Fixture(unsigned shift, unsigned overlap, unsigned values)
      : offset(shift), alias(overlap), pattern(values) {
    memory.bytes.clear();
    bindScalarThread(memory); // bound in the owning worker, as in the game
    signalCache.lastClear.store(1);
    memory.signalCache = &signalCache;
    memory.stopOnFault = true;
    addRegion(memory, image.data(), image.size());
    addRegion(memory, a.data() + 64 + offset, 512);
    addRegion(memory, b.data() + 64 + offset, 512);
    addRegion(memory, c.data() + 64 + offset, 512);
    addRegion(memory, stack.data(), stack.size());
    reset();
  }
  void reset() {
    a.fill(0xa1); b.fill(0xb2); c.fill(0xc3); stack.fill(0xd4);
    std::memset(&state, 0, sizeof(state));
    const uint32_t special[] = {0, 0x80000000, 0x7f800000, 0xff800000,
                               1, 0x007fffff, 0x7fa12345, 0xffc54321};
    for (unsigned i = 0; i < 128; ++i) {
      float value = float(int((i * 29 + pattern * 43) % 251) - 125) / 128.0f;
      uint32_t bits;
      std::memcpy(&bits, &value, 4);
      if (pattern == 2) bits = special[i % 8];
      std::memcpy(a.data() + 64 + offset + i * 4, &bits, 4);
      value = float(int((i * 13) % 59) - 29) / 64.0f;
      std::memcpy(b.data() + 64 + offset + i * 4, &value, 4);
    }
    auto address = [](uint8_t *p) { return reinterpret_cast<uint64_t>(p); };
    state.gpr.rdi.qword = address(a.data() + 64 + offset);
    state.gpr.rsi.qword = address(b.data() + 64 + offset);
    state.gpr.rdx.qword = alias == 1 ? state.gpr.rdi.qword :
                          alias == 2 ? state.gpr.rsi.qword :
                          address(c.data() + 64 + offset);
    state.gpr.rsp.qword = address(stack.data() + 520);
    state.gpr.rbx.qword = 0xb0b0;
    state.gpr.rbp.qword = 0xb1b1;
    state.gpr.r12.qword = 0xb2b2;
    state.gpr.r13.qword = 0xb3b3;
    state.gpr.r14.qword = 0xb4b4;
    state.gpr.r15.qword = 0xb5b5;
    std::memcpy(stack.data() + 520, &exitPC, 8);
    memory.fault.clear();
    memory.calls = memory.returns = 0;
  }
  void run(unsigned mode) {
    const bool batch = mode >= 2;
    auto f = mode % 2 ? sub_206364d0 : reference_sub_206364d0;
    if (batch) __remill_atomic_begin(&memory);
    try {
      f(state, reinterpret_cast<uint64_t>(image.data()) + 0x6364d0, &memory);
    } catch (...) {
      if (batch && memory.atomicLock.owns_lock()) memory.atomicLock.unlock();
      throw;
    }
    if (batch) __remill_atomic_end(&memory);
  }
  Snapshot snapshot() {
    Snapshot s{};
    std::memcpy(s.state.data(), &state, sizeof(state));
    s.a = a; s.b = b; s.c = c; s.stack = stack;
    return s;
  }
  void verify() {
    check(memory.fault.empty() && memory.calls == 0 && memory.returns == 1,
          "unexpected routine fault or control transfer");
    check(state.gpr.rip.qword == exitPC && state.gpr.rsp.qword ==
          reinterpret_cast<uint64_t>(stack.data() + 528), "return ABI differs");
    check(state.gpr.rbx.qword == 0xb0b0 && state.gpr.rbp.qword == 0xb1b1 &&
          state.gpr.r12.qword == 0xb2b2 && state.gpr.r13.qword == 0xb3b3 &&
          state.gpr.r14.qword == 0xb4b4 && state.gpr.r15.qword == 0xb5b5,
          "callee-saved registers changed");
    for (auto pair : {std::pair<const uint8_t *, uint8_t>(a.data(), 0xa1),
                      {b.data(), 0xb2}, {c.data(), 0xc3}}) {
      for (unsigned i = 0; i < a.size(); ++i)
        if (i < 64 + offset || i >= 576 + offset)
          check(pair.first[i] == pair.second, "data buffer canary changed");
    }
    for (unsigned i = 0; i < stack.size(); ++i)
      if (i < 396 || i >= 528) check(stack[i] == 0xd4, "stack canary changed");
    check(!memory.atomicLock.owns_lock(), "batch lock leaked");
  }
};

static void equivalence() {
  for (unsigned shift = 0; shift < 4; ++shift)
    for (unsigned alias = 0; alias < 3; ++alias)
      for (unsigned pattern = 0; pattern < 3; ++pattern) {
        Fixture f(shift, alias, pattern);
        f.run(0); f.verify();
        const auto reference = f.snapshot();
        for (unsigned mode = 1; mode < 4; ++mode) {
          f.reset(); f.run(mode); f.verify();
          const auto result = f.snapshot();
          check(reference.state == result.state && reference.a == result.a &&
                reference.b == result.b && reference.c == result.c &&
                reference.stack == result.stack, "original/coalesced/batched mismatch");
        }
      }
  // Throwing range failure must release the outer guard before another worker.
  Fixture f(0, 0, 0);
  (*f.memory.regions)[3].size = 508;
  bool threw = false;
  try { f.run(2); } catch (const std::runtime_error &) { threw = true; }
  check(threw && !f.memory.fault.empty() && !f.memory.atomicLock.owns_lock(),
        "fault cleanup did not release batch lock");
  std::thread t([&] { Memory m; addRegion(m, image.data(), image.size());
                       (void)read64(m, reinterpret_cast<uint64_t>(image.data())); });
  t.join();
}
static void benchmark(unsigned mode, unsigned threads, unsigned iterations) {
  std::atomic<unsigned> ready{0}; std::atomic<bool> go{false};
  std::vector<std::thread> workers;
  std::vector<double> maxima(threads);
  std::vector<std::string> failures(threads);
  for (unsigned n = 0; n < threads; ++n) workers.emplace_back([&, n] {
    try {
      Fixture f(0, 0, n % 2);
      ++ready;
      while (!go.load()) std::this_thread::yield();
      for (unsigned i = 0; i < iterations; ++i) {
        f.reset();
        const auto begin = Clock::now();
        f.run(mode);
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        maxima[n] = std::max(maxima[n], ms);
      }
    } catch (const std::exception &e) { failures[n] = e.what(); ++ready; }
  });
  while (ready.load() < threads) std::this_thread::yield();
  const auto begin = Clock::now(); go = true;
  for (auto &t : workers) t.join();
  const double sec = std::chrono::duration<double>(Clock::now() - begin).count();
  for (const auto &failure : failures) if (!failure.empty()) throw std::runtime_error(failure);
  std::printf("{\"mode\":%u,\"threads\":%u,\"calls\":%u,\"seconds\":%.9f,\"calls_per_second\":%.4f,\"maximum_call_ms\":%.6f}\n",
              mode, threads, threads * iterations, sec, threads * iterations / sec,
              *std::max_element(maxima.begin(), maxima.end()));
}
int main(int argc, char **argv) {
  try {
    if (argc < 2) return 2;
    std::ifstream file(argv[1], std::ios::binary);
    image.assign(std::istreambuf_iterator<char>(file), {});
    if (image.size() <= 0x7b87c0) throw std::runtime_error("module image too small");
    const auto original = image;
    equivalence();
    check(image == original, "immutable module image changed");
    std::printf("{\"equivalence_cases\":36,\"checked_modes\":4,\"checks\":%u,\"failed\":0}\n", checks);
    if (argc > 2) {
      const unsigned count = unsigned(std::stoul(argv[2]));
      for (unsigned repeat = 0; repeat < 3; ++repeat)
        for (unsigned threads : {1u, 2u})
          for (unsigned mode = 0; mode < 4; ++mode) benchmark(mode, threads, count);
    }
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what()); return 1;
  }
}
