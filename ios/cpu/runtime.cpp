#include "runtime.h"
#include "scalar-profile.h"
#if DCP_SCALAR_PROFILE
#include "scalar-profile.inc"
#endif
#include <cfenv>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>
#ifdef __APPLE__
#include <os/lock.h>
#endif

struct RegionCache {
  const std::vector<Memory::Region> *registry = nullptr;
  std::array<Memory::Region, 8> regions{};
  // Each nonzero tag proves that one registered native mapping covers the
  // entire page. Partial pages and cross-page accesses use the range checks.
  std::array<uint64_t, 256> pageTags{};
};
struct GuestScalarThread {
  RegionCache cache{};
  unsigned exclusiveDepth = 0;
#if DCP_SCALAR_PROFILE
  dcp::cpu::profile::Thread *profile = nullptr;
#endif
};
static thread_local GuestScalarThread scalarThread{};
void bindScalarThread(Memory &memory) {
  memory.scalarThread = &scalarThread;
#if DCP_SCALAR_PROFILE
  scalarThread.profile = dcp::cpu::profile::thread();
#endif
}
namespace {
constexpr size_t stripeCount = 64;
constexpr uintptr_t pageSize = 4096; // validation granularity
// Guest atlas comparisons are multi-megabyte reads. A 4 KiB lock band
// turned each into a global lock even for workers on unrelated mappings.
constexpr uintptr_t stripeBytes = 256 * 1024;
#ifdef __APPLE__
struct StripeMutex {
  os_unfair_lock value = OS_UNFAIR_LOCK_INIT;
  void lock() noexcept { os_unfair_lock_lock(&value); }
  void unlock() noexcept { os_unfair_lock_unlock(&value); }
};
#else
using StripeMutex = std::mutex;
#endif
struct VersionPage {
  uint64_t version = 1;
  bool escaped = false, watched = false;
};
struct alignas(128) Stripe {
  StripeMutex mutex;
  // Fast negative filter: ordinary stores outside watched pages avoid a map
  // lookup. Every access is protected by the corresponding existing stripe.
  std::array<uint64_t, 64> watched{};
  std::unordered_map<uintptr_t, std::shared_ptr<VersionPage>> pages;
};
std::array<Stripe, stripeCount> stripes;
GuestAtomicMutex guestAtomics;
std::atomic<bool> snapshotsEnabled{false};
std::atomic<bool> verifySnapshots{false};
std::atomic<uint64_t> snapshotHits{0}, snapshotVerified{0},
    snapshotVerifiedBytes{0}, snapshotFailures{0};
std::atomic<uintptr_t> watchedFirst{UINTPTR_MAX}, watchedEnd{0};
uint64_t snapshotGeneration = 1;
struct RegistryTracking {
  std::shared_ptr<std::vector<Memory::Region>> owner;
  std::unordered_map<size_t, Memory::Region> ranges;
};
std::unordered_map<const void *, RegistryTracking> trackedRegistries;

std::mutex registryMutex;
// Only the miss path needs a nontrivial TLS initializer and destructor.
thread_local std::shared_ptr<std::vector<Memory::Region>> registryOwner;

// All aliases of the same host page use the same stripe, including a fixture
// page addressed through both the synthetic guest base and a native mapping.
unsigned stripeForBand(uintptr_t page) {
  return (page ^ (page >> 6) ^ (page >> 12)) % stripeCount;
}
uintptr_t filterIndex(uintptr_t page) {
  return (page ^ (page >> 12) ^ (page >> 24)) & 4095;
}
Stripe &stripeForPage(uintptr_t page) {
  return stripes[stripeForBand(page / (stripeBytes / pageSize))];
}
template <class F> void pagesIn(const void *pointer, size_t size, F operation) {
  if (!size)
    return;
  const auto address = reinterpret_cast<uintptr_t>(pointer);
  const auto first = address / pageSize;
  const auto last = (address + size - 1) / pageSize;
  for (auto page = first;; ++page) {
    operation(page, stripeForPage(page));
    if (page == last)
      break;
  }
}
[[gnu::noinline]] void changedPages(const void *pointer, size_t size) {
  pagesIn(pointer, size, [](uintptr_t page, Stripe &stripe) {
    const auto bit = filterIndex(page);
    if (!(stripe.watched[bit / 64] & (uint64_t(1) << (bit % 64))))
      return;
    auto it = stripe.pages.find(page);
    if (it == stripe.pages.end() || !it->second->watched)
      return;
    auto &entry = *it->second;
#ifdef DCP_SNAPSHOT_TEST_VERSION_LIMIT
    constexpr uint64_t limit = DCP_SNAPSHOT_TEST_VERSION_LIMIT;
#else
    constexpr uint64_t limit = std::numeric_limits<uint64_t>::max();
#endif
    if (entry.version == limit)
      entry.escaped = true;
    else
      ++entry.version;
  });
}
[[gnu::always_inline]] static inline void changed(const void *pointer,
                                                  size_t size) {
  if (!size)
    return;
  const auto address = reinterpret_cast<uintptr_t>(pointer);
  if (address + size - 1 < watchedFirst.load(std::memory_order_relaxed) ||
      address >= watchedEnd.load(std::memory_order_relaxed))
    return;
  if (size <= pageSize - address % pageSize) {
    const auto page = address / pageSize;
    const auto bit = filterIndex(page);
    if (!(stripeForPage(page).watched[bit / 64] & (uint64_t(1) << (bit % 64))))
      return;
  }
  changedPages(pointer, size);
}
void escaped(const void *pointer, size_t size) {
  if (!snapshotsEnabled.load(std::memory_order_relaxed))
    return;
  pagesIn(pointer, size, [](uintptr_t page, Stripe &stripe) {
    auto &entry = stripe.pages[page];
    if (!entry)
      entry = std::make_shared<VersionPage>();
    entry->escaped = true;
  });
}
uint64_t spanMask(const void *pointer, size_t size) {
  const uintptr_t first = reinterpret_cast<uintptr_t>(pointer) / stripeBytes;
  if (size > stripeCount * stripeBytes)
    return UINT64_MAX;
  const size_t offset = reinterpret_cast<uintptr_t>(pointer) % stripeBytes;
  const size_t extra = size ? (offset + size - 1) / stripeBytes : 0;
  uint64_t mask = 0;
  for (size_t i = 0; i <= extra; ++i)
    mask |= uint64_t(1) << stripeForBand(first + i);
  return mask;
}
class AccessLock {
public:
  AccessLock(const void *pointer, size_t size, unsigned depth = scalarThread.exclusiveDepth) {
    if (depth)
      return;
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    if (size <= stripeBytes - address % stripeBytes) {
      single_ = &stripes[stripeForBand(address / stripeBytes)].mutex;
      single_->lock();
    } else
      acquire(spanMask(pointer, size));
  }
  AccessLock(const void *a, const void *b, size_t bytes) {
    if (!scalarThread.exclusiveDepth)
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
  StripeMutex *single_ = nullptr;
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
  if (scalarThread.exclusiveDepth++ != 0)
    return;
  size_t held = 0;
  try {
    for (; held < stripes.size(); ++held)
      stripes[held].mutex.lock();
  } catch (...) {
    while (held)
      stripes[--held].mutex.unlock();
    --scalarThread.exclusiveDepth;
    throw;
  }
}
void GuestAtomicMutex::unlock() noexcept {
  if (--scalarThread.exclusiveDepth == 0)
    for (auto it = stripes.rbegin(); it != stripes.rend(); ++it)
      it->mutex.unlock();
}
static void registerRegion(Memory &memory, uint8_t *address, size_t size,
                           bool track) {
  std::lock_guard<GuestAtomicMutex> lock(guestAtomics);
  const uint64_t base = reinterpret_cast<uint64_t>(address);
  if (!memory.regions || !address || size > UINT64_MAX - base)
    throw std::runtime_error("invalid guest memory region registration");
  std::lock_guard<std::mutex> registryLock(registryMutex);
  const auto index = memory.regions->size();
  changed(address, size);
  memory.regions->push_back({address, size});
  if (track && snapshotsEnabled.load(std::memory_order_relaxed)) {
    auto &registry = trackedRegistries[memory.regions.get()];
    registry.owner = memory.regions;
    registry.ranges.emplace(index, Memory::Region{address, size});
  }
  // Registration never revives escaped pages. Re-registering backing
  // invalidates prior proofs even if its address and size are reused.
}
void addRegion(Memory &memory, uint8_t *address, size_t size) {
  registerRegion(memory, address, size, false);
}
void addSnapshotRegion(Memory &memory, uint8_t *address, size_t size) {
  registerRegion(memory, address, size, true);
}
void configureGuestSnapshots(bool enabled, bool verify) {
  std::lock_guard<GuestAtomicMutex> lock(guestAtomics);
  std::lock_guard<std::mutex> registryLock(registryMutex);
  snapshotsEnabled = false;
  for (auto &stripe : stripes) {
    stripe.pages.clear();
    stripe.watched.fill(0);
  }
  trackedRegistries.clear();
  watchedFirst = UINTPTR_MAX;
  watchedEnd = 0;
  if (snapshotGeneration &&
      snapshotGeneration != std::numeric_limits<uint64_t>::max())
    ++snapshotGeneration;
  else
    snapshotGeneration = 0;
  // Zero is terminal: never revive proofs after a generation wraps.
  snapshotsEnabled = enabled && snapshotGeneration != 0;
  verifySnapshots = verify;
  snapshotHits = snapshotVerified = snapshotVerifiedBytes = snapshotFailures =
      0;
}
GuestSnapshotDiagnostics guestSnapshotDiagnostics() {
  return {snapshotsEnabled.load(),      verifySnapshots.load(),
          snapshotHits.load(),          snapshotVerified.load(),
          snapshotVerifiedBytes.load(), snapshotFailures.load()};
}
static void recordFault(Memory &memory, const char *description) {
  std::lock_guard<std::mutex> lock(registryMutex);
  memory.fault = description;
}
[[gnu::noinline]] static void checkSignals(const Memory &memory, uint64_t generation) {
  if (memory.stopping && memory.stopping->load())
    throw std::runtime_error("bootstrap stopped");
  if (memory.poll.paused && memory.poll.serviceStop &&
      !memory.poll.paused->load() && !memory.poll.serviceStop->load() &&
      (!memory.poll.workerCancel || !memory.poll.workerCancel->load())) {
    if (memory.signalCache)
      memory.signalCache->lastClear.store(generation, std::memory_order_relaxed);
    return;
  }
  if (memory.cancelled && memory.cancelled())
    throw std::runtime_error("bootstrap stopped");
}
[[gnu::always_inline]] static inline void checkCancelled(const Memory &memory) {
  auto *cache = memory.signalCache;
  uint64_t generation = 0;
  if (cache) {
    generation = cache->generation.load(std::memory_order_acquire);
    if (cache->lastClear.load(std::memory_order_relaxed) == generation)
      return;
  }
  checkSignals(memory, generation);
}
[[gnu::noinline]] static uint8_t *lookupRegion(Memory &memory,
                                               RegionCache &cache,
                                               uint64_t address, size_t size) {
  if (cache.registry != memory.regions.get()) {
    cache = {};
    registryOwner = memory.regions;
    cache.registry = registryOwner.get();
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
static uint8_t *checkedPointer(Memory &memory, uint64_t address, size_t size) {
  if (!memory.bytes.empty() && size <= memory.bytes.size() &&
      address >= Memory::base &&
      address - Memory::base <= memory.bytes.size() - size)
    return memory.bytes.data() + (address - Memory::base);
  if (!memory.regions)
    throw std::runtime_error("missing guest memory registry");
  auto &cache = scalarThread.cache;
  if (cache.registry != memory.regions.get())
    return lookupRegion(memory, cache, address, size);
  const uint64_t page = address / pageSize;
  auto &tag = cache.pageTags[(page ^ (page >> 8)) % cache.pageTags.size()];
  const bool withinPage = size && size <= pageSize - address % pageSize;
  if (withinPage && tag == page + 1)
    return reinterpret_cast<uint8_t *>(address);
  uint8_t *pointer = nullptr;
  for (size_t i = 0; i < cache.regions.size(); ++i) {
    const auto region = cache.regions[i];
    const uint64_t base = reinterpret_cast<uint64_t>(region.data);
    if (region.data && size <= region.size && address >= base &&
        address - base <= region.size - size) {
      if (i)
        std::swap(cache.regions[0], cache.regions[i]);
      pointer = region.data + (address - base);
      break;
    }
  }
  if (!pointer)
    pointer = lookupRegion(memory, cache, address, size);
  const auto region = cache.regions[0];
  const uint64_t base = reinterpret_cast<uint64_t>(region.data);
  const uint64_t pageStart = page * pageSize;
  if (withinPage && pageStart >= base && region.size >= pageSize &&
      pageStart - base <= region.size - pageSize)
    tag = page + 1;
  return pointer;
}
uint8_t *guestPointer(Memory &memory, uint64_t address, size_t size) {
  auto pointer = checkedPointer(memory, address, size);
  AccessLock lock(pointer, size);
  escaped(pointer, size);
  return pointer;
}
void guestValidate(Memory &memory, uint64_t address, size_t size) {
  auto pointer = checkedPointer(memory, address, size);
  AccessLock lock(pointer, size);
}
static bool snapshotEligible(Memory &memory, const uint8_t *pointer,
                             size_t size) {
  std::lock_guard<std::mutex> registryLock(registryMutex);
  const auto registry = trackedRegistries.find(memory.regions.get());
  if (registry == trackedRegistries.end())
    return false;
  const auto first = reinterpret_cast<uintptr_t>(pointer);
  const auto last = first + size;
  bool covered = false;
  for (size_t i = 0; i < memory.regions->size(); ++i) {
    const auto &range = memory.regions->at(i);
    const auto base = reinterpret_cast<uintptr_t>(range.data);
    if (!range.data || range.size > UINTPTR_MAX - base)
      continue;
    if (first >= base + range.size || base >= last)
      continue;
    const auto known = registry->second.ranges.find(i);
    if (known == registry->second.ranges.end() ||
        known->second.data != range.data || known->second.size != range.size)
      return false;
    covered |= first >= base && size <= range.size &&
               first - base <= range.size - size;
  }
  return covered;
}
struct GuestSnapshot::Impl {
  struct Observed {
    std::shared_ptr<VersionPage> page;
    uint64_t version;
  };
  std::shared_ptr<std::vector<Memory::Region>> owner;
  const uint8_t *pointer = nullptr;
  std::vector<uint8_t> bytes;
  std::vector<Observed> proof;
  uint64_t generation = 0;
  void refresh(Memory &memory) {
    proof.clear();
    generation = 0;
    if (!snapshotsEnabled.load(std::memory_order_relaxed) ||
        !snapshotEligible(memory, pointer, bytes.size()))
      return;
    bool valid = true;
    pagesIn(pointer, bytes.size(), [&](uintptr_t page, Stripe &stripe) {
      if (!valid)
        return;
      auto &entry = stripe.pages[page];
      if (!entry)
        entry = std::make_shared<VersionPage>();
      if (entry->escaped) {
        valid = false;
        return;
      }
      entry->watched = true;
      const auto bit = filterIndex(page);
      stripe.watched[bit / 64] |= uint64_t(1) << (bit % 64);
      proof.push_back({entry, entry->version});
    });
    if (valid && !proof.empty()) {
      generation = snapshotGeneration;
      auto first = watchedFirst.load(std::memory_order_relaxed);
      const auto address = reinterpret_cast<uintptr_t>(pointer);
      while (address < first &&
             !watchedFirst.compare_exchange_weak(first, address,
                                                 std::memory_order_relaxed)) {
      }
      auto end = watchedEnd.load(std::memory_order_relaxed);
      const auto limit = address + bytes.size();
      while (limit > end && !watchedEnd.compare_exchange_weak(
                                end, limit, std::memory_order_relaxed)) {
      }
    } else
      proof.clear();
  }
};
GuestSnapshot::GuestSnapshot(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
GuestSnapshot::~GuestSnapshot() = default;
const uint8_t *GuestSnapshot::data() const { return impl_->bytes.data(); }
size_t GuestSnapshot::size() const { return impl_->bytes.size(); }
size_t GuestSnapshot::capacity() const {
  return impl_->bytes.capacity() +
         impl_->proof.capacity() * sizeof(Impl::Observed);
}
std::shared_ptr<GuestSnapshot> guestSnapshot(Memory &memory, uint64_t address,
                                             size_t size) {
  if (!size || size > (64u << 20) || address > UINT64_MAX - size)
    throw std::runtime_error("invalid guest snapshot span");
  auto pointer = checkedPointer(memory, address, size);
  auto impl = std::make_unique<GuestSnapshot::Impl>();
  impl->owner = memory.regions;
  impl->pointer = pointer;
  impl->bytes.resize(size);
  AccessLock lock(pointer, size);
  std::memcpy(impl->bytes.data(), pointer, size);
  impl->refresh(memory);
  return std::shared_ptr<GuestSnapshot>(new GuestSnapshot(std::move(impl)));
}
GuestSnapshotMatch GuestSnapshot::matches(Memory &memory, uint64_t address) {
  auto &s = *impl_;
  if (s.owner != memory.regions)
    return GuestSnapshotMatch::Invalid;
  auto pointer = checkedPointer(memory, address, s.bytes.size());
  if (pointer != s.pointer)
    return GuestSnapshotMatch::Invalid;
  AccessLock lock(pointer, s.bytes.size());
  if (s.generation && s.generation == snapshotGeneration &&
      snapshotsEnabled.load(std::memory_order_relaxed) &&
      snapshotEligible(memory, pointer, s.bytes.size())) {
    bool same = true;
    for (const auto &observed : s.proof)
      same &=
          !observed.page->escaped && observed.page->version == observed.version;
    if (same) {
      if (verifySnapshots.load(std::memory_order_relaxed)) {
        snapshotVerified.fetch_add(1, std::memory_order_relaxed);
        snapshotVerifiedBytes.fetch_add(s.bytes.size(),
                                        std::memory_order_relaxed);
        if (std::memcmp(pointer, s.bytes.data(), s.bytes.size())) {
          snapshotFailures.fetch_add(1, std::memory_order_relaxed);
          return GuestSnapshotMatch::Different;
        }
      }
      snapshotHits.fetch_add(1, std::memory_order_relaxed);
      return GuestSnapshotMatch::Version;
    }
  }
  if (std::memcmp(pointer, s.bytes.data(), s.bytes.size()))
    return GuestSnapshotMatch::Different;
  s.refresh(memory);
  return GuestSnapshotMatch::Content;
}
// The bound thread already has the registry proof for a whole native page.
// Keep that common case inside the bridge; unusual spans still go through
// the same synthetic, registry and boundary checks used by host hooks.
template <typename T>
[[gnu::always_inline]] static inline uint8_t *scalarPointer(
    Memory &memory, uint64_t address, const GuestScalarThread &thread) {
  const uint64_t page = address / pageSize;
  const auto &cache = thread.cache;
  if (address > Memory::base && address - Memory::base >= memory.bytes.size() &&
      cache.registry == memory.regions.get() &&
      sizeof(T) <= pageSize - address % pageSize &&
      cache.pageTags[(page ^ (page >> 8)) % cache.pageTags.size()] == page + 1)
    return reinterpret_cast<uint8_t *>(address);
  return checkedPointer(memory, address, sizeof(T));
}

template <typename T, bool lifted = false>
static T loadScalar(Memory &memory, uint64_t address) {
  auto &thread = lifted && memory.scalarThread ? *memory.scalarThread : scalarThread;
  const auto *pointer = scalarPointer<T>(memory, address, thread);
#if DCP_SCALAR_PROFILE
  const auto native = reinterpret_cast<uintptr_t>(pointer);
  const auto stripe = sizeof(T) <= stripeBytes - native % stripeBytes
      ? stripeForBand(native / stripeBytes) : 64;
  auto sample = dcp::cpu::profile::scalar(thread.profile, lifted, false,
      sizeof(T), pointer, stripe, thread.exclusiveDepth != 0);
#endif
  AccessLock lock(pointer, sizeof(T), thread.exclusiveDepth);
#if DCP_SCALAR_PROFILE
  sample.acquired();
#endif
  T value;
  std::memcpy(&value, pointer, sizeof(value));
  return value;
}
template <typename T, bool lifted = false>
static void storeScalar(Memory &memory, uint64_t address, T value) {
  auto &thread = lifted && memory.scalarThread ? *memory.scalarThread : scalarThread;
  auto *pointer = scalarPointer<T>(memory, address, thread);
#if DCP_SCALAR_PROFILE
  const auto native = reinterpret_cast<uintptr_t>(pointer);
  const auto stripe = sizeof(T) <= stripeBytes - native % stripeBytes
      ? stripeForBand(native / stripeBytes) : 64;
  auto sample = dcp::cpu::profile::scalar(thread.profile, lifted, true,
      sizeof(T), pointer, stripe, thread.exclusiveDepth != 0);
#endif
  AccessLock lock(pointer, sizeof(T), thread.exclusiveDepth);
#if DCP_SCALAR_PROFILE
  sample.acquired();
#endif
  changed(pointer, sizeof(T));
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
    checkCancelled(*memory);
    return loadScalar<uint64_t, true>(*memory, address);
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
    checkCancelled(*memory);
    storeScalar<uint64_t, true>(*memory, address, value);
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
      value = loadScalar<TYPE, true>(*memory, address);                              \
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
      storeScalar<TYPE, true>(*memory, address, value);                                    \
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
    auto pointer = checkedPointer(*memory, address, 10);
    changed(pointer, 10);
    std::memcpy(pointer, value.data, 10);
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
  if (current == expected) {
    changed(pointer, sizeof(T));
    std::memcpy(pointer, &desired, sizeof(T));
  }
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
bool guestMatches(Memory &memory, uint64_t address, const void *snapshot,
                  size_t size) {
  const auto *pointer = checkedPointer(memory, address, size);
  AccessLock lock(pointer, size);
  return !size || std::memcmp(pointer, snapshot, size) == 0;
}
void guestWrite(Memory &memory, uint64_t address, const void *input,
                size_t size) {
  auto pointer = checkedPointer(memory, address, size);
  AccessLock lock(pointer, input, size);
  changed(pointer, size);
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
  changed(output, size);
  std::memmove(output, input, size);
}
void guestFill(Memory &memory, uint64_t destination, uint8_t value,
               size_t size) {
  checkCancelled(memory);
  if (!size)
    return;
  auto output = checkedPointer(memory, destination, size);
  AccessLock lock(output, size);
  changed(output, size);
  std::memset(output, value, size);
}

void guestMaxBytes(Memory &memory, uint64_t destination, const uint8_t *input,
                   size_t size) {
  auto output = checkedPointer(memory, destination, size);
  AccessLock lock(output, input, size);
  changed(output, size);
  for (size_t i = 0; i < size; ++i)
    if (input[i] > output[i])
      output[i] = input[i];
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
