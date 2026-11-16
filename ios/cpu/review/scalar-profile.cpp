#include "scalar-profile.h"
#include "runtime.h"
#include <cassert>
#include <iostream>
#include <thread>

extern "C" uint64_t __remill_read_memory_64(Memory *, uint64_t);
extern "C" Memory *__remill_write_memory_64(Memory *, uint64_t, uint64_t);
extern "C" Memory *__remill_atomic_begin(Memory *);
extern "C" Memory *__remill_atomic_end(Memory *);
Memory *dispatch(State &, uint64_t, Memory *memory) { return memory; }

int main() {
  namespace profile = dcp::cpu::profile;
  profile::reset();
  Memory memory;
  bindScalarThread(memory);
  profile::trace(0x20000000);
  for (unsigned i = 0; i < 31; ++i)
    __remill_write_memory_64(&memory, Memory::base + 8, i);
  profile::start_town();
  {
    const uint64_t rip = 0x20001008;
    profile::StateScope state(&rip);
    profile::trace(0x20001000);
    for (unsigned i = 0; i < 8192; ++i) {
      __remill_write_memory_64(&memory, Memory::base + 8, i);
      assert(__remill_read_memory_64(&memory, Memory::base + 8) == i);
    }
    __remill_atomic_begin(&memory);
    for (unsigned i = 0; i < 128; ++i) {
      __remill_write_memory_64(&memory, Memory::base + 8, i);
      assert(__remill_read_memory_64(&memory, Memory::base + 8) == i);
    }
    __remill_atomic_end(&memory);
    for (unsigned i = 0; i < 7; ++i) {
      write64(memory, Memory::base + 8, i);
      assert(read64(memory, Memory::base + 8) == i);
    }
    {
      const uint64_t nestedRip = 0x20003008;
      profile::StateScope nested(&nestedRip);
      profile::trace(0x20003000);
      for (unsigned i = 0; i < 2048; ++i)
        assert(__remill_read_memory_64(&memory, Memory::base + 8) == 6);
    }
    assert(__remill_read_memory_64(&memory, Memory::base + 8) == 6);
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 4; ++i)
      workers.emplace_back([i] {
        Memory local;
        bindScalarThread(local);
        const uint64_t rip = 0x20002008 + i * 0x100;
        profile::StateScope state(&rip);
        profile::trace(0x20002000 + i * 0x100);
        for (unsigned j = 0; j < 8192; ++j) {
          __remill_write_memory_64(&local, Memory::base + 8, j);
          assert(__remill_read_memory_64(&local, Memory::base + 8) == j);
        }
      });
    assert(profile::report(false) ==
           "{\"available\":false,\"reason\":\"workers_not_joined\"}");
    for (auto &worker : workers)
      worker.join();
    std::cout << profile::report(true) << '\n';
  }
  // Same-thread rebinding after a quiescent epoch reset must not retain a raw
  // pointer to the previous session's counters or include its reads/writes.
  profile::reset();
  bindScalarThread(memory);
  profile::start_town();
  {
    const uint64_t rip = 0x20004008;
    profile::StateScope state(&rip);
    profile::trace(0x20004000);
    __remill_write_memory_64(&memory, Memory::base + 8, 42);
    assert(__remill_read_memory_64(&memory, Memory::base + 8) == 42);
  }
  std::cout << profile::report(true) << '\n';
}
