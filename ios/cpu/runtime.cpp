#include "runtime.h"
#include <cfenv>
#include <cstring>

namespace {
std::recursive_mutex guestAtomics;
}

void addRegion(Memory &memory, uint8_t *address, size_t size) {
  std::lock_guard<std::recursive_mutex> lock(guestAtomics);
  const uint64_t base = reinterpret_cast<uint64_t>(address);
  if (!memory.regions || !address || size > UINT64_MAX - base)
    throw std::runtime_error("invalid guest memory region registration");
  memory.regions->push_back({address, size});
}
static void checkCancelled(const Memory &memory) {
  if ((memory.stopping && memory.stopping->load()) ||
      (memory.cancelled && memory.cancelled()))
    throw std::runtime_error("bootstrap stopped");
}
static uint8_t *checkedPointerLocked(Memory &memory, uint64_t address,
                                    size_t size) {
  if (!memory.bytes.empty() && size <= memory.bytes.size() && address >= Memory::base &&
      address - Memory::base <= memory.bytes.size() - size)
    return memory.bytes.data() + (address - Memory::base);
  if (!memory.regions)
    throw std::runtime_error("missing guest memory registry");
  if (memory.cachedRegistry != memory.regions) {
    memory.cachedRegions = {};
    memory.cachedRegistry = memory.regions;
  }
  for (size_t i = 0; i < memory.cachedRegions.size(); ++i) {
    const auto region = memory.cachedRegions[i];
    const uint64_t base = reinterpret_cast<uint64_t>(region.data);
    if (region.data && size <= region.size && address >= base &&
        address - base <= region.size - size) {
      if (i)
        std::swap(memory.cachedRegions[0], memory.cachedRegions[i]);
      return region.data + (address - base);
    }
  }
  for (const auto &region : *memory.regions) {
    const uint64_t base = reinterpret_cast<uint64_t>(region.data);
    if (region.data && region.size <= UINT64_MAX - base &&
        size <= region.size && address >= base &&
        address - base <= region.size - size) {
      for (size_t i = memory.cachedRegions.size() - 1; i; --i)
        memory.cachedRegions[i] = memory.cachedRegions[i - 1];
      memory.cachedRegions[0] = region;
      return region.data + (address - base);
    }
  }
  throw std::runtime_error(
      "guest memory access outside mapped range: " + std::to_string(address) +
      " size " + std::to_string(size));
}
uint8_t *guestPointer(Memory &memory, uint64_t address, size_t size) {
  std::lock_guard<std::recursive_mutex> lock(guestAtomics);
  return checkedPointerLocked(memory, address, size);
}
uint64_t read64(Memory &memory, uint64_t address) {
  checkCancelled(memory);
  std::lock_guard<std::recursive_mutex> lock(guestAtomics);
  uint64_t value;
  std::memcpy(&value, checkedPointerLocked(memory, address, sizeof(value)),
              sizeof(value));
  return value;
}
void write64(Memory &memory, uint64_t address, uint64_t value) {
  checkCancelled(memory);
  std::lock_guard<std::recursive_mutex> lock(guestAtomics);
  std::memcpy(checkedPointerLocked(memory, address, sizeof(value)), &value,
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
      std::memcpy(&value, checkedPointerLocked(*memory, address, sizeof(value)),       \
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
      std::memcpy(checkedPointerLocked(*memory, address, sizeof(value)), &value,       \
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
// Remill passes the packed 10-byte representation by reference. Never route
// this bridge through the arm64 native floating-point ABI.
static_assert(sizeof(float80_t) == 10, "unexpected Remill extended storage");
extern "C" Memory *__remill_read_memory_f80(Memory *memory, uint64_t address,
                                           float80_t &value) {
  checkCancelled(*memory);
  std::lock_guard<std::recursive_mutex> lock(guestAtomics);
  try {
    std::memcpy(value.data, checkedPointerLocked(*memory, address, 10), 10);
  } catch (const std::runtime_error &error) {
    memory->fault = error.what();
    if (memory->stopOnFault)
      throw;
    std::memset(value.data, 0, 10);
  }
  return memory;
}
extern "C" Memory *__remill_write_memory_f80(Memory *memory, uint64_t address,
                                            const float80_t &value) {
  checkCancelled(*memory);
  std::lock_guard<std::recursive_mutex> lock(guestAtomics);
  try {
    std::memcpy(checkedPointerLocked(*memory, address, 10), value.data, 10);
  } catch (const std::runtime_error &error) {
    memory->fault = error.what();
    if (memory->stopOnFault)
      throw;
  }
  return memory;
}
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
  checkCancelled(*memory);
  return memory;
}
template <typename T>
static Memory *compareExchange(Memory *memory, uint64_t address, T &expected,
                               T desired) {
  checkCancelled(*memory);
  std::lock_guard<std::recursive_mutex> lock(guestAtomics);
  auto pointer = checkedPointerLocked(*memory, address, sizeof(T));
  T current;
  std::memcpy(&current, pointer, sizeof(T));
  if (current == expected)
    std::memcpy(pointer, &desired, sizeof(T));
  expected = current;
  return memory;
}
extern "C" Memory *__remill_compare_exchange_memory_32(Memory *memory,
                                                       uint64_t address,
                                                       uint32_t &expected,
                                                       uint32_t desired) {
  return compareExchange(memory, address, expected, desired);
}
extern "C" Memory *__remill_compare_exchange_memory_64(Memory *memory,
                                                       uint64_t address,
                                                       uint64_t &expected,
                                                       uint64_t desired) {
  return compareExchange(memory, address, expected, desired);
}

void guestRead(Memory &memory, uint64_t address, void *output, size_t size) {
  std::lock_guard<std::recursive_mutex> lock(guestAtomics);
  auto pointer = checkedPointerLocked(memory, address, size);
  if (size)
    std::memcpy(output, pointer, size);
}
void guestWrite(Memory &memory, uint64_t address, const void *input,
                size_t size) {
  std::lock_guard<std::recursive_mutex> lock(guestAtomics);
  auto pointer = checkedPointerLocked(memory, address, size);
  if (size)
    std::memcpy(pointer, input, size);
}

// Remill's x86 rounding enum differs from Darwin arm64 FE_* constants.
extern "C" int32_t __remill_fpu_get_rounding() {
  switch (std::fegetround()) {
  case FE_TONEAREST:
    return 0;
  case FE_DOWNWARD:
    return 1;
  case FE_UPWARD:
    return 2;
  case FE_TOWARDZERO:
    return 3;
  default:
    throw std::runtime_error("unsupported host floating-point rounding mode");
  }
}
extern "C" void __remill_fpu_set_rounding(int32_t mode) {
  const int native[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
  if (mode < 0 || mode > 3 || std::fesetround(native[mode]))
    throw std::runtime_error("invalid guest floating-point rounding mode");
}

// These privileged operations cannot be executed by a PS4 user process. Keep
// speculative traces linkable, but report an executed operation as a fault.
#define UNSUPPORTED_PORT(BITS, TYPE)                                            \
  extern "C" TYPE __remill_read_io_port_##BITS(Memory *, uint64_t port) {       \
    throw std::runtime_error("unsupported guest I/O port read: " +              \
                             std::to_string(port));                            \
  }                                                                            \
  extern "C" Memory *__remill_write_io_port_##BITS(                            \
      Memory *, uint64_t port, TYPE) {                                          \
    throw std::runtime_error("unsupported guest I/O port write: " +             \
                             std::to_string(port));                            \
  }
UNSUPPORTED_PORT(8, uint8_t)
UNSUPPORTED_PORT(16, uint16_t)
UNSUPPORTED_PORT(32, uint32_t)
#undef UNSUPPORTED_PORT
