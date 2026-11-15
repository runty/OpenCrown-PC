#include "runtime.h"
#include <array>
#include <atomic>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

Memory *dispatch(State &, uint64_t, Memory *m) { return m; }
extern "C" Memory *__remill_write_memory_8(Memory *, uint64_t, uint8_t);
extern "C" Memory *__remill_write_memory_16(Memory *, uint64_t, uint16_t);
extern "C" Memory *__remill_write_memory_32(Memory *, uint64_t, uint32_t);
extern "C" Memory *__remill_write_memory_f32(Memory *, uint64_t, float);
extern "C" Memory *__remill_write_memory_f64(Memory *, uint64_t, double);
extern "C" Memory *__remill_compare_exchange_memory_64(Memory *, uint64_t,
                                                       uint64_t &, uint64_t);
extern "C" Memory *__remill_compare_exchange_memory_128(Memory *, uint64_t,
                                                        uint128_t &,
                                                        uint128_t &);
namespace {
unsigned checks = 0;
void check(bool value, const char *name) {
  if (!value)
    throw std::runtime_error(name);
  ++checks;
}
using Match = GuestSnapshotMatch;
struct Fixture {
  Memory memory;
  std::vector<uint8_t> bytes = std::vector<uint8_t>(2u << 20, 0);
  uint64_t base() { return reinterpret_cast<uint64_t>(bytes.data()); }
  explicit Fixture(bool verify = false) {
    configureGuestSnapshots(true, verify);
    addSnapshotRegion(memory, bytes.data(), bytes.size());
  }
};
[[maybe_unused]] void writers() {
  Fixture f;
  const uint64_t a = f.base() + 4090;
  std::array<uint8_t, 32> value{};
  value.fill(0xfa);
  const std::array<std::function<void()>, 12> writes{
      {[&] { write64(f.memory, a, 0x1122334455667788); },
       [&] { __remill_write_memory_8(&f.memory, a + 31, 1); },
       [&] { __remill_write_memory_16(&f.memory, a + 31, 2); },
       [&] { __remill_write_memory_32(&f.memory, a + 2, 3); },
       [&] { __remill_write_memory_f32(&f.memory, a + 3, 1.25f); },
       [&] { __remill_write_memory_f64(&f.memory, a + 1, 1.75); },
       [&] {
         float80_t v{};
         std::memset(v.data, 0x41, 10);
         __remill_write_memory_f80(&f.memory, a + 4, v);
       },
       [&] { guestWrite(f.memory, a, value.data(), value.size()); },
       [&] { guestFill(f.memory, a + 31, 2, 1); },
       [&] { guestMove(f.memory, a + 31, a + 512, 1); },
       [&] { guestMaxBytes(f.memory, a, value.data(), value.size()); },
       [&] {
         uint64_t expected = read64(f.memory, a);
         __remill_compare_exchange_memory_64(&f.memory, a, expected, 8);
       }}};
  for (size_t i = 0; i < writes.size(); ++i) {
    guestFill(f.memory, a, 0xa5, 32);
    guestFill(f.memory, a + 512, 0x12, 32);
    auto snapshot = guestSnapshot(f.memory, a, 32);
    check(snapshot->matches(f.memory, a) == Match::Version,
          "initial proof not version matched");
    writes[i]();
    check(snapshot->matches(f.memory, a) == Match::Different,
          "store invalidation missed");
    check(snapshot->data()[0] == 0xa5, "snapshot bytes mutated");
  }
  guestFill(f.memory, a, 0, 32);
  auto snapshot = guestSnapshot(f.memory, a, 32);
  uint64_t expected = 1;
  __remill_compare_exchange_memory_64(&f.memory, a, expected, 8);
  check(expected == 0 && snapshot->matches(f.memory, a) == Match::Version,
        "failed CAS changed proof");
  uint128_t e = 0, d = 17;
  __remill_compare_exchange_memory_128(&f.memory, a, e, d);
  check(snapshot->matches(f.memory, a) == Match::Different,
        "CAS128 invalidation missed");
  snapshot = guestSnapshot(f.memory, a, 32);
  guestMove(f.memory, a + 1, a, 31);
  check(snapshot->matches(f.memory, a) == Match::Different,
        "overlapping move invalidation missed");
}
[[maybe_unused]] void fallback() {
  Fixture f;
  const auto a = f.base() + 128;
  auto snapshot = guestSnapshot(f.memory, a, 8192);
  auto *escaped = guestPointer(f.memory, a + 8191, 1);
  check(snapshot->matches(f.memory, a) == Match::Content,
        "escape did not disable fast proof");
  *escaped = 42;
  check(snapshot->matches(f.memory, a) == Match::Different,
        "escaped write was missed");
  snapshot = guestSnapshot(f.memory, a, 8192);
  check(snapshot->matches(f.memory, a) == Match::Content,
        "escaped page regained fast proof");
  auto before = guestPointer(f.memory, f.base() + 65536, 1);
  snapshot = guestSnapshot(f.memory, f.base() + 65536, 4096);
  *before = 17;
  check(snapshot->matches(f.memory, f.base() + 65536) == Match::Different,
        "pre-snapshot escape was missed");
  // Unknown native ownership overlapping an opted-in region excludes reuse.
  addRegion(f.memory, f.bytes.data() + 131072, 8192);
  snapshot = guestSnapshot(f.memory, f.base() + 131072, 4096);
  check(snapshot->matches(f.memory, f.base() + 131072) == Match::Content,
        "native-owned range became version tracked");
  f.bytes[131072] = 93;
  check(snapshot->matches(f.memory, f.base() + 131072) == Match::Different,
        "native-owned write missed");
  snapshot = guestSnapshot(f.memory, f.base() + 196608, 4096);
  addRegion(f.memory, f.bytes.data() + 196608, 4096);
  check(snapshot->matches(f.memory, f.base() + 196608) == Match::Content,
        "late native ownership did not exclude proof");
}
[[maybe_unused]] void identities() {
  Fixture f;
  const auto a = f.base() + 4096;
  auto s = guestSnapshot(f.memory, a, 4096);
  check(s->matches(f.memory, a + 1) == Match::Invalid,
        "proof reused for another span");
  Memory other;
  addSnapshotRegion(other, f.bytes.data(), f.bytes.size());
  check(s->matches(other, a) == Match::Invalid,
        "proof crossed registry ownership");
  addSnapshotRegion(f.memory, f.bytes.data(), f.bytes.size());
  check(s->matches(f.memory, a) == Match::Content,
        "registration reuse kept old proof");
  check(s->matches(f.memory, a) == Match::Version,
        "equal-byte proof did not refresh");
  configureGuestSnapshots(false);
  check(s->matches(f.memory, a) == Match::Content,
        "disabled session retained proof");
  configureGuestSnapshots(true);
  check(s->matches(f.memory, a) == Match::Content, "reset retained old proof");
  Memory synthetic;
  addSnapshotRegion(synthetic, synthetic.bytes.data(), synthetic.bytes.size());
  auto alias = guestSnapshot(synthetic, Memory::base + 4090, 32);
  guestFill(synthetic,
            reinterpret_cast<uint64_t>(synthetic.bytes.data() + 4090), 3, 32);
  check(alias->matches(synthetic, Memory::base + 4090) == Match::Different,
        "native/synthetic alias invalidation missed");
  bool rejected = false;
  try {
    guestSnapshot(synthetic, UINT64_MAX, 1);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  check(rejected, "snapshot overflow accepted");
}
void wrap() {
  Fixture f;
  auto snapshot = guestSnapshot(f.memory, f.base(), 4096);
  for (unsigned i = 0; i < 14; ++i)
    write64(f.memory, f.base(), 0);
  check(snapshot->matches(f.memory, f.base()) == Match::Content,
        "write counter ABA retained proof");
#ifdef DCP_SNAPSHOT_TEST_VERSION_LIMIT
  check(snapshot->matches(f.memory, f.base()) == Match::Content,
        "overflowed page regained proof");
#else
  check(snapshot->matches(f.memory, f.base()) == Match::Version,
        "same-byte proof failed to refresh");
#endif
}
[[maybe_unused]] void concurrent() {
  Fixture f;
  std::atomic<bool> done{false};
  std::thread writer([&] {
    Memory local;
    local.regions = f.memory.regions;
    for (unsigned i = 0; i < 2000; ++i)
      guestFill(local, f.base(), uint8_t(i), 65536);
    done = true;
  });
  unsigned samples = 0;
  do {
    auto s = guestSnapshot(f.memory, f.base(), 65536);
    const auto result = s->matches(f.memory, f.base());
    check(result != Match::Invalid, "concurrent proof lost mapping");
    for (size_t j = 1; j < s->size(); ++j)
      check(s->data()[j] == s->data()[0],
            "snapshot bytes and publication were torn");
    ++samples;
  } while (!done.load() || samples < 4);
  writer.join();
  auto s = guestSnapshot(f.memory, f.base(), 65536);
  check(s->matches(f.memory, f.base()) == Match::Version,
        "final concurrent proof invalid");
}
[[maybe_unused]] void originalLock() {
  Fixture f;
  auto snapshot = guestSnapshot(f.memory, f.base(), 4096);
  std::promise<void> started;
  std::future<Match> pending;
  bool blocked;
  GuestAtomicMutex original;
  {
    std::lock_guard<GuestAtomicMutex> lock(original);
    pending = std::async(std::launch::async, [&] {
      started.set_value();
      return snapshot->matches(f.memory, f.base());
    });
    started.get_future().wait();
    blocked = pending.wait_for(std::chrono::milliseconds(30)) ==
              std::future_status::timeout;
  }
  check(pending.get() == Match::Version && blocked,
        "snapshot check crossed original LOCK");
}
[[maybe_unused]] void verified() {
  Fixture f(true);
  auto snapshot = guestSnapshot(f.memory, f.base(), 4096);
  check(snapshot->matches(f.memory, f.base()) == Match::Version,
        "verified proof did not match");
  auto stats = guestSnapshotDiagnostics();
  check(stats.verifying && stats.verified_checks == 1 &&
            stats.verified_bytes == 4096 && !stats.proof_failures,
        "verification accounting");
  // Deliberately violate tracked-writer admission, in the fixture only: the
  // audit mode must expose a bad proof rather than silently accepting it.
  f.bytes[4095] = 17;
  check(snapshot->matches(f.memory, f.base()) == Match::Different &&
            guestSnapshotDiagnostics().proof_failures == 1,
        "full-content verification missed an unreported writer");
}
} // namespace
int main() {
  try {
#ifdef DCP_SNAPSHOT_TEST_VERSION_LIMIT
    wrap();
#else
    writers();
    fallback();
    identities();
    originalLock();
    verified();
    wrap();
    concurrent();
#endif
    configureGuestSnapshots(false);
    std::cout
        << "{\"checks\":" << checks
        << ",\"failed\":0,\"version_proofs\":true,\"escaped_fallback\":true}\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
