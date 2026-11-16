// Included by runtime.cpp only in DCP_SCALAR_PROFILE builds.
#include <algorithm>
#include <mach/mach_time.h>
#include <sstream>
#include <unordered_map>

namespace dcp::cpu::profile {
namespace {
constexpr size_t recordLimit = 65536, pageLimit = 4096, threadLimit = 128;
constexpr uint64_t samplePeriod = 1024;
struct Wait {
  uint64_t samples = 0, ticks = 0, max_ticks = 0, over_10us = 0;
  void add(uint64_t elapsed) {
    ++samples;
    ticks += elapsed;
    max_ticks = std::max(max_ticks, elapsed);
    static const auto timebase = [] {
      mach_timebase_info_data_t value{};
      mach_timebase_info(&value);
      return value;
    }();
    if (elapsed * double(timebase.numer) / timebase.denom > 10000)
      ++over_10us;
  }
};
} // namespace
struct Record {
  uint64_t traces = 0, reads = 0, writes = 0, native_reads = 0,
           native_writes = 0, bytes = 0, atomic_held = 0;
  std::array<uint64_t, 5> sizes{}; // 1, 2, 4, 8, other bytes
  Wait wait;
};
struct Thread {
  uint64_t epoch = 0, index = 0, operations = 0, random = 1,
           dropped_records = 0, dropped_pages = 0,
           dropped_instruction_sites = 0, allocation_failures = 0;
  Record *current = nullptr;
  const uint64_t *rip = nullptr;
  std::unordered_map<uint64_t, Record> records;
  std::unordered_map<uint64_t, Wait> pages, instruction_sites;
  std::array<Wait, 65> stripes{}; // last slot: a multi-band access
};
namespace {
std::atomic<uint64_t> epoch{1}, omittedThreads{0};
std::atomic<bool> town{false};
std::mutex registryLock;
std::vector<std::shared_ptr<Thread>> registry;
thread_local std::shared_ptr<Thread> local;
thread_local uint64_t attemptedEpoch = 0;
double nanoseconds(uint64_t ticks) {
  mach_timebase_info_data_t timebase{};
  mach_timebase_info(&timebase);
  return ticks * double(timebase.numer) / timebase.denom;
}
void writeWait(std::ostream &out, const Wait &wait) {
  out << "{\"samples\":" << wait.samples
      << ",\"acquire_ns\":" << nanoseconds(wait.ticks)
      << ",\"max_acquire_ns\":" << nanoseconds(wait.max_ticks)
      << ",\"over_10us\":" << wait.over_10us << '}';
}
void merge(Wait &a, const Wait &b) {
  a.samples += b.samples;
  a.ticks += b.ticks;
  a.max_ticks = std::max(a.max_ticks, b.max_ticks);
  a.over_10us += b.over_10us;
}
void merge(Record &a, const Record &b) {
  a.traces += b.traces;
  a.reads += b.reads;
  a.writes += b.writes;
  a.native_reads += b.native_reads;
  a.native_writes += b.native_writes;
  a.bytes += b.bytes;
  a.atomic_held += b.atomic_held;
  for (size_t i = 0; i < a.sizes.size(); ++i)
    a.sizes[i] += b.sizes[i];
  merge(a.wait, b.wait);
}
} // namespace
void reset() {
  // Callers have joined every guest worker. Epoch changes invalidate TLS owners
  // lazily; old owners retain their objects until their next bind or thread
  // exit.
  std::lock_guard<std::mutex> lock(registryLock);
  town = false;
  registry.clear();
  omittedThreads = 0;
  epoch.fetch_add(1, std::memory_order_release);
}
Thread *thread() {
  const auto current = epoch.load(std::memory_order_acquire);
  if (local && local->epoch == current)
    return local.get();
  if (attemptedEpoch == current)
    return nullptr;
  attemptedEpoch = current;
  std::lock_guard<std::mutex> lock(registryLock);
  if (registry.size() == threadLimit) {
    ++omittedThreads;
    return nullptr;
  }
  local = std::make_shared<Thread>();
  local->epoch = current;
  local->index = registry.size();
  local->random =
      0x9e3779b97f4a7c15ULL ^ ((local->index + 1) * 0xd1b54a32d192ed03ULL);
  local->records.reserve(2048);
  registry.push_back(local);
  return local.get();
}
void trace(uint64_t pc) {
  if (!town.load(std::memory_order_relaxed))
    return;
  auto *owner = thread();
  if (!owner)
    return;
  auto found = owner->records.find(pc);
  if (found == owner->records.end()) {
    if (owner->records.size() == recordLimit) {
      ++owner->dropped_records;
      owner->current = nullptr;
      return;
    }
    found = owner->records.emplace(pc, Record{}).first;
  }
  owner->current = &found->second;
  ++owner->current->traces;
}
StateScope::StateScope(const uint64_t *rip)
    : owner(thread()), previous(owner ? owner->rip : nullptr),
      previous_record(owner ? owner->current : nullptr) {
  if (owner)
    owner->rip = rip;
}
StateScope::~StateScope() {
  if (owner) {
    owner->rip = previous;
    owner->current = previous_record;
  }
}
void start_town() {
  if (!town.load(std::memory_order_relaxed))
    town.store(true, std::memory_order_relaxed);
}
Sample scalar(Thread *owner, bool lifted, bool writing, size_t bytes,
              const void *pointer, unsigned stripe, bool atomic_held) {
  if (!owner || !owner->current)
    return {};
  auto &record = *owner->current;
  if (lifted) {
    if (writing)
      ++record.writes;
    else
      ++record.reads;
  } else {
    if (writing)
      ++record.native_writes;
    else
      ++record.native_reads;
  }
  record.bytes += bytes;
  const size_t bucket = bytes == 1   ? 0
                        : bytes == 2 ? 1
                        : bytes == 4 ? 2
                        : bytes == 8 ? 3
                                     : 4;
  ++record.sizes[bucket];
  if (atomic_held)
    ++record.atomic_held;
  ++owner->operations;
  if (atomic_held)
    return {};
  // Avoid fixed-stride aliasing with repeated scalar/vector loops. This is
  // diagnostic pseudo-random sampling, not security entropy.
  owner->random ^= owner->random << 13;
  owner->random ^= owner->random >> 7;
  owner->random ^= owner->random << 17;
  if (owner->random % samplePeriod)
    return {};
  return {owner,
          &record,
          mach_absolute_time(),
          reinterpret_cast<uint64_t>(pointer) / 4096,
          stripe,
          owner->rip ? *owner->rip : 0};
}
void Sample::acquired() {
  if (!record)
    return;
  elapsed = mach_absolute_time() - before;
  captured = true;
}
Sample::~Sample() {
  // The Sample precedes AccessLock in the caller, so these allocations/counter
  // updates run after the original data lock has been released.
  if (!captured)
    return;
  record->wait.add(elapsed);
  thread->stripes[stripe].add(elapsed);
  try {
    auto site = thread->instruction_sites.find(instruction_pc);
    if (site == thread->instruction_sites.end() &&
        thread->instruction_sites.size() < 16384)
      site = thread->instruction_sites.emplace(instruction_pc, Wait{}).first;
    if (site != thread->instruction_sites.end())
      site->second.add(elapsed);
    else
      ++thread->dropped_instruction_sites;
    auto found = thread->pages.find(page);
    if (found == thread->pages.end()) {
      if (thread->pages.size() == pageLimit) {
        ++thread->dropped_pages;
        return;
      }
      found = thread->pages.emplace(page, Wait{}).first;
    }
    found->second.add(elapsed);
  } catch (const std::bad_alloc &) {
    ++thread->allocation_failures;
  }
}
std::string report(bool workers_joined) {
  if (!workers_joined)
    return "{\"available\":false,\"reason\":\"workers_not_joined\"}";
  std::lock_guard<std::mutex> lock(registryLock);
  std::unordered_map<uint64_t, Record> records;
  std::unordered_map<uint64_t, Wait> pages, instructionSites;
  std::array<Wait, 65> stripes{};
  uint64_t droppedRecords = 0, droppedPages = 0, droppedSites = 0,
           allocationFailures = 0;
  for (const auto &owner : registry) {
    droppedRecords += owner->dropped_records;
    droppedPages += owner->dropped_pages;
    droppedSites += owner->dropped_instruction_sites;
    allocationFailures += owner->allocation_failures;
    for (const auto &[ip, value] : owner->instruction_sites)
      merge(instructionSites[ip], value);
    for (const auto &[pc, value] : owner->records)
      merge(records[pc], value);
    for (const auto &[page, value] : owner->pages)
      merge(pages[page], value);
    for (size_t i = 0; i < stripes.size(); ++i)
      merge(stripes[i], owner->stripes[i]);
  }
  Record totals;
  for (const auto &[pc, value] : records)
    merge(totals, value);
  std::vector<std::pair<uint64_t, Record>> ordered(records.begin(),
                                                   records.end());
  std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
    return a.second.reads + a.second.writes > b.second.reads + b.second.writes;
  });
  std::vector<std::pair<uint64_t, Wait>> orderedPages(pages.begin(),
                                                      pages.end());
  std::sort(orderedPages.begin(), orderedPages.end(),
            [](const auto &a, const auto &b) {
              return a.second.ticks > b.second.ticks;
            });
  std::vector<std::pair<uint64_t, Wait>> orderedSites(instructionSites.begin(),
                                                      instructionSites.end());
  std::sort(orderedSites.begin(), orderedSites.end(),
            [](const auto &a, const auto &b) {
              return a.second.ticks > b.second.ticks;
            });
  std::ostringstream out;
  out << "{\"available\":true,\"instrumented\":true,\"phase\":\"after_first_"
         "town\",\"sampler\":\"xorshift64\",\"mean_sample_period\":"
      << samplePeriod << ",\"threads\":" << registry.size()
      << ",\"omitted_threads\":" << omittedThreads
      << ",\"dropped_records\":" << droppedRecords
      << ",\"dropped_pages\":" << droppedPages
      << ",\"dropped_instruction_sites\":" << droppedSites
      << ",\"allocation_failures\":" << allocationFailures
      << ",\"reads\":" << totals.reads << ",\"writes\":" << totals.writes
      << ",\"native_reads\":" << totals.native_reads
      << ",\"native_writes\":" << totals.native_writes
      << ",\"bytes\":" << totals.bytes
      << ",\"atomic_held\":" << totals.atomic_held << ",\"wait\":";
  writeWait(out, totals.wait);
  out << ",\"recent_trace_entries\":[";
  bool first = true;
  for (const auto &[pc, value] : ordered) {
    if (!first)
      out << ',';
    first = false;
    out << "{\"pc\":" << pc << ",\"traces\":" << value.traces
        << ",\"reads\":" << value.reads << ",\"writes\":" << value.writes
        << ",\"native_reads\":" << value.native_reads
        << ",\"native_writes\":" << value.native_writes
        << ",\"bytes\":" << value.bytes
        << ",\"atomic_held\":" << value.atomic_held << ",\"sizes\":[";
    for (size_t i = 0; i < value.sizes.size(); ++i) {
      if (i)
        out << ',';
      out << value.sizes[i];
    }
    out << "],\"wait\":";
    writeWait(out, value.wait);
    out << '}';
  }
  out << "],\"stripes\":[";
  for (size_t i = 0; i < stripes.size(); ++i) {
    if (i)
      out << ',';
    writeWait(out, stripes[i]);
  }
  out << "],\"sampled_pages_by_total_acquire_time\":[";
  first = true;
  for (size_t i = 0; i < std::min<size_t>(40, orderedPages.size()); ++i) {
    if (!first)
      out << ',';
    first = false;
    out << "{\"host_page\":" << orderedPages[i].first << ",\"wait\":";
    writeWait(out, orderedPages[i].second);
    out << '}';
  }
  out << "],\"sampled_guest_rip_by_total_acquire_time\":[";
  first = true;
  for (size_t i = 0; i < std::min<size_t>(64, orderedSites.size()); ++i) {
    if (!first)
      out << ',';
    first = false;
    out << "{\"guest_rip\":" << orderedSites[i].first << ",\"wait\":";
    writeWait(out, orderedSites[i].second);
    out << '}';
  }
  out << "]}";
  return out.str();
}
} // namespace dcp::cpu::profile
