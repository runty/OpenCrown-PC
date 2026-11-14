#include "runtime.h"
#include <future>
#include <sstream>
#include <vector>
#include "fixtures.h"

Memory *dispatch(State &state, uint64_t pc, Memory *memory) {
    for (const auto &entry : entries)
        if (entry.pc == pc) return entry.function(state, pc, memory);
    throw std::runtime_error("unresolved guest control-flow target");
}
static uint64_t flags(const State &state) {
    return uint64_t(state.aflag.cf) | (uint64_t(state.aflag.pf) << 2) |
           (uint64_t(state.aflag.zf) << 6) | (uint64_t(state.aflag.sf) << 7) |
           (uint64_t(state.aflag.of) << 11);
}
static unsigned run(unsigned worker) {
    unsigned passed = 0;
    for (const auto &test : cases) {
        State state{};
        Memory memory;
        const uint64_t stack = Memory::base + 0xff00;
        const uint64_t data = Memory::base + 0x103; // Exercise unaligned guest memory.
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
        if (!memory.fault.empty() || state.gpr.rax.qword != expected || state.gpr.rsp.qword != stack + 8 ||
            state.gpr.rip.qword != 0xfeedface || memory.returns == 0 ||
            (test.checkFlags && flags(state) != test.flags) ||
            (test.memory && read64(memory, data + 8) != expected) ||
            (test.calls && memory.calls == 0))
            throw std::runtime_error(std::string("fixture mismatch: ") + test.name);
        ++passed;
    }
    // Checked memory rejects both ends and overflow, and accepts the final valid qword.
    Memory memory;
    for (uint64_t address : {Memory::base - 1, Memory::base + 65529, UINT64_MAX}) {
        bool rejected = false;
        try { read64(memory, address); } catch (const std::runtime_error &) { rejected = true; }
        if (!rejected) throw std::runtime_error("invalid guest read accepted");
        rejected = false;
        try { write64(memory, address, 1); } catch (const std::runtime_error &) { rejected = true; }
        if (!rejected) throw std::runtime_error("invalid guest write accepted");
        passed += 2;
    }
    write64(memory, Memory::base + 65528, 42);
    if (read64(memory, Memory::base + 65528) != 42) throw std::runtime_error("last guest qword failed");
    ++passed;
    State state{};
    bool rejected = false;
    try { dispatch(state, 0xbad, &memory); } catch (const std::runtime_error &) { rejected = true; }
    if (!rejected) throw std::runtime_error("unresolved target accepted");
    ++passed;
    // Execute a lifted invalid read. Intrinsics are nounwind, so faults use memory status.
    memory = Memory{};
    state = State{};
    state.gpr.rdi.qword = UINT64_MAX;
    state.gpr.rsp.qword = Memory::base + 0xff00;
    write64(memory, state.gpr.rsp.qword, 0xfeedface);
    dispatch(state, 0x4000, &memory);
    if (memory.fault.empty()) throw std::runtime_error("lifted invalid read accepted");
    return passed + 1;
}
extern "C" const char *dcp_cpu_report() {
    static std::string report;
    try {
        auto first = std::async(std::launch::async, run, 1);
        auto second = std::async(std::launch::async, run, 2);
        unsigned passed = first.get() + second.get();
        std::ostringstream json;
        json << "{\"passed\":" << passed << ",\"failed\":0,\"workers\":2,"
             << "\"routines\":8,\"backend\":\"Remill AOT\","
             << "\"native_reference_routines\":7,\"tls_reference\":\"seeded per-worker guest memory\","
             << "\"corpus_sha256\":\"" << corpus_hash << "\"}";
        report = json.str();
    } catch (const std::exception &error) {
        // Fixture names and harness diagnostics are controlled by this source tree.
        report = std::string("{\"failed\":1,\"error\":\"") + error.what() + "\"}";
    }
    return report.c_str();
}
