#include "runtime.h"

#include "fixtures.h"
#include <cmath>
#include <cstring>
#include <future>
#include <sstream>
#include <vector>

Memory *dispatch(State &state, uint64_t pc, Memory *memory) {
  for (const auto &entry : entries)
    if (entry.pc == pc)
      return entry.function(state, pc, memory);
  throw std::runtime_error("unresolved guest control-flow target");
}
static uint64_t flags(const State &state) {
  return uint64_t(state.aflag.cf) | (uint64_t(state.aflag.pf) << 2) |
         (uint64_t(state.aflag.zf) << 6) | (uint64_t(state.aflag.sf) << 7) |
         (uint64_t(state.aflag.of) << 11);
}
static bool reciprocalMatches(uint64_t actual, uint64_t reference) {
  for (unsigned i = 0; i < 2; ++i) {
    uint32_t a = actual >> (i * 32), r = reference >> (i * 32);
    float af, rf;
    std::memcpy(&af, &a, 4);
    std::memcpy(&rf, &r, 4);
    if (std::isnan(rf)) {
      if (!std::isnan(af))
        return false;
    } else if (std::isinf(rf) || rf == 0.0f) {
      if (a != r)
        return false;
    } else if (!std::isfinite(af) || std::fabs((af - rf) / rf) > 0.000367f)
      return false;
  }
  return true;
}
static unsigned run(unsigned worker) {
  unsigned passed = 0;
  for (const auto &test : cases) {
    State state{};
    Memory memory;
    const uint64_t stack = Memory::base + 0xff00;
    const uint64_t data =
        Memory::base + 0x103; // Exercise unaligned guest memory.
    const uint64_t tls = Memory::base + 0x200;
    state.gpr.rdi.qword = test.memory ? data : test.a;
    state.gpr.rsi.qword = test.indirect ? test.pc + 16 : test.b;
    state.gpr.rsp.qword = stack;
    state.addr.fs_base.qword = tls;
    const uint64_t tlsValue = test.a ^ (uint64_t(worker) << 40);
    write64(memory, data, test.a);
    write64(memory, tls, tlsValue);
    write64(memory, stack, 0xfeedface);
    dispatch(state, test.pc, &memory);
    const uint64_t expected = test.tls ? tlsValue + test.b : test.result;
    if (!memory.fault.empty() ||
        (test.approximate ? !reciprocalMatches(state.gpr.rax.qword, expected)
                          : state.gpr.rax.qword != expected) ||
        state.gpr.rsp.qword != stack + 8 || state.gpr.rip.qword != 0xfeedface ||
        memory.returns == 0 ||
        (test.checkFlags && flags(state) != test.flags) ||
        (test.memory && read64(memory, data + 8) != expected) ||
        (test.calls && memory.calls == 0))
      throw std::runtime_error(
          std::string("fixture mismatch: ") + test.name +
          " a=" + std::to_string(test.a) + " b=" + std::to_string(test.b) +
          " actual=" + std::to_string(state.gpr.rax.qword) +
          " expected=" + std::to_string(expected));
    ++passed;
  }
  // Checked memory rejects both ends and overflow, and accepts the final valid
  // qword.
  Memory memory;
  for (uint64_t address :
       {Memory::base - 1, Memory::base + 65529, UINT64_MAX}) {
    bool rejected = false;
    try {
      read64(memory, address);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    if (!rejected)
      throw std::runtime_error("invalid guest read accepted");
    rejected = false;
    try {
      write64(memory, address, 1);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    if (!rejected)
      throw std::runtime_error("invalid guest write accepted");
    passed += 2;
  }
  write64(memory, Memory::base + 65528, 42);
  if (read64(memory, Memory::base + 65528) != 42)
    throw std::runtime_error("last guest qword failed");
  ++passed;
  // Extended memory bridges preserve all ten bytes, accept the final complete
  // value and reject partial/overflow accesses without modifying neighbours.
  float80_t extended{};
  for (unsigned i = 0; i < 10; ++i)
    extended.data[i] = uint8_t(0x31 + i * 17);
  for (uint64_t address : {Memory::base + 3, Memory::base + 65526}) {
    __remill_write_memory_f80(&memory, address, extended);
    float80_t copy{};
    __remill_read_memory_f80(&memory, address, copy);
    if (std::memcmp(copy.data, extended.data, 10))
      throw std::runtime_error("extended memory copy lost bits");
    ++passed;
  }
  memory.stopOnFault = true;
  const auto before = memory.bytes;
  for (uint64_t address : {Memory::base - 1, Memory::base + 65527, UINT64_MAX}) {
    bool readRejected = false, writeRejected = false;
    try {
      __remill_read_memory_f80(&memory, address, extended);
    } catch (const std::runtime_error &) { readRejected = true; }
    try {
      __remill_write_memory_f80(&memory, address, extended);
    } catch (const std::runtime_error &) { writeRejected = true; }
    if (!readRejected || !writeRejected || memory.bytes != before)
      throw std::runtime_error("invalid extended memory access accepted");
    passed += 2;
  }
  State state{};
  bool rejected = false;
  try {
    dispatch(state, 0xbad, &memory);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  if (!rejected)
    throw std::runtime_error("unresolved target accepted");
  ++passed;
  // Execute a lifted invalid read. Intrinsics are nounwind, so faults use
  // memory status.
  memory = Memory{};
  state = State{};
  state.gpr.rdi.qword = UINT64_MAX;
  state.gpr.rsp.qword = Memory::base + 0xff00;
  write64(memory, state.gpr.rsp.qword, 0xfeedface);
  dispatch(state, 0x4000, &memory);
  if (memory.fault.empty())
    throw std::runtime_error("lifted invalid read accepted");
  return passed + 1;
}
extern "C" const char *dcp_cpu_report() {
  static std::string report;
  try {
    auto first = std::async(std::launch::async, run, 1);
    auto second = std::async(std::launch::async, run, 2);
    unsigned passed = first.get() + second.get();
    std::array<uint8_t, 16> shared{};
    uint64_t atomic64Pc = 0;
    for (const auto &test : cases)
      if (!std::strcmp(test.name, "atomic64")) atomic64Pc = test.pc;
    if (!atomic64Pc) throw std::runtime_error("missing 64-bit CAS fixture");
    auto increment = [&shared](uint64_t pc) {
      Memory memory;
      memory.stopOnFault = true;
      addRegion(memory, shared.data(), shared.size());
      for (unsigned i = 0; i < 100; ++i) {
        State state{};
        state.gpr.rdi.qword = reinterpret_cast<uint64_t>(shared.data());
        state.gpr.rsp.qword = Memory::base + 0xff00;
        write64(memory, state.gpr.rsp.qword, 0xfeedface);
        dispatch(state, pc, &memory);
        if (!memory.fault.empty())
          throw std::runtime_error(memory.fault);
      }
    };
    auto counterFirst = std::async(std::launch::async, increment, 0xa000);
    auto counterSecond = std::async(std::launch::async, increment, 0xa000);
    counterFirst.get();
    counterSecond.get();
    uint32_t total;
    std::memcpy(&total, shared.data(), 4);
    if (total != 200)
      throw std::runtime_error("concurrent guest CAS lost an increment");
    ++passed;
    shared.fill(0);
    auto wideFirst = std::async(std::launch::async, increment, atomic64Pc);
    auto wideSecond = std::async(std::launch::async, increment, atomic64Pc);
    wideFirst.get();
    wideSecond.get();
    uint64_t wideTotal;
    std::memcpy(&wideTotal, shared.data(), 8);
    if (wideTotal != 200)
      throw std::runtime_error("concurrent 64-bit guest CAS lost an increment");
    ++passed;
    std::ostringstream json;
    json << "{\"passed\":" << passed << ",\"failed\":0,\"workers\":2,"
         << "\"routines\":" << fixture_count << ",\"backend\":\"Remill AOT\","
         << "\"native_reference_routines\":" << fixture_count - 1
         << ",\"tls_reference\":\"seeded per-worker guest memory\","
         << "\"corpus_sha256\":\"" << corpus_hash << "\"}";
    report = json.str();
  } catch (const std::exception &error) {
    // Fixture names and harness diagnostics are controlled by this source tree.
    report = std::string("{\"failed\":1,\"error\":\"") + error.what() + "\"}";
  }
  return report.c_str();
}
