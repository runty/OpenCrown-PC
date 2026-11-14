#pragma once
#include "remill/Arch/X86/Runtime/State.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

struct Memory {
    static constexpr uint64_t base = 0x100000;
    std::array<uint8_t, 65536> bytes{};
    std::string fault;
    unsigned calls = 0;
    unsigned returns = 0;
};
using Lifted = Memory *(*)(State &, uint64_t, Memory *);
Memory *dispatch(State &, uint64_t, Memory *);
uint64_t read64(Memory &, uint64_t);
void write64(Memory &, uint64_t, uint64_t);
extern "C" const char *dcp_cpu_report();
