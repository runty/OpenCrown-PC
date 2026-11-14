#include "runtime.h"
#include <cstring>

static uint8_t *checked(Memory &memory, uint64_t address, size_t size) {
    if (address < Memory::base || address - Memory::base > memory.bytes.size() - size)
        throw std::runtime_error("guest memory access outside mapped range");
    return memory.bytes.data() + (address - Memory::base);
}
uint64_t read64(Memory &memory, uint64_t address) {
    uint64_t value;
    std::memcpy(&value, checked(memory, address, sizeof(value)), sizeof(value));
    return value;
}
void write64(Memory &memory, uint64_t address, uint64_t value) {
    std::memcpy(checked(memory, address, sizeof(value)), &value, sizeof(value));
}
extern "C" uint64_t __remill_read_memory_64(Memory *memory, uint64_t address) {
    try { return read64(*memory, address); }
    catch (const std::runtime_error &error) { memory->fault = error.what(); return 0; }
}
extern "C" Memory *__remill_write_memory_64(Memory *memory, uint64_t address, uint64_t value) {
    try { write64(*memory, address, value); }
    catch (const std::runtime_error &error) { memory->fault = error.what(); }
    return memory;
}
// Remill has already computed these flags. Variadic operands describe provenance.
extern "C" bool __remill_flag_computation_carry(bool value, ...) { return value; }
extern "C" bool __remill_flag_computation_zero(bool value, ...) { return value; }
extern "C" bool __remill_flag_computation_sign(bool value, ...) { return value; }
extern "C" bool __remill_flag_computation_overflow(bool value, ...) { return value; }
extern "C" Memory *__remill_function_return(State &, uint64_t, Memory *memory) {
    ++memory->returns;
    return memory;
}
extern "C" Memory *__remill_function_call(State &state, uint64_t address, Memory *memory) {
    ++memory->calls;
    try { return dispatch(state, address, memory); }
    catch (const std::runtime_error &error) { memory->fault = error.what(); return memory; }
}
extern "C" Memory *__remill_jump(State &state, uint64_t address, Memory *memory) {
    try { return dispatch(state, address, memory); }
    catch (const std::runtime_error &error) { memory->fault = error.what(); return memory; }
}
extern "C" Memory *__remill_missing_block(State &state, uint64_t address, Memory *memory) {
    try { return dispatch(state, address, memory); }
    catch (const std::runtime_error &error) { memory->fault = error.what(); return memory; }
}
extern "C" bool __remill_compare_eq(bool value) { return value; }
extern "C" bool __remill_compare_ult(bool value) { return value; }
// The ISA leaves these bits undefined. Choose zero, and exclude them from comparisons.
extern "C" uint8_t __remill_undefined_8() { return 0; }
