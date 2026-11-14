#pragma once
#import <Foundation/Foundation.h>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

namespace dcp::mobile {
inline bool completedInScene(NSDictionary *guest) {
  return [guest[@"checkpoint_complete"] boolValue] &&
         [guest[@"checkpoint_scene"] isEqual:guest[@"current_scene"]] &&
         ![guest[@"checkpoint_scene_changed"] boolValue];
}

struct CompletedFrame {
  uint64_t flip;
  double seconds;
};

// Count completed render intervals between the first and last captured flips
// inside the scene window. A full capture queue may discard guest frames.
inline NSDictionary *settledTiming(const std::deque<CompletedFrame> &frames,
                                   uint64_t anchor, uint64_t count) {
  if (!count || count > 3600 || anchor > UINT64_MAX - count)
    return nil;
  auto first =
      std::find_if(frames.begin(), frames.end(), [anchor](const auto &frame) {
        return frame.flip >= anchor;
      });
  auto end = std::find_if(first, frames.end(), [=](const auto &frame) {
    return frame.flip > anchor + count;
  });
  if (end - first < 2)
    return nil;
  auto last = end - 1;
  std::vector<double> intervals;
  for (auto it = first + 1; it != end; ++it) {
    if (it->flip <= (it - 1)->flip)
      return nil;
    intervals.push_back((it->seconds - (it - 1)->seconds) * 1000.0);
  }
  const double seconds = last->seconds - first->seconds;
  if (seconds <= 0)
    return nil;
  std::sort(intervals.begin(), intervals.end());
  const size_t size = intervals.size();
  return @{
    @"first_flip" : @(first->flip),
    @"last_flip" : @(last->flip),
    @"render_intervals" : @(size),
    @"guest_flip_intervals" : @(last->flip - first->flip),
    @"skipped_inside" : @(last->flip - first->flip - size),
    @"seconds" : @(seconds),
    @"fps" : @(size / seconds),
    @"p50_ms" : @(intervals[(size - 1) / 2]),
    @"p95_ms" : @(intervals[(95 * size + 99) / 100 - 1])
  };
}
inline void recordCompleted(std::deque<CompletedFrame> &frames, uint64_t flip,
                            double seconds) {
  frames.push_back({flip, seconds});
  if (frames.size() > 3601)
    frames.pop_front();
}
} // namespace dcp::mobile
