#include "core.h"
#include "../game/cursor-readback.h"
#include <cassert>
#include <cfenv>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>
using namespace dcp::mobile;

static uint64_t bits(float x, float y) {
  uint32_t a, b;
  std::memcpy(&a, &x, 4);
  std::memcpy(&b, &y, 4);
  return uint64_t(b) << 32 | a;
}
// Independent model of the original relative cursor, including the observation
// delay, first contact, id change, lift and screen clamp. No game assets needed.
struct Game {
  InputState input;
  float x = 300, y = -200, k = .6f;
  bool previous = false, button = false;
  uint8_t id = 0;
  uint16_t px = 0, py = 0;
  uint32_t sequence = 0;
  int presses = 0;
  DcpPlatformPad frame() {
    const bool connected = input.connected(0);
    auto p = input.read(0);
    // The original pad parser asks for controller information after ReadState.
    // Repeated metadata queries must not consume finger steps or tap pulses.
    for (unsigned n = 0; n < 5; ++n)
      assert(input.connected(0) == connected);
    input.pointer_cursor(++sequence, x, y);
    if (p.touch_count) {
      assert(p.tx[0] <= 1919 && p.ty[0] <= 942 && p.tid[0] < 128);
      if (previous && p.tid[0] == id) {
        x += (int(p.tx[0]) - px) * k;
        y += (int(p.ty[0]) - py) * (1080.f / 943.f) * k;
      }
      px = p.tx[0]; py = p.ty[0]; id = p.tid[0];
    }
    previous = p.touch_count != 0;
    x = std::fmax(-960, std::fmin(960, x));
    y = std::fmax(-540, std::fmin(540, y));
    if ((p.buttons & 0x100000) && !button) ++presses;
    button = p.buttons & 0x100000;
    return p;
  }
  void frames(unsigned n) { while (n--) frame(); }
};
int main() {
  {
    Game g; g.x = 211; g.y = -293;
    g.input.pointer_cursor(++g.sequence, g.x, g.y);
    g.input.pointer_press(211, 380); g.frames(600);
    assert(std::fabs(g.x - 211) < 8 && std::fabs(g.y - 380) < 8);
    assert(!g.input.connected(4));
  }
  for (float gain : {.4f, .6f, 1.f, 2.f}) {
    Game g; g.k = gain;
    g.input.pointer_cursor(++g.sequence, g.x, g.y);
    for (auto target : {std::pair<float,float>{-700,-380}, {940,100}, {-200,520}}) {
      g.input.pointer_press(target.first, target.second);
      g.frames(120);
      assert(std::fabs(g.x - target.first) < 8 && std::fabs(g.y - target.second) < 8);
      g.input.pointer_release(true);
      const int presses = g.presses;
      g.frames(60);
      assert(g.presses == presses + 1 && !g.input.read(0).touch_count);
    }
  }
  // A screen tap combines with held movement/action buttons, but never emits
  // the interaction while the screen finger is still held.
  {
    Game g;
    auto p = neutral(); p.buttons = 0x8000; p.lx = 0;
    g.input.touch(p);
    g.input.pointer_cursor(++g.sequence, g.x, g.y);
    g.input.pointer_press(400, 200);
    for (int n = 0; n < 120; ++n) {
      p = g.frame(); assert(p.buttons == 0x8000 && p.lx == 0);
    }
    assert(!g.presses);
    g.input.pointer_release(true); g.frames(60);
    assert(g.presses == 1 && !g.input.read(0).touch_count);
  }
  for (int cancel = 0; cancel < 4; ++cancel) {
    Game g; g.input.pointer_press(700, 100); g.input.pointer_release(true);
    if (cancel == 0) g.input.pointer_cancel();
    if (cancel == 1) { g.input.active(false); g.input.active(true); }
    if (cancel == 2) { g.input.reset(); g.input.active(true); }
    if (cancel == 3) {
      auto p = neutral(); p.buttons = 0x100000;
      touch_point(p, 300.f/1919, 471.f/942, 7); g.input.touch(p);
      p = g.input.read(0); assert(p.touch_count == 1 && p.tx[0] == 300 && p.ty[0] == 471);
      g.input.touch(neutral());
    }
    g.frames(120); assert(g.presses == 0 && !g.input.read(0).touch_count);
  }
  {
    Game g; g.input.pointer_press(400, 200);
    g.input.pointer_move(std::numeric_limits<float>::quiet_NaN(), 0);
    g.frames(80); assert(!g.input.read(0).touch_count && !g.presses);
    g.input.pointer_press(400, 200); g.input.pointer_release(false);
    g.frames(80); assert(!g.presses && !g.input.read(0).touch_count);
  }
  // Snapshot coherence and integer-only rejection of signalling NaNs. Neither
  // publication nor rejection may change the translated thread's FP flags.
  dcp::game::CursorReadback cursor;
  assert(!cursor.read().sequence);
  assert(cursor.publish(bits(-960, 540)));
  const auto before = cursor.read();
  std::feclearexcept(FE_ALL_EXCEPT);
  assert(!cursor.publish(0x7f800001u));
  assert(!cursor.publish(bits(961, 0)) && !cursor.publish(bits(0, -541)));
  assert(!std::fetestexcept(FE_ALL_EXCEPT));
  assert(cursor.read().sequence == before.sequence);
  assert(cursor.publish(bits(1, -1)));
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (unsigned n = 0; n < 100000; ++n) assert(cursor.publish(bits(1+n%2, -1-float(n%2))));
    done.store(true);
  });
  do {
    auto p = cursor.read(); assert((p.x == 1 || p.x == 2) && p.y == -p.x && p.sequence);
  } while (!done.load());
  writer.join();
  std::cout << "PASS: pointer convergence, tap/drag, combined input, Map/cancel/pause/reset, cursor bounds/FP flags and 100000 coherent publications\n";
}
