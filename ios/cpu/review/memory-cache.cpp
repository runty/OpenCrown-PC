// Independent cache/bounds/concurrency regression fixture; no game
// instructions.
#include "runtime.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <thread>
#include <vector>

Memory *dispatch(State &, uint64_t, Memory *memory) { return memory; }
extern "C" uint8_t __remill_read_memory_8(Memory *, uint64_t);
extern "C" uint16_t __remill_read_memory_16(Memory *, uint64_t);
extern "C" uint32_t __remill_read_memory_32(Memory *, uint64_t);
extern "C" float __remill_read_memory_f32(Memory *, uint64_t);
extern "C" double __remill_read_memory_f64(Memory *, uint64_t);
extern "C" Memory *__remill_write_memory_8(Memory *, uint64_t, uint8_t);
extern "C" Memory *__remill_write_memory_16(Memory *, uint64_t, uint16_t);
extern "C" Memory *__remill_write_memory_32(Memory *, uint64_t, uint32_t);
extern "C" Memory *__remill_write_memory_f32(Memory *, uint64_t, float);
extern "C" Memory *__remill_write_memory_f64(Memory *, uint64_t, double);
extern "C" Memory *__remill_compare_exchange_memory_8(Memory *, uint64_t,
                                                      uint8_t &, uint8_t);
extern "C" Memory *__remill_compare_exchange_memory_16(Memory *, uint64_t,
                                                       uint16_t &, uint16_t);
extern "C" Memory *__remill_compare_exchange_memory_32(Memory *, uint64_t,
                                                       uint32_t &, uint32_t);
extern "C" Memory *__remill_compare_exchange_memory_64(Memory *, uint64_t,
                                                       uint64_t &, uint64_t);
extern "C" Memory *__remill_compare_exchange_memory_128(Memory *, uint64_t,
                                                        uint128_t &, uint128_t &);
extern "C" Memory *__remill_atomic_begin(Memory *);
extern "C" Memory *__remill_atomic_end(Memory *);
namespace {
std::atomic<uint64_t> checks{0}, failures{0};
void check(bool ok, const char *name) {
  ++checks;
  if (!ok) {
    if (failures.fetch_add(1) < 20)
      std::fprintf(stderr, "FAIL %s\n", name);
  }
}
template <class F> void rejects(F function, const char *name) {
  bool caught = false;
  try {
    function();
  } catch (const std::runtime_error &) {
    caught = true;
  }
  check(caught, name);
}
uint64_t address(const void *pointer) {
  return reinterpret_cast<uint64_t>(pointer);
}
std::atomic<bool> callbackStop{false};
bool cancelled() { return callbackStop.load(); }
std::mutex callbackMutex;
std::condition_variable callbackChanged;
bool callbackEntered = false, callbackReleased = false;
bool blockingCancelled() {
  std::unique_lock lock(callbackMutex);
  callbackEntered = true;
  callbackChanged.notify_all();
  callbackChanged.wait(lock, [] { return callbackReleased; });
  return false;
}
void callbackDoesNotHoldRegistryLock() {
  Memory memory;
  memory.bytes[32] = 0x5a;
  memory.cancelled = blockingCancelled;
  {
    std::lock_guard lock(callbackMutex);
    callbackEntered = callbackReleased = false;
  }
  std::atomic<bool> observerDone{false};
  uint8_t workerByte = 0, observerByte = 0;
  std::thread worker(
      [&] { workerByte = __remill_read_memory_8(&memory, Memory::base + 32); });
  bool entered;
  {
    std::unique_lock lock(callbackMutex);
    entered = callbackChanged.wait_for(lock, std::chrono::seconds(2),
                                       [] { return callbackEntered; });
  }
  std::thread observer([&] {
    guestRead(memory, Memory::base + 32, &observerByte, 1);
    {
      std::lock_guard lock(callbackMutex);
      observerDone = true;
    }
    callbackChanged.notify_all();
  });
  bool progressed;
  {
    std::unique_lock lock(callbackMutex);
    progressed = callbackChanged.wait_for(lock, std::chrono::seconds(1),
                                          [&] { return observerDone.load(); });
    // Always release/join, even with the old deadlocking lock order. A failure
    // becomes a bounded assertion instead of hanging the sanitizer process.
    callbackReleased = true;
  }
  callbackChanged.notify_all();
  worker.join();
  observer.join();
  check(entered, "scalar cancellation callback entered");
  check(progressed, "paused cancellation callback does not hold registry lock");
  check(workerByte == 0x5a && observerByte == 0x5a,
        "callback and concurrent native read complete");
}

void outerAtomicExcludesContender() {
  std::array<uint64_t, 2> data{0, 0};
  Memory outer, nested, contender;
  addRegion(outer, reinterpret_cast<uint8_t *>(data.data()), sizeof(data));
  nested.regions = contender.regions = outer.regions;
  std::atomic<bool> entered{false}, done{false};
  uint64_t observed = 0;
  __remill_atomic_begin(&outer);
  // Two contexts on the same thread exercise real recursive lock ownership.
  __remill_atomic_begin(&nested);
  write64(outer, address(data.data()), 123);
  uint64_t expected = 123;
  __remill_compare_exchange_memory_64(&nested, address(data.data()), expected, 456);
  std::thread worker([&] {
    entered = true;
    observed = read64(contender, address(data.data()));
    done = true;
  });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (!entered.load() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  const bool started = entered.load();
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  const bool blockedByBoth = !done.load();
  __remill_atomic_end(&nested);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  const bool blockedByOuter = !done.load();
  // Acquired in runtime.cpp, released here: verifies one TLS identity across
  // translation units, including unique_lock's inlined cleanup path.
  outer.atomicLock.unlock();
  worker.join();
  check(started, "atomic contender scheduled");
  check(expected == 123, "recursive CAS under outer atomic region");
  check(blockedByBoth && blockedByOuter, "contender waits for final outer unlock");
  check(done && observed == 456, "contender sees committed atomic data");
  check(!outer.atomicLock.owns_lock() && !nested.atomicLock.owns_lock(),
        "cross-unit outer lock fully released");
}

unsigned signalPolls = 0;
void boundPollSignals() {
  Memory memory;
  std::atomic<bool> paused{false}, serviceStop{false}, workerCancel{false}, stopped{false};
  memory.stopping = &stopped;
  memory.cancelled = [] { ++signalPolls; return false; };
  memory.poll = {&paused, &serviceStop, &workerCancel};
  read64(memory, Memory::base);
  check(signalPolls == 0, "clear bound runtime signals");
  for (auto *flag : {&paused, &serviceStop, &workerCancel}) {
    const auto before = signalPolls;
    *flag = true;
    read64(memory, Memory::base);
    *flag = false;
    check(signalPolls == before + 1, "each pending signal calls the lifecycle handler");
  }
  stopped = true;
  rejects([&] { read64(memory, Memory::base); }, "stop precedes clear bound signals");
  stopped = false;
  memory.poll = {};
  read64(memory, Memory::base);
  check(signalPolls == 4, "unbound generic callback always runs");
}

void boundExecution() {
  Memory memory;
  bindScalarThread(memory);
  memory.stopOnFault = true;
  alignas(4096) std::array<uint8_t, 8192> data{};
  const uint64_t start = address(data.data());
  addRegion(memory, data.data(), 4096);
  __remill_write_memory_32(&memory, start + 4092, 42);
  check(__remill_read_memory_32(&memory, start + 4092) == 42, "bound scalar page hit");
  rejects([&] { __remill_read_memory_32(&memory, start + 4093); }, "bound cross-page fault");
  memory.regions = std::make_shared<std::vector<Memory::Region>>();
  rejects([&] { __remill_read_memory_32(&memory, start); }, "bound page revocation");

  std::atomic<uint64_t> generation{1};
  std::atomic<bool> stop{false}, paused{false}, service{false}, cancel{false};
  Memory::SignalCache signals{generation};
  memory.signalCache = &signals;
  memory.stopping = &stop;
  memory.poll = {&paused, &service, &cancel};
  memory.cancelled = [] { ++signalPolls; return false; };
  const unsigned before = signalPolls;
  __remill_read_memory_32(&memory, Memory::base);
  check(signals.lastClear.load() == 1, "bound clear signal generation");
  for (auto *flag : {&paused, &service, &cancel}) {
    *flag = true;
    ++generation;
    __remill_read_memory_32(&memory, Memory::base);
    __remill_read_memory_32(&memory, Memory::base);
    *flag = false;
    ++generation;
    __remill_read_memory_32(&memory, Memory::base);
  }
  check(signalPolls == before + 6, "pending signals remain visible until cleared");
  stop = true;
  ++generation;
  rejects([&] { __remill_read_memory_32(&memory, Memory::base); }, "bound stop");
}

void overlappingPages() {
  Memory memory;
  // Include an alias crossing the 256 KiB synchronization boundary.
  memory.bytes.resize(512 * 1024);
  // Register the backing a second way. Alternate between a low guest address
  // and its host alias in the middle of an unaligned, two-page write.
  addRegion(memory, memory.bytes.data(), memory.bytes.size());
  const auto base = address(memory.bytes.data());
  const size_t offset = 256 * 1024 - (base % (256 * 1024)) - 1;
  const uint64_t low = Memory::base + offset, high = base + offset;
  constexpr uint64_t first = 0x1122334455667788ULL, second = ~first;
  write64(memory, low, first);
  std::thread writer([&] {
    for (unsigned i = 0; i < 16000; ++i) {
      const uint64_t value = i % 2 ? first : second;
      guestWrite(memory, high, &value, sizeof(value));
    }
  });
  for (unsigned i = 0; i < 16000; ++i) {
    const uint64_t value = read64(memory, low);
    check(value == first || value == second, "cross-page alias snapshot");
  }
  writer.join();
  check(guestMatches(memory, low, &first, sizeof(first)), "matched complete snapshot");
  check(!guestMatches(memory, high, &second, sizeof(second)), "mismatched complete snapshot");
}

void boundaries() {
  Memory memory;
  std::vector<uint8_t> backing(4096 * 768, 0);
  constexpr unsigned mapped = 512;
  auto region = [&](unsigned i) { return backing.data() + 4096 * i + 64; };
  auto length = [](unsigned i) { return size_t(97 + i % 159); };
  for (unsigned i = 0; i < mapped; ++i)
    addRegion(memory, region(i), length(i));
  // Repeatedly churn eight cache entries across hundreds of disjoint mappings.
  for (unsigned round = 0; round < 9; ++round)
    for (unsigned j = 0; j < mapped; ++j) {
      unsigned i = (j * 173 + round * 71) % mapped;
      auto *base = region(i);
      size_t size = length(i);
      check(guestPointer(memory, address(base), size) == base,
            "complete range");
      check(guestPointer(memory, address(base + size), 0) == base + size,
            "one-past zero span");
      check(guestPointer(memory, address(base + 3), 8) == base + 3,
            "unaligned range");
      uint64_t value = (uint64_t(round) << 40) | i;
      write64(memory, address(base + 1), value);
      check(read64(memory, address(base + 1)) == value, "unaligned64");
      uint8_t bytes[17];
      for (unsigned k = 0; k < 17; ++k)
        bytes[k] = uint8_t(i + k);
      guestWrite(memory, address(base + 11), bytes, 17);
      uint8_t out[17]{};
      guestRead(memory, address(base + 11), out, 17);
      check(!std::memcmp(out, bytes, 17), "bulk read/write");
      rejects([&] { guestPointer(memory, address(base) - 1, 1); },
              "before start rejected");
      rejects([&] { guestPointer(memory, address(base + size), 1); },
              "one-past byte rejected");
      rejects([&] { guestPointer(memory, address(base + size - 3), 4); },
              "cross-boundary rejected");
      rejects(
          [&] {
            guestPointer(memory, address(base),
                         std::numeric_limits<size_t>::max());
          },
          "overflowing size rejected");
    }
  rejects([&] { guestPointer(memory, UINT64_MAX, 8); },
          "overflowing address rejected");
  rejects([&] { guestPointer(memory, 0, 0); },
          "unmapped null zero span rejected");
  rejects([&] { addRegion(memory, nullptr, 0); },
          "null empty registration rejected");
  rejects([&] { addRegion(memory, nullptr, 8); }, "null registration rejected");
  rejects(
      [&] {
        addRegion(memory, reinterpret_cast<uint8_t *>(UINT64_MAX - 3), 8);
      },
      "registration overflow rejected");
  // Even malformed caller-populated registry entries must never authorize a
  // read.
  memory.regions->push_back({nullptr, 1024});
  memory.regions->push_back({reinterpret_cast<uint8_t *>(UINT64_MAX - 3), 8});
  rejects([&] { guestPointer(memory, 0, 1); }, "malformed null entry skipped");
  rejects([&] { guestPointer(memory, UINT64_MAX - 1, 1); },
          "malformed overflowing entry skipped");
  addRegion(memory, region(mapped), 0);
  check(guestPointer(memory, address(region(mapped)), 0) == region(mapped),
        "valid empty mapping");
  rejects([&] { guestPointer(memory, address(region(mapped)), 1); },
          "empty mapping has no bytes");
  guestRead(memory, address(region(mapped)), nullptr, 0);
  guestWrite(memory, address(region(mapped)), nullptr, 0);
  check(true, "zero length nullable buffers");
  // A later containing registration must still be consulted after cached
  // misses.
  auto *larger = region(mapped + 1);
  addRegion(memory, larger, 16);
  guestPointer(memory, address(larger), 8);
  addRegion(memory, larger, 128);
  check(guestPointer(memory, address(larger + 8), 80) == larger + 8,
        "larger append after cached subset");
  memory.regions.reset();
  check(guestPointer(memory, Memory::base, 8) == memory.bytes.data(),
        "synthetic mapping independent of registry");
  rejects([&] { guestPointer(memory, address(region(0)), 8); },
          "absent native registry rejected");
  rejects([&] { addRegion(memory, region(0), 8); },
          "absent registry registration rejected");
  check(guestPointer(memory, Memory::base + memory.bytes.size(), 0) ==
            memory.bytes.data() + memory.bytes.size(),
        "synthetic endpoint");
  memory.bytes.clear();
  rejects([&] { guestPointer(memory, Memory::base, 0); },
          "empty synthetic storage rejected");
}

void replacement() {
  Memory memory;
  for (unsigned i = 0; i < 512; ++i) {
    auto bytes = std::make_unique<uint8_t[]>(128);
    uint64_t old = address(bytes.get());
    addRegion(memory, bytes.get(), 128);
    write64(memory, old, i);
    check(read64(memory, old) == i, "replacement warm cache");
    std::weak_ptr<std::vector<Memory::Region>> oldRegistry = memory.regions;
    memory.regions = std::make_shared<std::vector<Memory::Region>>();
    // Revocation occurs while this Memory is quiescent; backing can then die.
    bytes.reset();
    rejects([&] { read64(memory, old); },
            "replaced registry revokes cached allocation");
    check(oldRegistry.expired(),
          "old strong registry released after invalidation");
  }
}

void bridges() {
  Memory memory;
  std::vector<uint8_t> bytes(256, 0);
  addRegion(memory, bytes.data(), bytes.size());
  const auto a = address(bytes.data() + 1);
  __remill_write_memory_8(&memory, a, 0xab);
  check(__remill_read_memory_8(&memory, a) == 0xab, "8 bridge");
  __remill_write_memory_16(&memory, a, 0xabcd);
  check(__remill_read_memory_16(&memory, a) == 0xabcd, "16 bridge");
  __remill_write_memory_32(&memory, a, 0xabcddcba);
  check(__remill_read_memory_32(&memory, a) == 0xabcddcba, "32 bridge");
  __remill_write_memory_f32(&memory, a, 1.25f);
  check(__remill_read_memory_f32(&memory, a) == 1.25f, "f32 bridge");
  __remill_write_memory_f64(&memory, a, -2.75);
  check(__remill_read_memory_f64(&memory, a) == -2.75, "f64 bridge");
  float80_t input{}, output{};
  for (unsigned i = 0; i < 10; ++i)
    input.data[i] = uint8_t(10 + i);
  __remill_write_memory_f80(&memory, a, input);
  __remill_read_memory_f80(&memory, a, output);
  check(!std::memcmp(input.data, output.data, 10), "f80 exact bytes");
  uint32_t expected32 = 123;
  __remill_write_memory_32(&memory, a, 321);
  __remill_compare_exchange_memory_32(&memory, a, expected32, 999);
  check(expected32 == 321 && __remill_read_memory_32(&memory, a) == 321,
        "CAS32 failure updates expected");
  __remill_compare_exchange_memory_32(&memory, a, expected32, 999);
  check(expected32 == 321 && __remill_read_memory_32(&memory, a) == 999,
        "CAS32 success");
  write64(memory, a, 456);
  uint64_t expected64 = 123;
  __remill_compare_exchange_memory_64(&memory, a, expected64, 999);
  check(expected64 == 456 && read64(memory, a) == 456,
        "CAS64 failure updates expected");
  __remill_compare_exchange_memory_64(&memory, a, expected64, 999);
  check(expected64 == 456 && read64(memory, a) == 999, "CAS64 success");
  std::atomic<bool> stopping{true};
  memory.stopping = &stopping;
  rejects([&] { read64(memory, a); }, "read cancellation");
  rejects([&] { write64(memory, a, 0); }, "write cancellation");
  rejects(
      [&] { __remill_compare_exchange_memory_32(&memory, a, expected32, 0); },
      "CAS32 cancellation");
  rejects(
      [&] { __remill_compare_exchange_memory_64(&memory, a, expected64, 0); },
      "CAS64 cancellation");
  rejects([&] { __remill_read_memory_16(&memory, a); },
          "scalar read cancellation");
  rejects([&] { __remill_write_memory_f80(&memory, a, input); },
          "extended write cancellation");
  rejects([&] { __remill_atomic_begin(&memory); }, "atomic entry cancellation");
  stopping = false;
  memory.cancelled = cancelled;
  callbackStop = true;
  rejects([&] { read64(memory, a); }, "callback cancellation");
  callbackStop = false;
  __remill_atomic_begin(&memory);
  rejects([&] { __remill_atomic_begin(&memory); }, "nested atomic rejected");
  stopping = true;
  rejects([&] { __remill_atomic_end(&memory); }, "atomic end cancellation");
  check(!memory.atomicLock.owns_lock(),
        "atomic released before cancellation throw");
  stopping = false;
  write64(memory, a, 42);
  check(read64(memory, a) == 42, "usable after cancelled atomic end");
  rejects([&] { __remill_atomic_end(&memory); }, "unbalanced atomic rejected");
  check(__remill_read_memory_8(&memory, 0) == 0 && !memory.fault.empty(),
        "fault recorded and zero returned");
  memory.stopOnFault = true;
  rejects([&] { __remill_read_memory_8(&memory, 0); },
          "fault throws with stopOnFault");
}

void narrowAtomics() {
  std::array<uint8_t, 32> bytes;
  bytes.fill(0xa5);
  Memory root;
  addRegion(root, bytes.data(), bytes.size());
  const uint64_t byteAddress = address(bytes.data() + 3),
                 wordAddress = address(bytes.data() + 7);
  __remill_write_memory_8(&root, byteAddress, 123);
  __remill_write_memory_16(&root, wordAddress, 456);
  uint8_t expected8 = 0;
  uint16_t expected16 = 0;
  __remill_compare_exchange_memory_8(&root, byteAddress, expected8, 9);
  __remill_compare_exchange_memory_16(&root, wordAddress, expected16, 9);
  check(expected8 == 123 && bytes[3] == 123, "CAS8 failure updates expected");
  check(expected16 == 456 && __remill_read_memory_16(&root, wordAddress) == 456,
        "CAS16 failure updates expected");
  __remill_compare_exchange_memory_8(&root, byteAddress, expected8, 9);
  __remill_compare_exchange_memory_16(&root, wordAddress, expected16, 9);
  check(bytes[3] == 9 && expected8 == 123, "CAS8 success");
  check(__remill_read_memory_16(&root, wordAddress) == 9 && expected16 == 456,
        "CAS16 success");
  check(bytes[2] == 0xa5 && bytes[4] == 0xa5 && bytes[6] == 0xa5 &&
            bytes[9] == 0xa5,
        "narrow CAS preserves adjacent bytes");
  rejects(
      [&] {
        __remill_compare_exchange_memory_16(&root, address(bytes.data() + 31),
                                            expected16, 0);
      },
      "CAS16 crossing end rejects");
  rejects(
      [&] {
        __remill_compare_exchange_memory_8(&root, address(bytes.data() + 32),
                                           expected8, 0);
      },
      "CAS8 past end rejects");
  std::atomic<bool> stop{true};
  root.stopping = &stop;
  rejects(
      [&] {
        __remill_compare_exchange_memory_8(&root, byteAddress, expected8, 0);
      },
      "CAS8 cancellation");
  rejects(
      [&] {
        __remill_compare_exchange_memory_16(&root, wordAddress, expected16, 0);
      },
      "CAS16 cancellation");
  stop = false;
  __remill_write_memory_8(&root, byteAddress, 0);
  __remill_write_memory_16(&root, wordAddress, 0);
  std::vector<std::thread> threads;
  std::atomic<bool> go{false};
  for (unsigned worker = 0; worker < 8; ++worker)
    threads.emplace_back([&] {
      Memory local;
      local.regions = root.regions;
      while (!go.load())
        std::this_thread::yield();
      for (unsigned i = 0; i < 4000; ++i) {
        if (i < 20) {
          uint8_t current = __remill_read_memory_8(&local, byteAddress);
          for (;;) {
            uint8_t expected = current;
            __remill_compare_exchange_memory_8(&local, byteAddress, expected,
                                               uint8_t(current + 1));
            if (expected == current)
              break;
            current = expected;
          }
        }
        uint16_t current = __remill_read_memory_16(&local, wordAddress);
        for (;;) {
          uint16_t expected = current;
          __remill_compare_exchange_memory_16(&local, wordAddress, expected,
                                              uint16_t(current + 1));
          if (expected == current)
            break;
          current = expected;
        }
      }
    });
  go = true;
  for (auto &thread : threads)
    thread.join();
  check(__remill_read_memory_8(&root, byteAddress) == 160,
        "CAS8 exact no-wrap concurrent count");
  check(__remill_read_memory_16(&root, wordAddress) == 32000,
        "CAS16 exact no-wrap concurrent count");
  check(bytes[2] == 0xa5 && bytes[4] == 0xa5 && bytes[6] == 0xa5 &&
            bytes[9] == 0xa5,
        "concurrent narrow CAS canaries");
}

void wideAtomics() {
  alignas(16) std::array<uint8_t, 64> bytes;
  bytes.fill(0xa5);
  Memory root;
  addRegion(root, bytes.data(), bytes.size());
  const uint64_t target = address(bytes.data() + 16);
  const uint128_t original = (uint128_t(0xfedcba9876543210ULL) << 64) |
                             0x0123456789abcdefULL;
  uint128_t expected = 0, desired = ~original, actual = 0;
  guestWrite(root, target, &original, 16);
  check(__remill_compare_exchange_memory_128(&root, target, expected, desired) == &root,
        "CAS128 returns memory token");
  guestRead(root, target, &actual, 16);
  check(expected == original && actual == original && desired == ~original,
        "CAS128 failure updates full expected and preserves desired");
  __remill_atomic_begin(&root);
  __remill_compare_exchange_memory_128(&root, target, expected, desired);
  __remill_atomic_end(&root);
  guestRead(root, target, &actual, 16);
  check(expected == original && actual == desired && desired == ~original,
        "CAS128 success inside serialized atomic region");
  check(std::all_of(bytes.begin(), bytes.begin()+16, [](uint8_t b){return b==0xa5;}) &&
        std::all_of(bytes.begin()+32, bytes.end(), [](uint8_t b){return b==0xa5;}),
        "CAS128 preserves surrounding bytes");
  const auto before = bytes;
  rejects([&]{__remill_compare_exchange_memory_128(&root, address(bytes.data()+49), expected, desired);},
          "CAS128 partial boundary rejects");
  rejects([&]{__remill_compare_exchange_memory_128(&root, UINT64_MAX-7, expected, desired);},
          "CAS128 overflow rejects");
  std::atomic<bool> stop{true};
  root.stopping = &stop;
  rejects([&]{__remill_compare_exchange_memory_128(&root, target, expected, desired);},
          "CAS128 cancellation");
  check(bytes == before, "CAS128 rejected writes preserve all bytes");
  stop = false;
  // Carry across the low 64 bits while retaining a correlated high half.
  const uint128_t start = (uint128_t(17) << 64) | (UINT64_MAX - 100);
  guestWrite(root, target, &start, 16);
  std::vector<std::thread> threads;
  for (unsigned worker=0; worker<8; ++worker)
    threads.emplace_back([&]{
      Memory local;
      local.regions = root.regions;
      for(unsigned i=0;i<4000;++i){
        uint128_t current;
        guestRead(local, target, &current, 16);
        for(;;){
          uint128_t wanted=current+1, seen=current;
          __remill_compare_exchange_memory_128(&local, target, seen, wanted);
          if(seen==current) break;
          current=seen;
        }
      }
    });
  for(auto &thread:threads) thread.join();
  guestRead(root,target,&actual,16);
  check(actual == start+32000, "CAS128 exact concurrent count with 64-bit carry");
}

void concurrent() {
  constexpr unsigned workers = 8, initial = 400, additional = 400,
                     iterations = 12000, stride = 512;
  std::vector<uint8_t> bytes((initial + additional) * stride, 0);
  Memory root;
  auto pointer = [&](unsigned i) { return bytes.data() + i * stride + 32; };
  for (unsigned i = 0; i < initial; ++i)
    addRegion(root, pointer(i), 256);
  const uint64_t counter64 = address(pointer(0) + 1),
                 counter32 = address(pointer(0) + 13);
  write64(root, counter64, 0);
  __remill_write_memory_32(&root, counter32, 0);
  std::atomic<bool> go{false};
  std::atomic<unsigned> published{initial};
  std::vector<std::thread> threads;
  threads.emplace_back([&] {
    while (!go.load())
      std::this_thread::yield();
    for (unsigned i = initial; i < initial + additional; ++i) {
      addRegion(root, pointer(i), 256);
      published.store(i + 1, std::memory_order_release);
    }
  });
  for (unsigned worker = 0; worker < workers; ++worker)
    threads.emplace_back([&, worker] {
      Memory local;
      local.regions = root.regions;
      while (!go.load())
        std::this_thread::yield();
      uint64_t io = address(pointer(worker + 1) + 3);
      for (unsigned i = 0; i < iterations; ++i) {
        uint64_t value = (uint64_t(worker) << 32) | i;
        write64(local, io, value);
        check(read64(local, io) == value, "concurrent private IO");
        uint32_t small = __remill_read_memory_32(&local, counter32);
        for (;;) {
          uint32_t expected = small;
          __remill_compare_exchange_memory_32(&local, counter32, expected,
                                              small + 1);
          if (expected == small)
            break;
          small = expected;
        }
        uint64_t large = read64(local, counter64);
        for (;;) {
          uint64_t expected = large;
          __remill_compare_exchange_memory_64(&local, counter64, expected,
                                              large + 1);
          if (expected == large)
            break;
          large = expected;
        }
        unsigned count = published.load(std::memory_order_acquire),
                 index = (i * 173 + worker * 43) % count;
        check(guestPointer(local, address(pointer(index)), 256) ==
                  pointer(index),
              "concurrent append/cache churn");
      }
    });
  go = true;
  for (auto &thread : threads)
    thread.join();
  check(read64(root, counter64) == uint64_t(workers) * iterations,
        "CAS64 exact concurrent count");
  check(__remill_read_memory_32(&root, counter32) == workers * iterations,
        "CAS32 exact concurrent count");
  check(root.regions->size() == initial + additional,
        "all appended regions retained");
  // A single Memory's valid accesses may also share its cache safely under the
  // lock.
  threads.clear();
  for (unsigned worker = 0; worker < workers; ++worker)
    threads.emplace_back([&, worker] {
      auto a = address(pointer(worker + 1) + 17);
      for (unsigned i = 0; i < 2000; ++i) {
        write64(root, a, i);
        check(read64(root, a) == i, "shared-context valid cache access");
      }
    });
  for (auto &thread : threads)
    thread.join();
}
} // namespace
int main() {
  try {
#ifdef MEMORY_CACHE_BLOCKING_ONLY
    callbackDoesNotHoldRegistryLock();
#else
    boundaries();
    replacement();
    bridges();
    outerAtomicExcludesContender();
    overlappingPages();
    boundPollSignals();
    boundExecution();
    narrowAtomics();
    wideAtomics();
    concurrent();
    callbackDoesNotHoldRegistryLock();
#endif
  } catch (const std::exception &error) {
    std::fprintf(stderr, "Unexpected fixture exception: %s\n", error.what());
    return 2;
  }
  std::printf(
      "{\"checks\":%llu,\"failed\":%llu,\"mappings\":800,\"workers\":8}\n",
      (unsigned long long)checks.load(), (unsigned long long)failures.load());
  return failures ? 1 : 0;
}
