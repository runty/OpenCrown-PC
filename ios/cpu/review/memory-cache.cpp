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
    narrowAtomics();
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
