#include "core.h"
#include <algorithm>
#include <cmath>

namespace dcp::mobile {
size_t AudioRing::queued() const {
  const auto r = read_.load(std::memory_order_acquire);
  const auto w = write_.load(std::memory_order_acquire);
  // Diagnostic readers can see a read newer than their write snapshot.
  return w >= r ? std::min<size_t>(w - r, audio_capacity) : 0;
}
size_t AudioRing::push(const float *data, size_t frames, uint64_t &clipped) {
  const auto w = write_.load(std::memory_order_relaxed);
  const auto r = read_.load(std::memory_order_acquire);
  frames = std::min<size_t>(frames, audio_capacity - (w - r));
  for (size_t f = 0; f < frames; ++f) {
    for (size_t c = 0; c < 2; ++c) {
      float v = data[f * 2 + c];
      if (!std::isfinite(v)) {
        v = 0;
        ++clipped;
      } else if (v < -1 || v > 1) {
        v = std::clamp(v, -1.f, 1.f);
        ++clipped;
      }
      samples_[((w + f) % audio_capacity) * 2 + c] = v;
    }
  }
  write_.store(w + frames, std::memory_order_release);
  return frames;
}
size_t AudioRing::pop(float *left, float *right, size_t frames) {
  const auto r = read_.load(std::memory_order_relaxed);
  const auto w = write_.load(std::memory_order_acquire);
  frames = std::min<size_t>(frames, w - r);
  for (size_t f = 0; f < frames; ++f) {
    left[f] = samples_[((r + f) % audio_capacity) * 2];
    right[f] = samples_[((r + f) % audio_capacity) * 2 + 1];
  }
  read_.store(r + frames, std::memory_order_release);
  return frames;
}
size_t AudioRing::clear() {
  const auto n = queued();
  read_.store(0, std::memory_order_relaxed);
  write_.store(0, std::memory_order_relaxed);
  return n;
}
DcpPlatformPad neutral(bool connected) {
  DcpPlatformPad p{};
  p.connected = connected;
  p.lx = p.ly = p.rx = p.ry = 128;
  return p;
}
uint8_t axis(float value, bool invert) {
  if (!std::isfinite(value))
    value = 0;
  value = std::clamp(value, -1.f, 1.f);
  return uint8_t(std::lround(((invert ? -value : value) + 1.f) * 127.5f));
}
uint8_t trigger(float value) {
  return uint8_t(std::lround(
      std::clamp(std::isfinite(value) ? value : 0.f, 0.f, 1.f) * 255));
}
void touch_point(DcpPlatformPad &p, float x, float y, uint8_t id) {
  if (p.touch_count >= 2 || !std::isfinite(x) || !std::isfinite(y))
    return;
  const auto i = p.touch_count++;
  p.tx[i] = uint16_t(std::lround(std::clamp(x, 0.f, 1.f) * 1919));
  p.ty[i] = uint16_t(std::lround(std::clamp(y, 0.f, 1.f) * 942));
  p.tid[i] = id & 127;
}
void InputState::physical(unsigned slot, const DcpPlatformPad &p) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (slot < physical_.size())
    physical_[slot] = p.connected ? p : neutral(false);
}
void InputState::touch(const DcpPlatformPad &p) {
  std::lock_guard<std::mutex> lock(mutex_);
  touch_ = p.connected ? p : neutral(false);
  touch_.touch_count = std::min<uint8_t>(touch_.touch_count, 2);
}
void InputState::active(bool active) {
  std::lock_guard<std::mutex> lock(mutex_);
  active_ = active;
  if (!active)
    touch_ = neutral();
}
void InputState::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  physical_.fill(neutral(false));
  touch_ = neutral(false);
  active_ = false;
}
DcpPlatformPad InputState::read(unsigned slot) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (slot >= physical_.size())
    return neutral(false);
  auto p = physical_[slot];
  if (!p.connected)
    p = neutral(false);
  if (slot == 0 && touch_.connected) {
    p.connected = true;
    p.buttons |= touch_.buttons;
    auto merge = [](uint8_t a, uint8_t b) {
      return std::abs(int(b) - 128) > std::abs(int(a) - 128) ? b : a;
    };
    p.lx = merge(p.lx, touch_.lx);
    p.ly = merge(p.ly, touch_.ly);
    p.rx = merge(p.rx, touch_.rx);
    p.ry = merge(p.ry, touch_.ry);
    p.l2 = std::max(p.l2, touch_.l2);
    p.r2 = std::max(p.r2, touch_.r2);
    if (touch_.touch_count) {
      p.touch_count = touch_.touch_count;
      for (unsigned i = 0; i < 2; ++i) {
        p.tx[i] = touch_.tx[i];
        p.ty[i] = touch_.ty[i];
        p.tid[i] = touch_.tid[i];
      }
    }
  }
  return active_ ? p : neutral(p.connected);
}
} // namespace dcp::mobile
