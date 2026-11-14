#include "runtime.h"
#include <cfenv>
#include <cstring>
#include <mutex>

namespace {
constexpr size_t stripeCount = 64;
constexpr uintptr_t pageSize = 4096;
struct alignas(128) Stripe {
  std::mutex mutex;
};
std::array<Stripe, stripeCount> stripes;
GuestAtomicMutex guestAtomics;
thread_local unsigned exclusiveDepth = 0;
std::mutex registryMutex;
struct RegionCache {
  std::shared_ptr<std::vector<Memory::Region>> registry;
  std::array<Memory::Region, 8> regions{};
};
thread_local RegionCache cache;

// All aliases of the same host page use the same stripe, including a fixture
// page addressed through both the synthetic guest base and a native mapping.
unsigned stripeForPage(uintptr_t page) {
  return (page ^ (page >> 6) ^ (page >> 12)) % stripeCount;
}
uint64_t spanMask(const void *pointer, size_t size) {
  const uintptr_t first = reinterpret_cast<uintptr_t>(pointer) / pageSize;
  if (size > stripeCount * pageSize)
    return UINT64_MAX;
  const size_t offset = reinterpret_cast<uintptr_t>(pointer) % pageSize;
  const size_t extra = size ? (offset + size - 1) / pageSize : 0;
  uint64_t mask = 0;
  for (size_t i = 0; i <= extra; ++i)
    mask |= uint64_t(1) << stripeForPage(first + i);
  return mask;
}
class AccessLock {
public:
  AccessLock(const void *pointer, size_t size) {
    if (exclusiveDepth)
      return;
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    if (size <= pageSize - address % pageSize) {
      single_ = &stripes[stripeForPage(address / pageSize)].mutex;
      single_->lock();
    } else
      acquire(spanMask(pointer, size));
  }
  AccessLock(const void *a, const void *b, size_t bytes) {
    if (!exclusiveDepth)
      acquire(spanMask(a, bytes) | spanMask(b, bytes));
  }
  AccessLock(const AccessLock &) = delete;
  ~AccessLock() {
    if (single_)
      single_->unlock();
    else
      release();
  }

private:
  std::mutex *single_ = nullptr;
  uint64_t held_ = 0;
  void acquire(uint64_t mask) {
    try {
      while (mask) {
        const unsigned index = __builtin_ctzll(mask);
        stripes[index].mutex.lock();
        held_ |= uint64_t(1) << index;
        mask &= mask - 1;
      }
    } catch (...) {
      release();
      throw;
    }
  }
  void release() noexcept {
    while (held_) {
      stripes[__builtin_ctzll(held_)].mutex.unlock();
      held_ &= held_ - 1;
    }
  }
};
} // namespace
// Guest atomics exclude all checked operations. Every multi-stripe path
// acquires its locks in ascending order.
void GuestAtomicMutex::lock() {
  if (exclusiveDepth++ != 0)
    return;
  size_t held = 0;
  try {
    for (; held < stripes.size(); ++held)
      stripes[held].mutex.lock();
  } catch (...) {
    while (held)
      stripes[--held].mutex.unlock();
    --exclusiveDepth;
    throw;
  }
}
void GuestAtomicMutex::unlock() noexcept {
  if (--exclusiveDepth == 0)
    for (auto it = stripes.rbegin(); it != stripes.rend(); ++it)
      it->mutex.unlock();
}
void addRegion(Memory &memory, uint8_t *address, size_t size) {
  std::lock_guard<GuestAtomicMutex> lock(guestAtomics);
  const uint64_t base = reinterpret_cast<uint64_t>(address);
  if (!memory.regions || !address || size > UINT64_MAX - base)
    throw std::runtime_error("invalid guest memory region registration");
  std::lock_guard<std::mutex> registryLock(registryMutex);
  memory.regions->push_back({address, size});
}
static void recordFault(Memory &memory, const char *description) {
  std::lock_guard<std::mutex> lock(registryMutex);
  memory.fault = description;
}
static void checkCancelled(const Memory &memory) {
  if ((memory.stopping && memory.stopping->load()) ||
      (memory.cancelled && memory.cancelled()))
    throw std::runtime_error("bootstrap stopped");
}
static uint8_t *checkedPointer(Memory &memory, uint64_t address, size_t size) {
  if (!memory.bytes.empty() && size <= memory.bytes.size() &&
      address >= Memory::base &&
      address - Memory::base <= memory.bytes.size() - size)
    return memory.bytes.data() + (address - Memory::base);
  if (!memory.regions)
    throw std::runtime_error("missing guest memory registry");
  if (cache.registry != memory.regions) {
    cache.regions = {};
    cache.registry = memory.regions;
  }
  for (size_t i = 0; i < cache.regions.size(); ++i) {
    const auto region = cache.regions[i];
    const uint64_t base = reinterpret_cast<uint64_t>(region.data);
    if (region.data && size <= region.size && address >= base &&
        address - base <= region.size - size) {
      if (i)
        std::swap(cache.regions[0], cache.regions[i]);
      return region.data + (address - base);
    }
  }
  std::lock_guard<std::mutex> registryLock(registryMutex);
  for (const auto &region : *memory.regions) {
    const uint64_t base = reinterpret_cast<uint64_t>(region.data);
    if (region.data && region.size <= UINT64_MAX - base &&
        size <= region.size && address >= base &&
        address - base <= region.size - size) {
      for (size_t i = cache.regions.size() - 1; i; --i)
        cache.regions[i] = cache.regions[i - 1];
      cache.regions[0] = region;
      return region.data + (address - base);
    }
  }
  throw std::runtime_error(
      "guest memory access outside mapped range: " + std::to_string(address) +
      " size " + std::to_string(size));
}
uint8_t *guestPointer(Memory &memory, uint64_t address, size_t size) {
  auto pointer = checkedPointer(memory, address, size);
  AccessLock lock(pointer, size);
  return pointer;
}
template <typename T> static T loadScalar(Memory &memory, uint64_t address) {
  const auto *pointer = checkedPointer(memory, address, sizeof(T));
  AccessLock lock(pointer, sizeof(T));
  T value;
  std::memcpy(&value, pointer, sizeof(value));
  return value;
}
template <typename T>
static void storeScalar(Memory &memory, uint64_t address, T value) {
  auto *pointer = checkedPointer(memory, address, sizeof(T));
  AccessLock lock(pointer, sizeof(T));
  std::memcpy(pointer, &value, sizeof(value));
}
uint64_t read64(Memory &memory, uint64_t address) {
  checkCancelled(memory);
  return loadScalar<uint64_t>(memory, address);
}
void write64(Memory &memory, uint64_t address, uint64_t value) {
  checkCancelled(memory);
  storeScalar(memory, address, value);
}
extern "C" uint64_t __remill_read_memory_64(Memory *memory, uint64_t address) {
  try {
    return read64(*memory, address);
  } catch (const std::runtime_error &error) {
    recordFault(*memory, error.what());
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
    recordFault(*memory, error.what());
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
  if (memory->stopOnFault) {
    [[clang::musttail]] return dispatch(state, address, memory);
  }
  try {
    return dispatch(state, address, memory);
  } catch (const std::runtime_error &error) {
    recordFault(*memory, error.what());
    if (memory->stopOnFault)
      throw;
    return memory;
  }
}
extern "C" Memory *__remill_jump(State &state, uint64_t address,
                                 Memory *memory) {
  if (memory->stopOnFault) {
    [[clang::musttail]] return dispatch(state, address, memory);
  }
  try {
    return dispatch(state, address, memory);
  } catch (const std::runtime_error &error) {
    recordFault(*memory, error.what());
    if (memory->stopOnFault)
      throw;
    return memory;
  }
}
extern "C" Memory *__remill_missing_block(State &state, uint64_t address,
                                          Memory *memory) {
  if (memory->stopOnFault) {
    [[clang::musttail]] return dispatch(state, address, memory);
  }
  try {
    return dispatch(state, address, memory);
  } catch (const std::runtime_error &error) {
    recordFault(*memory, error.what());
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
    checkCancelled(*memory);                                                   \
    TYPE value = 0;                                                            \
    try {                                                                      \
      value = loadScalar<TYPE>(*memory, address);                              \
    } catch (const std::runtime_error &error) {                                \
      recordFault(*memory, error.what());                                      \
      if (memory->stopOnFault)                                                 \
        throw;                                                                 \
    }                                                                          \
    return value;                                                              \
  }                                                                            \
  extern "C" Memory *__remill_write_memory_##BITS(                             \
      Memory *memory, uint64_t address, TYPE value) {                          \
    checkCancelled(*memory);                                                   \
    try {                                                                      \
      storeScalar(*memory, address, value);                                    \
    } catch (const std::runtime_error &error) {                                \
      recordFault(*memory, error.what());                                      \
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
  std::lock_guard<GuestAtomicMutex> lock(guestAtomics);
  try {
    std::memcpy(value.data, checkedPointer(*memory, address, 10), 10);
  } catch (const std::runtime_error &error) {
    recordFault(*memory, error.what());
    if (memory->stopOnFault)
      throw;
    std::memset(value.data, 0, 10);
  }
  return memory;
}
extern "C" Memory *__remill_write_memory_f80(Memory *memory, uint64_t address,
                                             const float80_t &value) {
  checkCancelled(*memory);
  std::lock_guard<GuestAtomicMutex> lock(guestAtomics);
  try {
    std::memcpy(checkedPointer(*memory, address, 10), value.data, 10);
  } catch (const std::runtime_error &error) {
    recordFault(*memory, error.what());
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
  memory->atomicLock = std::unique_lock<GuestAtomicMutex>(guestAtomics);
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
  auto pointer = checkedPointer(*memory, address, sizeof(T));
  AccessLock lock(pointer, sizeof(T));
  T current;
  std::memcpy(&current, pointer, sizeof(T));
  if (current == expected)
    std::memcpy(pointer, &desired, sizeof(T));
  expected = current;
  return memory;
}
extern "C" Memory *__remill_compare_exchange_memory_8(Memory *memory,
                                                      uint64_t address,
                                                      uint8_t &expected,
                                                      uint8_t desired) {
  return compareExchange(memory, address, expected, desired);
}
extern "C" Memory *__remill_compare_exchange_memory_16(Memory *memory,
                                                       uint64_t address,
                                                       uint16_t &expected,
                                                       uint16_t desired) {
  return compareExchange(memory, address, expected, desired);
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

extern "C" Memory *__remill_compare_exchange_memory_128(Memory *memory,
                                                        uint64_t address,
                                                        uint128_t &expected,
                                                        uint128_t &desired) {
  // Remill passes both 128-bit operands by reference, unlike its narrow ABI.
  return compareExchange(memory, address, expected, desired);
}

void guestRead(Memory &memory, uint64_t address, void *output, size_t size) {
  auto pointer = checkedPointer(memory, address, size);
  AccessLock lock(pointer, output, size);
  if (size)
    std::memcpy(output, pointer, size);
}
bool guestMatches(Memory &memory, uint64_t address, const void *snapshot, size_t size) {
  const auto *pointer = checkedPointer(memory, address, size);
  AccessLock lock(pointer, size);
  return !size || std::memcmp(pointer, snapshot, size) == 0;
}
void guestWrite(Memory &memory, uint64_t address, const void *input,
                size_t size) {
  auto pointer = checkedPointer(memory, address, size);
  AccessLock lock(pointer, input, size);
  if (size)
    std::memcpy(pointer, input, size);
}
void guestMove(Memory &memory, uint64_t destination, uint64_t source,
               size_t size) {
  checkCancelled(memory);
  if (!size)
    return;
  auto output = checkedPointer(memory, destination, size);
  auto input = checkedPointer(memory, source, size);
  AccessLock lock(output, input, size);
  std::memmove(output, input, size);
}
void guestFill(Memory &memory, uint64_t destination, uint8_t value,
               size_t size) {
  checkCancelled(memory);
  if (!size)
    return;
  auto output = checkedPointer(memory, destination, size);
  AccessLock lock(output, size);
  std::memset(output, value, size);
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
#define UNSUPPORTED_PORT(BITS, TYPE)                                           \
  extern "C" TYPE __remill_read_io_port_##BITS(Memory *, uint64_t port) {      \
    throw std::runtime_error("unsupported guest I/O port read: " +             \
                             std::to_string(port));                            \
  }                                                                            \
  extern "C" Memory *__remill_write_io_port_##BITS(Memory *, uint64_t port,    \
                                                   TYPE) {                     \
    throw std::runtime_error("unsupported guest I/O port write: " +            \
                             std::to_string(port));                            \
  }
UNSUPPORTED_PORT(8, uint8_t)
UNSUPPORTED_PORT(16, uint16_t)
UNSUPPORTED_PORT(32, uint32_t)
#undef UNSUPPORTED_PORT
