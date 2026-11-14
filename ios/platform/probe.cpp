#include "core.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
using namespace dcp::mobile;
int main() {
  assert(axis(0) == 128 && axis(-1) == 0 && axis(1) == 255 &&
         axis(1, true) == 0);
  assert(axis(std::numeric_limits<float>::quiet_NaN()) == 128);
  assert(trigger(2) == 255 && trigger(-1) == 0);
  auto p = neutral();
  touch_point(p, -1, 2, 255);
  touch_point(p, .5f, .5f, 2);
  touch_point(p, 1, 1, 3);
  assert(p.touch_count == 2 && p.tx[0] == 0 && p.ty[0] == 942 &&
         p.tid[0] == 127);
  InputState input;
  assert(input.read(0).connected && !input.read(1).connected);
  auto physical = neutral();
  physical.buttons = 0x4000;
  physical.lx = 0;
  physical.l2 = 170;
  input.physical(0, physical);
  p = neutral();
  p.buttons = 0x8;
  p.ry = 255;
  p.l2 = 200;
  input.touch(p);
  p = input.read(0);
  assert(p.buttons == 0x4008 && p.lx == 0 && p.ry == 255 && p.l2 == 200);
  input.physical(0, neutral(false));
  p = input.read(0);
  assert(p.buttons == 8 && p.lx == 128);
  input.active(false);
  p = input.read(0);
  assert(p.connected && p.buttons == 0 && p.ry == 128);
  input.active(true);
  assert(input.read(0).buttons == 0);
  input.reset();
  assert(!input.read(0).connected);
  AudioRing ring;
  uint64_t clipped = 0;
  float values[] = {2, -2, std::numeric_limits<float>::quiet_NaN(), .5};
  assert(ring.push(values, 2, clipped) == 2 && clipped == 3);
  float l[4]{}, r[4]{};
  assert(ring.pop(l, r, 4) == 2 && l[0] == 1 && r[0] == -1 && l[1] == 0 &&
         r[1] == .5);
  std::vector<float> samples(audio_capacity * 2, .25f);
  assert(ring.push(samples.data(), audio_capacity, clipped) == audio_capacity);
  assert(ring.push(values, 2, clipped) == 0 && ring.queued() == audio_capacity);
  assert(ring.clear() == audio_capacity && ring.queued() == 0);
  // Multiple wraps and two real threads establish the SPSC release/acquire
  // path.
  constexpr size_t total = 400000;
  std::thread producer([&] {
    uint64_t nclipped = 0;
    for (size_t f = 0; f < total;) {
      float data[2] = {float(f % 1000) / 1000, -float(f % 1000) / 1000};
      if (ring.push(data, 1, nclipped))
        ++f;
      else
        std::this_thread::yield();
    }
    assert(nclipped == 0);
  });
  for (size_t f = 0; f < total;) {
    if (ring.pop(l, r, 1)) {
      assert(l[0] == float(f % 1000) / 1000 && r[0] == -float(f % 1000) / 1000);
      ++f;
    } else
      std::this_thread::yield();
  }
  producer.join();
  assert(ring.queued() == 0);
  std::cout << "PASS: input merge/disconnect/pause, axis/touch bounds, audio "
               "sanitize/capacity/reset, 400000 concurrent frames\n";
}
