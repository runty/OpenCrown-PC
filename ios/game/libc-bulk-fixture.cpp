#include "libc-bulk.h"
#include "runtime.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>
extern "C" Memory *sub_30054a60(State &, uint64_t, Memory *);
extern "C" Memory *sub_30054a20(State &, uint64_t, Memory *);
extern "C" Memory *sub_300552e0(State &, uint64_t, Memory *);
Memory *dispatch(State &, uint64_t, Memory *) {
  throw std::runtime_error("Unexpected dispatch in bulk fixture");
}
namespace {
constexpr uint64_t PCs[] = {0x30054a60, 0x30054a20, 0x300552e0};
using Shim = Memory *(*)(State &, uint64_t, Memory *);
Shim shims[] = {sub_30054a60, sub_30054a20, sub_300552e0};
thread_local unsigned entered = 0;
thread_local State *enteredState = nullptr;
thread_local uint64_t enteredPC = 0;
thread_local unsigned cancelCalls = 0;
bool cancelAfterCommit() { return ++cancelCalls >= 2; }
std::atomic<uint64_t> checks{0}, failures{0}, calls[3]{}, byteCount{0};
void check(bool ok, const char *label) {
  ++checks;
  if (!ok && failures.fetch_add(1) < 16)
    std::fprintf(stderr, "FAIL %s\n", label);
}
uint64_t address(const void *p) { return reinterpret_cast<uint64_t>(p); }
void pattern(std::vector<uint8_t> &data, unsigned seed = 0) {
  for (size_t i = 0; i < data.size(); ++i)
    data[i] = uint8_t(i * 173 + seed * 37);
}
State prepare(Memory &memory, uint64_t destination, uint64_t source,
              uint64_t count, uint64_t stack = Memory::base + 0xff00) {
  State state;
  std::memset(&state, 0xa5, sizeof(state));
  state.gpr.rdi.qword = destination;
  state.gpr.rsi.qword = source;
  state.gpr.rdx.qword = count;
  state.gpr.rsp.qword = stack;
  write64(memory, stack, 0x123456789abcdef0ULL);
  return state;
}
void success(unsigned kind, State &state, Memory &memory,
             uint64_t expectedPC = 0x123456789abcdef0ULL) {
  State expected;
  std::memcpy(&expected, &state, sizeof(state));
  expected.gpr.rax.qword = state.gpr.rdi.qword;
  expected.gpr.rip.qword = expectedPC;
  expected.gpr.rsp.qword += 8;
  unsigned before = entered;
  auto result = shims[kind](state, PCs[kind], &memory);
  check(result == &memory, "Memory return identity");
  check(entered == before + 1 && enteredState == &state &&
            enteredPC == PCs[kind],
        "production LLVM shim trace hook");
  check(!std::memcmp(&state, &expected, sizeof(state)),
        "all State bytes preserved except RAX/RIP/RSP");
  ++calls[kind];
  byteCount.fetch_add(state.gpr.rdx.qword);
}
template <class F> void rejects(F function, const char *label) {
  bool caught = false;
  try {
    function();
  } catch (const std::runtime_error &) {
    caught = true;
  }
  check(caught, label);
}
void reference(std::vector<uint8_t> &out, const std::vector<uint8_t> &before,
               size_t destination, size_t source, size_t count) {
  for (size_t i = 0; i < count; ++i)
    out[destination + i] = before[source + i];
}

void smallCopies() {
  Memory memory;
  std::vector<uint8_t> data(640), initial(data.size()), expected(data.size());
  pattern(initial);
  addRegion(memory, data.data(), data.size());
  const size_t sizes[] = {0,  1,  2,  3,  7,  8,  15,  16,  17,
                          31, 32, 33, 63, 64, 65, 127, 128, 129};
  for (unsigned kind = 0; kind < 2; ++kind)
    for (size_t size : sizes)
      for (unsigned sa = 0; sa < 64; ++sa)
        for (unsigned da = 0; da < 64; ++da) {
          data = initial;
          expected = initial;
          size_t src = 64 + sa, dst = 320 + da;
          reference(expected, initial, dst, src, size);
          auto state = prepare(memory, address(data.data() + dst),
                               address(data.data() + src), size);
          success(kind, state, memory);
          check(data == expected,
                "copy exact bytes and guards, all mod64 alignments");
        }
  for (unsigned kind = 0; kind < 2; ++kind)
    for (size_t size : sizes)
      for (unsigned alignment = 0; alignment < 64; ++alignment)
        for (int distance : {-65, -32, -17, -8, -1, 0, 1, 8, 17, 32, 65}) {
          data = initial;
          expected = initial;
          size_t src = 192 + alignment, dst = size_t(int(src) + distance);
          reference(expected, initial, dst, src, size);
          auto state = prepare(memory, address(data.data() + dst),
                               address(data.data() + src), size);
          success(kind, state, memory);
          check(data == expected, "overlap directions and guards");
        }
}
void largeCopiesAndFills() {
  const size_t sizes[] = {255,  256,  257,  511,  512,   513,   1023,  1024,
                          1025, 4095, 4096, 4097, 65535, 65536, 65537, 1048576};
  for (size_t size : sizes) {
    Memory memory;
    std::vector<uint8_t> data(size * 2 + 512), initial(data.size()),
        expected(data.size());
    pattern(initial, 1);
    addRegion(memory, data.data(), data.size());
    for (unsigned kind = 0; kind < 2; ++kind)
      for (unsigned sa : {0u, 1u, 7u, 15u, 31u, 63u})
        for (unsigned da : {0u, 1u, 7u, 15u, 31u, 63u}) {
          data = initial;
          expected = initial;
          size_t src = 64 + sa, dst = size + 256 + da;
          reference(expected, initial, dst, src, size);
          auto state = prepare(memory, address(data.data() + dst),
                               address(data.data() + src), size);
          success(kind, state, memory);
          check(data == expected, "large copy exact bytes and guards");
        }
  }
  for (size_t size :
       {size_t(0), size_t(1), size_t(7), size_t(16), size_t(31), size_t(32),
        size_t(63), size_t(64), size_t(65), size_t(255), size_t(256),
        size_t(257), size_t(4096), size_t(65537), size_t(1048576)}) {
    Memory memory;
    std::vector<uint8_t> data(size + 256), initial(data.size()),
        expected(data.size());
    pattern(initial, 2);
    addRegion(memory, data.data(), data.size());
    for (unsigned alignment = 0; alignment < 64; ++alignment)
      for (uint64_t value : {0ULL, 1ULL, 127ULL, 128ULL, 255ULL, 256ULL,
                             0xabcdef42ULL, UINT64_MAX}) {
        data = initial;
        expected = initial;
        size_t dst = 64 + alignment;
        std::fill(expected.begin() + dst, expected.begin() + dst + size,
                  uint8_t(value));
        auto state = prepare(memory, address(data.data() + dst), value, size);
        success(2, state, memory);
        check(data == expected,
              "memset low byte and guards all mod64 alignments");
      }
  }
  // An explicit 16 MiB copy exercises sizes well beyond ordinary inline copies.
  size_t size = 16 * 1024 * 1024;
  Memory memory;
  std::vector<uint8_t> data(size * 2 + 512), initial(data.size()),
      expected(data.size());
  pattern(initial, 3);
  addRegion(memory, data.data(), data.size());
  data = initial;
  expected = initial;
  reference(expected, initial, size + 128, 63, size);
  auto state = prepare(memory, address(data.data() + size + 128),
                       address(data.data() + 63), size);
  success(0, state, memory);
  check(data == expected, "16MiB copy");
}
void stackOverlapAndZero() {
  Memory memory;
  std::vector<uint8_t> data(512), expected;
  addRegion(memory, data.data(), data.size());
  for (unsigned kind = 0; kind < 3; ++kind) {
    pattern(data, 4);
    auto state = prepare(memory, address(data.data() + 64),
                         kind == 2 ? 0xab : address(data.data() + 256), 16,
                         address(data.data() + 64));
    expected = data;
    if (kind == 2)
      std::fill(expected.begin() + 64, expected.begin() + 80, 0xab);
    else
      reference(expected, data, 64, 256, 16);
    uint64_t pc;
    std::memcpy(&pc, expected.data() + 64, 8);
    success(kind, state, memory, pc);
    check(data == expected, "RET observes overwritten return slot");
    for (uint64_t invalid : {0ULL, UINT64_MAX}) {
      auto zero = prepare(memory, invalid, invalid, 0);
      success(kind, zero, memory);
      check(zero.gpr.rax.qword == invalid,
            "zero length nullable/unmapped arguments");
    }
  }
  // Both the destination and saved RET slot can reside in synthetic memory.
  auto state = prepare(memory, Memory::base + 100, Memory::base + 200, 16);
  uint8_t bytes[16];
  for (unsigned i = 0; i < 16; ++i)
    bytes[i] = uint8_t(i + 1);
  guestWrite(memory, Memory::base + 200, bytes, 16);
  success(1, state, memory);
  uint8_t result[16]{};
  guestRead(memory, Memory::base + 100, result, 16);
  check(!std::memcmp(bytes, result, 16), "synthetic memory spans");
}
void failuresAndCancellation() {
  Memory memory;
  std::vector<uint8_t> data(512), saved;
  pattern(data, 5);
  addRegion(memory, data.data(), 128);
  addRegion(memory, data.data() + 128, 128);
  addRegion(memory, data.data() + 256, 256);
  auto fail = [&](unsigned kind, uint64_t dst, uint64_t src, uint64_t count,
                  uint64_t stack = Memory::base + 0xff00) {
    auto savedStop = memory.stopping;
    auto savedCallback = memory.cancelled;
    memory.stopping = nullptr;
    memory.cancelled = nullptr;
    auto state = prepare(memory, dst, src, count);
    memory.stopping = savedStop;
    memory.cancelled = savedCallback;
    state.gpr.rsp.qword = stack;
    State before;
    std::memcpy(&before, &state, sizeof(state));
    saved = data;
    auto prior = dcp_libc_bulk_stats();
    unsigned traceBefore = entered;
    rejects([&] { shims[kind](state, PCs[kind], &memory); },
            "bulk/stack bounds failure throws through LLVM shim");
    check(data == saved, "failure validates whole spans before writes");
    check(!std::memcmp(&state, &before, sizeof(state)),
          "failure leaves all State unchanged");
    auto after = dcp_libc_bulk_stats();
    check(!std::memcmp(&prior, &after, sizeof(prior)),
          "failed return not counted");
    check(entered == traceBefore + 1, "failing shim traced once");
  };
  fail(0, address(data.data() + 256), 0, 16);
  fail(1, address(data.data() + 256), UINT64_MAX, 16);
  fail(0, 0, address(data.data() + 256), 16);
  fail(2, UINT64_MAX, 0xaa, 16);
  fail(0, address(data.data() + 256), address(data.data() + 120),
       16); // Adjacent registrations cannot authorize a crossing span.
  fail(1, address(data.data() + 120), address(data.data() + 256), 16);
  fail(2, address(data.data() + 256), 0xaa, UINT64_MAX);
  fail(0, address(data.data() + 256), address(data.data() + 32), 16, 0);
  fail(1, address(data.data() + 256), address(data.data() + 32), 16,
       UINT64_MAX);
  fail(2, address(data.data() + 256), 0xab, 16,
       Memory::base + memory.bytes.size() - 4);
  std::atomic<bool> stop{true};
  memory.stopping = &stop;
  for (unsigned kind = 0; kind < 3; ++kind) {
    fail(kind, address(data.data() + 256),
         kind == 2 ? 0xab : address(data.data() + 32), 16);
    fail(kind, 0, 0, 0);
  }
  stop = false;
  // Cancellation after a committed move may stop before RET. This boundary is
  // cooperative, not a rollback transaction; bytes remain committed, State does
  // not.
  memory.cancelled = cancelAfterCommit;
  cancelCalls = 0;
  auto state = prepare(memory, address(data.data() + 256),
                       address(data.data() + 32), 16);
  cancelCalls = 0;
  State before;
  std::memcpy(&before, &state, sizeof(state));
  saved = data;
  reference(saved, data, 256, 32, 16);
  auto prior = dcp_libc_bulk_stats();
  rejects([&] { shims[0](state, PCs[0], &memory); },
          "post-commit cancellation");
  check(data == saved && !std::memcmp(&state, &before, sizeof(state)),
        "post-commit cancellation retains bytes and untouched return state");
  auto after = dcp_libc_bulk_stats();
  check(!std::memcmp(&prior, &after, sizeof(prior)),
        "cancelled RET not counted");
  memory.cancelled = nullptr;
  rejects([&] { dcp_libc_bulk(state, 3, &memory); }, "unknown kind rejected");
  rejects([&] { dcp_libc_bulk(state, 0, nullptr); }, "null Memory rejected");
}
void threaded() {
  std::vector<std::thread> threads;
  for (unsigned worker = 0; worker < 8; ++worker)
    threads.emplace_back([worker] {
      Memory memory;
      std::vector<uint8_t> data(1024), expected;
      pattern(data, worker + 10);
      addRegion(memory, data.data(), data.size());
      for (unsigned i = 0; i < 3000; ++i) {
        unsigned kind = i % 3;
        size_t count = i % 257, src = 64 + i % 63, dst = 512 + i % 63;
        expected = data;
        if (kind == 2)
          std::fill(expected.begin() + dst, expected.begin() + dst + count,
                    uint8_t(i));
        else
          reference(expected, data, dst, src, count);
        auto state = prepare(
            memory, address(data.data() + dst),
            kind == 2 ? uint64_t(i) : address(data.data() + src), count);
        success(kind, state, memory);
        check(data == expected, "independent threaded contexts");
      }
    });
  for (auto &thread : threads)
    thread.join();
}
} // namespace
extern "C" void dcp_trace_enter(State &state, uint64_t pc) {
  ++entered;
  enteredState = &state;
  enteredPC = pc;
}
int main() {
  dcp_libc_bulk_reset();
  try {
    smallCopies();
    largeCopiesAndFills();
    stackOverlapAndZero();
    failuresAndCancellation();
    threaded();
  } catch (const std::exception &error) {
    std::fprintf(stderr, "Unexpected fixture exception: %s\n", error.what());
    return 2;
  }
  auto stats = dcp_libc_bulk_stats();
  check(stats.copy_calls == calls[0] && stats.move_calls == calls[1] &&
            stats.fill_calls == calls[2] && stats.bytes == byteCount,
        "atomic per-session totals");
  dcp_libc_bulk_reset();
  auto empty = dcp_libc_bulk_stats();
  check(!empty.copy_calls && !empty.move_calls && !empty.fill_calls &&
            !empty.bytes,
        "quiescent reset");
  std::printf(
      "{\"checks\":%llu,\"failed\":%llu,\"copy_calls\":%llu,\"move_calls\":%"
      "llu,\"fill_calls\":%llu,\"bytes\":%llu,\"threads\":8}\n",
      (unsigned long long)checks.load(), (unsigned long long)failures.load(),
      (unsigned long long)stats.copy_calls,
      (unsigned long long)stats.move_calls,
      (unsigned long long)stats.fill_calls, (unsigned long long)stats.bytes);
  return failures ? 1 : 0;
}
