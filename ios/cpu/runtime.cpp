#include "runtime.h"
#include <cstring>

namespace {
std::recursive_mutex guestAtomics;
}

void addRegion(Memory &memory, uint8_t *address, size_t size) {
  std::lock_guard<std::recursive_mutex> lock(guestAtomics);
  memory.regions->push_back({address, size});
}
static void checkCancelled(const Memory &memory) {
  if ((memory.stopping && memory.stopping->load()) ||
      (memory.cancelled && memory.cancelled()))
    throw std::runtime_error("bootstrap stopped");
}
uint8_t *guestPointer(Memory &memory, uint64_t address, size_t size) {
  std::lock_guard<std::recursive_mutex> lock(guestAtomics);
  if (size <= memory.bytes.size() && address >= Memory::base &&
      address - Memory::base <= memory.bytes.size() - size)
    return memory.bytes.data() + (address - Memory::base);
  for (const auto &region : *memory.regions) {
    const uint64_t base = reinterpret_cast<uint64_t>(region.data);
    if (size <= region.size && address >= base &&
        address - base <= region.size - size)
      return region.data + (address - base);
  }
  throw std::runtime_error(
      "guest memory access outside mapped range: " + std::to_string(address) +
      " size " + std::to_string(size));
}
uint64_t read64(Memory &memory, uint64_t address) {
  checkCancelled(memory);
  std::lock_guard<std::recursive_mutex> lock(guestAtomics);
  uint64_t value;
  std::memcpy(&value, guestPointer(memory, address, sizeof(value)),
              sizeof(value));
  return value;
}
void write64(Memory &memory, uint64_t address, uint64_t value) {
  checkCancelled(memory);
  std::lock_guard<std::recursive_mutex> lock(guestAtomics);
  std::memcpy(guestPointer(memory, address, sizeof(value)), &value,
              sizeof(value));
}
extern "C" uint64_t __remill_read_memory_64(Memory *memory, uint64_t address) {
  try {
    return read64(*memory, address);
  } catch (const std::runtime_error &error) {
    memory->fault = error.what();
    if (memory->stopOnFault)
      throw;
    return 0;
  }
}
extern "C" Memory *__remill_write_memory_64(Memory *memory, uint64_t address,
                                            uint64_t value) {
  try {
    write64(*memory, address, value);
  } catch (const std::runtime_error &error) {
    memory->fault = error.what();
    if (memory->stopOnFault)
      throw;
  }
  return memory;
}
// Remill has already computed these flags. Variadic operands describe
// provenance.
extern "C" bool __remill_flag_computation_carry(bool value, ...) {
  return value;
}
extern "C" bool __remill_flag_computation_zero(bool value, ...) {
  return value;
}
extern "C" bool __remill_flag_computation_sign(bool value, ...) {
  return value;
}
extern "C" bool __remill_flag_computation_overflow(bool value, ...) {
  return value;
}
extern "C" Memory *__remill_function_return(State &, uint64_t, Memory *memory) {
  ++memory->returns;
  return memory;
}
extern "C" Memory *__remill_function_call(State &state, uint64_t address,
                                          Memory *memory) {
  ++memory->calls;
  try {
    return dispatch(state, address, memory);
  } catch (const std::runtime_error &error) {
    memory->fault = error.what();
    if (memory->stopOnFault)
      throw;
    return memory;
  }
}
extern "C" Memory *__remill_jump(State &state, uint64_t address,
                                 Memory *memory) {
  try {
    return dispatch(state, address, memory);
  } catch (const std::runtime_error &error) {
    memory->fault = error.what();
    if (memory->stopOnFault)
      throw;
    return memory;
  }
}
extern "C" Memory *__remill_missing_block(State &state, uint64_t address,
                                          Memory *memory) {
  try {
    return dispatch(state, address, memory);
  } catch (const std::runtime_error &error) {
    memory->fault = error.what();
    if (memory->stopOnFault)
      throw;
    return memory;
  }
}
extern "C" bool __remill_compare_eq(bool value) { return value; }
extern "C" bool __remill_compare_ult(bool value) { return value; }
// The ISA leaves these bits undefined. Choose zero, and exclude them from
// comparisons.
extern "C" uint8_t __remill_undefined_8() { return 0; }

// Scalar bridges used by actual module startup and byte/string routines.
#define SCALAR_MEMORY(BITS, TYPE)                                              \
  extern "C" TYPE __remill_read_memory_##BITS(Memory *memory,                  \
                                              uint64_t address) {              \
    std::lock_guard<std::recursive_mutex> lock(guestAtomics);                  \
    checkCancelled(*memory);                                                   \
    TYPE value = 0;                                                            \
    try {                                                                      \
      std::memcpy(&value, guestPointer(*memory, address, sizeof(value)),       \
                  sizeof(value));                                              \
    } catch (const std::runtime_error &error) {                                \
      memory->fault = error.what();                                            \
      if (memory->stopOnFault)                                                 \
        throw;                                                                 \
    }                                                                          \
    return value;                                                              \
  }                                                                            \
  extern "C" Memory *__remill_write_memory_##BITS(                             \
      Memory *memory, uint64_t address, TYPE value) {                          \
    checkCancelled(*memory);                                                   \
    std::lock_guard<std::recursive_mutex> lock(guestAtomics);                  \
    try {                                                                      \
      std::memcpy(guestPointer(*memory, address, sizeof(value)), &value,       \
                  sizeof(value));                                              \
    } catch (const std::runtime_error &error) {                                \
      memory->fault = error.what();                                            \
      if (memory->stopOnFault)                                                 \
        throw;                                                                 \
    }                                                                          \
    return memory;                                                             \
  }
SCALAR_MEMORY(8, uint8_t)
SCALAR_MEMORY(16, uint16_t)
SCALAR_MEMORY(32, uint32_t)
SCALAR_MEMORY(f32, float)
SCALAR_MEMORY(f64, double)
#undef SCALAR_MEMORY
extern "C" bool __remill_compare_ule(bool value) { return value; }
extern "C" bool __remill_compare_slt(bool value) { return value; }
extern "C" bool __remill_compare_sle(bool value) { return value; }
extern "C" bool __remill_compare_neq(bool value) { return value; }
extern "C" bool __remill_compare_ugt(bool value) { return value; }
extern "C" bool __remill_compare_uge(bool value) { return value; }
extern "C" bool __remill_compare_sgt(bool value) { return value; }
extern "C" bool __remill_compare_sge(bool value) { return value; }
extern "C" Memory *__remill_barrier_store_store(Memory *memory) {
  __atomic_thread_fence(__ATOMIC_RELEASE);
  return memory;
}
extern "C" Memory *__remill_barrier_load_load(Memory *memory) {
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  return memory;
}
extern "C" Memory *__remill_barrier_load_store(Memory *memory) {
  __atomic_thread_fence(__ATOMIC_ACQ_REL);
  return memory;
}
extern "C" Memory *__remill_barrier_store_load(Memory *memory) {
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  return memory;
}

extern "C" Memory *__remill_atomic_begin(Memory *memory) {
  checkCancelled(*memory);
  if (memory->atomicLock.owns_lock())
    throw std::runtime_error("nested guest atomic region");
  memory->atomicLock = std::unique_lock<std::recursive_mutex>(guestAtomics);
  return memory;
}
extern "C" Memory *__remill_atomic_end(Memory *memory) {
  if (!memory->atomicLock.owns_lock())
    throw std::runtime_error("unbalanced guest atomic region");
  memory->atomicLock.unlock();
  return memory;
}
extern "C" Memory *__remill_compare_exchange_memory_32(Memory *memory,
                                                       uint64_t address,
                                                       uint32_t &expected,
                                                       uint32_t desired) {
  std::lock_guard<std::recursive_mutex> lock(guestAtomics);
  auto pointer = guestPointer(*memory, address, 4);
  uint32_t current;
  std::memcpy(&current, pointer, 4);
  if (current == expected)
    std::memcpy(pointer, &desired, 4);
  expected = current;
  return memory;
}

void guestRead(Memory &memory, uint64_t address, void *output, size_t size) {
  std::lock_guard<std::recursive_mutex> lock(guestAtomics);
  std::memcpy(output, guestPointer(memory, address, size), size);
}
void guestWrite(Memory &memory, uint64_t address, const void *input,
                size_t size) {
  std::lock_guard<std::recursive_mutex> lock(guestAtomics);
  std::memcpy(guestPointer(memory, address, size), input, size);
}
