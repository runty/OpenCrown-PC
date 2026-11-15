#pragma once
#import <Foundation/Foundation.h>
#include "render/backend.h"

namespace dcp::mobile {
inline NSDictionary *textureStatsDictionary(const render::TextureStats &stats) {
  return @{
    @"available" : @(stats.snapshot_budget_bytes != 0),
    @"byte_semantics" : @"Logical spans; comparisons can exit early; not measured DRAM traffic",
    @"requests" : @(stats.requests),
    @"same_frame_hits" : @(stats.same_frame_hits),
    @"comparisons" : @(stats.comparisons),
    @"comparison_span_bytes" : @(stats.comparison_span_bytes),
    @"comparison_hits" : @(stats.comparison_hits),
    @"matched_span_bytes" : @(stats.matched_span_bytes),
    @"copies" : @(stats.copies),
    @"copied_bytes" : @(stats.copied_bytes),
    @"hashed_bytes" : @(stats.hashed_bytes),
    @"uploads" : @(stats.uploads),
    @"budget_evictions" : @(stats.budget_evictions),
    @"retention_misses" : @(stats.retention_misses),
    @"snapshot_bytes" : @(stats.snapshot_bytes),
    @"peak_snapshot_bytes" : @(stats.peak_snapshot_bytes),
    @"snapshot_budget_bytes" : @(stats.snapshot_budget_bytes)
  };
}
} // namespace dcp::mobile
