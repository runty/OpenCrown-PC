#include "direct-memory.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

int main() {
  using dcp::game::DirectMemoryArena;
  unsigned checks = 0;
  auto check = [&](bool value) { ++checks; assert(value); };
  // Original boot resource-header/data1/data2 sizes and 2 MiB alignment.
  // The data1 table stores uint32(name_address - header_address); the game
  // reconstructs it with a zero-extended offset. No pointer repair is applied.
  DirectMemoryArena resources(uint64_t(768) << 20, uint64_t(768) << 20);
  const uint64_t header = resources.allocate(0x240000, 0x200000) + 0x200;
  const uint64_t data1 = resources.allocate(0x4000, 0x200000);
  const uint64_t data2 = resources.allocate(0x1f000000, 0x200000);
  check(header < data1 && data1 < data2);
  // Reversing two real backing regions demonstrates the original failure
  // shape: zero-extending their negative relative offset adds an extra 4 GiB.
  const uint64_t reversedHeader = data2 + 0x200, earlierName = data1 + 0x100;
  const uint32_t reversedOffset = uint32_t(earlierName) - uint32_t(reversedHeader);
  check(reversedHeader + uint64_t(reversedOffset) != earlierName);
  check(reversedHeader + uint64_t(reversedOffset) == earlierName + (uint64_t(1) << 32));
  for (uint64_t data : {data1, data2}) {
    for (uint64_t index = 0; index < 8; ++index) {
      const uint64_t name = data + 0x100 + 4 * 8 + 64 * index;
      const uint32_t offset = uint32_t(name) - uint32_t(header);
      check(header + uint64_t(offset) == name);
      *reinterpret_cast<uint8_t *>(name) = uint8_t(index + 1);
      check(*reinterpret_cast<const uint8_t *>(header + offset) == index + 1);
    }
  }
  const auto before = resources.stats();
  check(before.capacity == uint64_t(768) << 20);
  check(before.allocations == 3 && before.used >= 0x1f600000);
  for (auto request : std::array<std::array<uint64_t, 2>, 4>{{
           {0, 65536}, {1, 98304}, {UINT64_MAX, 65536},
           {uint64_t(768) << 20, 65536}}}) {
    bool rejected = false;
    try { resources.allocate(request[0], request[1]); }
    catch (const std::runtime_error &) { rejected = true; }
    check(rejected);
    check(resources.stats().used == before.used);
  }
  // Concurrent service calls must own distinct pages; each page remains
  // writable and intact while subsequent mappings are added.
  DirectMemoryArena concurrent(uint64_t(32) << 20, uint64_t(32) << 20);
  std::array<std::array<uint64_t, 64>, 4> addresses{};
  std::vector<std::thread> threads;
  for (unsigned worker = 0; worker < addresses.size(); ++worker)
    threads.emplace_back([&, worker] {
      for (unsigned i = 0; i < addresses[worker].size(); ++i) {
        auto address = concurrent.allocate(1, 65536);
        addresses[worker][i] = address;
        *reinterpret_cast<uint64_t *>(address) = 1 + worker * 64 + i;
      }
    });
  for (auto &thread : threads) thread.join();
  std::vector<uint64_t> ordered;
  for (unsigned worker = 0; worker < addresses.size(); ++worker)
    for (unsigned i = 0; i < addresses[worker].size(); ++i) {
      auto address = addresses[worker][i];
      check(address % 65536 == 0);
      check(*reinterpret_cast<const uint64_t *>(address) == 1 + worker * 64 + i);
      ordered.push_back(address);
    }
  std::sort(ordered.begin(), ordered.end());
  check(std::adjacent_find(ordered.begin(), ordered.end()) == ordered.end());
  check(concurrent.stats().allocations == ordered.size());
  DirectMemoryArena defaults;
  const auto first = defaults.allocate(4096, 65536);
  *reinterpret_cast<uint64_t *>(first) = 0x516a;
  const auto second = defaults.allocate(4096, 0x200000);
  check(first < second && second % 0x200000 == 0);
  check(*reinterpret_cast<const uint64_t *>(first) == 0x516a);
  check(defaults.stats().capacity >= (uint64_t(3) << 30));
  check(defaults.stats().capacity <= (uint64_t(4) << 30));
  std::printf("{\"checks\":%u,\"failed\":0,\"concurrent_allocations\":256,"
              "\"original_resource_offset_contract\":true}\n", checks);
}
