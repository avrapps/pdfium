// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdftext/cpdf_line_builder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>

namespace pdfium {
namespace layout {

// =============================================================================
// UnionFind Implementation
// =============================================================================

UnionFind::UnionFind(size_t n) : parent_(n), rank_(n, 0) {
  std::iota(parent_.begin(), parent_.end(), 0);
}

int UnionFind::Find(int x) {
  if (parent_[x] != x) {
    parent_[x] = Find(parent_[x]);  // Path compression
  }
  return parent_[x];
}

void UnionFind::Unite(int x, int y) {
  int px = Find(x);
  int py = Find(y);
  if (px == py) {
    return;
  }
  if (rank_[px] < rank_[py]) {
    std::swap(px, py);
  }
  parent_[py] = px;
  if (rank_[px] == rank_[py]) {
    rank_[px]++;
  }
}

std::vector<std::vector<int>> UnionFind::GetClusters() const {
  std::map<int, std::vector<int>> groups;
  for (size_t i = 0; i < parent_.size(); ++i) {
    int root = const_cast<UnionFind*>(this)->Find(static_cast<int>(i));
    groups[root].push_back(static_cast<int>(i));
  }

  std::vector<std::vector<int>> result;
  result.reserve(groups.size());
  for (auto& [_, indices] : groups) {
    result.push_back(std::move(indices));
  }
  return result;
}

// =============================================================================
// LineBuilder Implementation
// =============================================================================

LineBuilder::LineBuilder() = default;

LineBuilder::~LineBuilder() = default;

void LineBuilder::BuildMicroRuns(const std::vector<GlyphItem>& glyphs,
                                  const AdaptiveParams& params) {
  micro_runs_.clear();

  if (glyphs.empty()) {
    return;
  }

  // Collect non-space, non-generated glyphs
  std::vector<int> sorted_indices;
  sorted_indices.reserve(glyphs.size());
  for (size_t i = 0; i < glyphs.size(); ++i) {
    if (!glyphs[i].is_space_like && !glyphs[i].is_generated) {
      sorted_indices.push_back(static_cast<int>(i));
    }
  }

  if (sorted_indices.empty()) {
    return;
  }

  // KEY FIX: Sort ONLY by char_index to preserve CPDF_TextPage reading order!
  // CPDF_TextPage already handles column-aware ordering by:
  // 1. Grouping text objects into Y-bands (lines)
  // 2. X-sorting within each Y-band
  // Many PDFs emit text in column order (left col first, then right col),
  // which CPDF_TextPage preserves. DON'T break this by sorting by object_id!
  std::stable_sort(sorted_indices.begin(), sorted_indices.end(),
                   [&](int a, int b) {
                     return glyphs[a].char_index < glyphs[b].char_index;
                   });

  // Build micro-runs by detecting BREAKS in the stream, not by grouping same object
  MicroRun current_run;

  for (size_t i = 0; i < sorted_indices.size(); ++i) {
    int gi = sorted_indices[i];
    const GlyphItem& g = glyphs[gi];

    bool start_new_run = false;

    if (current_run.is_empty()) {
      // First glyph - start a run
    } else {
      int prev_gi = current_run.glyph_indices.back();
      const GlyphItem& prev = glyphs[prev_gi];

      float min_h = std::min(g.ink_height(), prev.ink_height());
      if (min_h < 0.1f) min_h = params.median_height;

      float baseline_diff = std::abs(g.baseline_y - prev.baseline_y);
      float dx = g.origin_x - prev.origin_x;

      // Detect discontinuities that indicate a new line/segment:
      // 1. Large baseline jump = different line
      // 2. Large backward X jump = likely new column
      // 3. Large FORWARD gap = gutter between columns on same baseline
      //    (CPDF_TextPage X-sorts within Y-band, so left-to-right gaps can be gutters)
      // 4. Rotation change
      bool baseline_jump = baseline_diff > min_h * 1.5f;
      bool backward_jump = dx < -min_h * 2.0f;  // Significant backward = new column
      bool rotation_change = g.rotation_bucket != current_run.rotation_bucket;
      
      // NEW: Detect large forward gaps (gutters between items on same Y-band)
      // Normal character spacing is ~1-2x glyph width. Gaps > 4x median advance
      // indicate a column boundary or significant separation.
      float max_normal_gap = std::max(4.0f * params.median_advance, 3.0f * min_h);
      bool large_forward_gap = dx > max_normal_gap;

      if (baseline_jump || backward_jump || rotation_change || large_forward_gap) {
        start_new_run = true;

        if (debug_enabled_) {
          MergeDecision decision;
          decision.type = MergeDecision::Type::kGlyphToLine;
          decision.from_idx = prev_gi;
          decision.to_idx = gi;
          decision.score = 0.0f;
          decision.reason = baseline_jump ? "baseline_jump" :
                            backward_jump ? "backward_jump" :
                            large_forward_gap ? "large_forward_gap" :
                            "rotation_change";
          decision.accepted = false;
          merge_log_.push_back(decision);
        }
      }
    }

    if (start_new_run && !current_run.is_empty()) {
      current_run.ComputeStats(glyphs);
      micro_runs_.push_back(std::move(current_run));
      current_run = MicroRun();
    }

    current_run.glyph_indices.push_back(gi);
    current_run.object_id = g.object_id;  // Track for debugging, not for grouping
    current_run.rotation_bucket = g.rotation_bucket;
  }

  // Finalize last run
  if (!current_run.is_empty()) {
    current_run.ComputeStats(glyphs);
    micro_runs_.push_back(std::move(current_run));
  }
  
  // DEBUG: Print micro-run summary
  printf("[LineBuilder] Built %zu micro-runs from %zu glyphs\n", 
         micro_runs_.size(), sorted_indices.size());
  
  // Sample some micro-runs for debugging
  size_t sample_count = std::min(micro_runs_.size(), static_cast<size_t>(5));
  for (size_t i = 0; i < sample_count; ++i) {
    const auto& run = micro_runs_[i];
    printf("[LineBuilder]   MicroRun %zu: glyphs=%zu, width=%.1f, baseline=%.1f\n",
           i, run.glyph_indices.size(), RectWidth(run.bbox), run.baseline_y);
  }
  if (micro_runs_.size() > 5) {
    printf("[LineBuilder]   ... and %zu more micro-runs\n", micro_runs_.size() - 5);
  }
}

// =============================================================================
// NEW: Coarse Column Detection from Micro-Runs
// =============================================================================

void LineBuilder::DetectCoarseColumns(const AdaptiveParams& params) {
  coarse_columns_ = CoarseColumnModel();
  coarse_columns_.num_columns = 1;
  coarse_columns_.is_valid = false;
  
  printf("[CoarseCol] Starting detection with %zu micro-runs\n", micro_runs_.size());
  
  if (micro_runs_.size() < 3) {
    printf("[CoarseCol] ABORT: Not enough micro-runs (<3)\n");
    return;  // Not enough data
  }
  
  // Compute page bounds from micro-runs
  float page_left = 1e9f, page_right = -1e9f;
  for (const auto& run : micro_runs_) {
    page_left = std::min(page_left, RectLeft(run.bbox));
    page_right = std::max(page_right, RectRight(run.bbox));
  }
  
  float page_width = page_right - page_left;
  printf("[CoarseCol] Page bounds: left=%.1f, right=%.1f, width=%.1f\n", 
         page_left, page_right, page_width);
  
  if (page_width < 100.0f) {
    printf("[CoarseCol] ABORT: Page too narrow (%.1f < 100)\n", page_width);
    return;  // Too narrow for multi-column
  }
  
  // DEBUG: Check for wide runs
  int wide_run_count = 0;
  int very_wide_run_count = 0;
  for (const auto& run : micro_runs_) {
    float run_width = RectWidth(run.bbox);
    float ratio = run_width / page_width;
    if (ratio > 0.35f) {
      wide_run_count++;
      printf("[CoarseCol] Wide run: width=%.1f (%.1f%% of page), glyphs=%zu\n",
             run_width, ratio * 100.0f, run.glyph_indices.size());
    }
    if (ratio > 0.6f) {
      very_wide_run_count++;
      printf("[CoarseCol] VERY WIDE run: width=%.1f (%.1f%% of page) - will be downweighted 0.3x\n",
             run_width, ratio * 100.0f);
    }
  }
  printf("[CoarseCol] Wide runs (>35%%): %d, Very wide runs (>60%%): %d\n", 
         wide_run_count, very_wide_run_count);
  
  // Build X-histogram using difference array (O(runs + width))
  int hist_size = static_cast<int>(std::ceil(page_width)) + 2;
  std::vector<float> diff(hist_size + 1, 0.0f);
  
  for (const auto& run : micro_runs_) {
    // FIXED: Use "ink presence" weight, not "ink width"
    // This prevents wide headlines from filling in gutter valleys
    // 
    // Options:
    // - constant: weight = 1.0 (pure presence)
    // - glyph-based: weight = min(glyph_count, 10) (capped)
    // - sublinear: weight = sqrt(width) (compromise)
    //
    // We use sqrt(width) as a compromise: longer runs contribute more,
    // but not linearly (a 200pt headline only contributes ~14x more than
    // a 1pt run, not 200x)
    float run_width = RectWidth(run.bbox);
    float weight = std::sqrt(std::max(run_width, 1.0f));
    
    // Downweight very short runs (labels, fragments) - they're often
    // captions or marginalia that shouldn't dominate column detection
    if (run_width < page_width * 0.05f) {
      weight *= 0.5f;
    }
    
    // Downweight very wide runs (spanning headlines) - they can mask gutters
    if (run_width > page_width * 0.6f) {
      weight *= 0.3f;
    }
    
    int left_idx = static_cast<int>(RectLeft(run.bbox) - page_left);
    int right_idx = static_cast<int>(RectRight(run.bbox) - page_left);
    
    left_idx = std::clamp(left_idx, 0, hist_size - 1);
    right_idx = std::clamp(right_idx, 0, hist_size - 1);
    
    diff[left_idx] += weight;
    diff[right_idx + 1] -= weight;
  }
  
  // Convert to histogram via prefix sum
  std::vector<float> histogram(hist_size, 0.0f);
  float cumsum = 0.0f;
  for (int i = 0; i < hist_size; ++i) {
    cumsum += diff[i];
    histogram[i] = cumsum;
  }
  
  // Smooth histogram (simple box filter)
  constexpr int kSmoothWindow = 5;
  std::vector<float> smoothed(hist_size, 0.0f);
  for (int i = 0; i < hist_size; ++i) {
    float sum = 0.0f;
    int count = 0;
    for (int j = std::max(0, i - kSmoothWindow);
         j <= std::min(hist_size - 1, i + kSmoothWindow); ++j) {
      sum += histogram[j];
      count++;
    }
    smoothed[i] = sum / count;
  }
  histogram = std::move(smoothed);
  
  // Find peak and compute percentile-based threshold
  float peak = *std::max_element(histogram.begin(), histogram.end());
  printf("[CoarseCol] Histogram peak=%.2f, size=%d\n", peak, hist_size);
  
  // Sample histogram at key positions
  int quarter = hist_size / 4;
  int half = hist_size / 2;
  int three_quarter = 3 * hist_size / 4;
  printf("[CoarseCol] Histogram samples: [25%%]=%.2f, [50%%]=%.2f, [75%%]=%.2f\n",
         histogram[quarter], histogram[half], histogram[three_quarter]);
  
  // Find the minimum value in the center region (where gutters would be)
  float center_min = 1e9f;
  int center_min_idx = 0;
  for (int i = hist_size / 5; i < 4 * hist_size / 5; ++i) {
    if (histogram[i] < center_min) {
      center_min = histogram[i];
      center_min_idx = i;
    }
  }
  printf("[CoarseCol] Center region min=%.2f at idx=%d (%.1f%% of page)\n",
         center_min, center_min_idx, 100.0f * center_min_idx / hist_size);
  
  if (peak < 1.0f) {
    printf("[CoarseCol] ABORT: Peak too low (%.2f < 1.0)\n", peak);
    return;  // No content
  }
  
  // FIXED: Use percentile-based threshold instead of strict % of peak
  // This is more robust to noise, kerning artifacts, and justification
  //
  // Compute p20 (low density) and p80 (high density) percentiles
  std::vector<float> sorted_hist = histogram;
  std::sort(sorted_hist.begin(), sorted_hist.end());
  
  // Skip zeros for percentile calculation (empty margins)
  size_t non_zero_start = 0;
  while (non_zero_start < sorted_hist.size() && sorted_hist[non_zero_start] < 0.01f) {
    non_zero_start++;
  }
  
  float valley_threshold;
  if (non_zero_start < sorted_hist.size() - 10) {
    // Enough non-zero data: use percentile
    size_t p20_idx = non_zero_start + (sorted_hist.size() - non_zero_start) * 20 / 100;
    size_t p80_idx = non_zero_start + (sorted_hist.size() - non_zero_start) * 80 / 100;
    float p20 = sorted_hist[p20_idx];
    float p80 = sorted_hist[p80_idx];
    
    // Valley is where histogram is closer to p20 than to p80
    // threshold = p20 + 0.15 * (p80 - p20)
    valley_threshold = p20 + 0.15f * (p80 - p20);
    printf("[CoarseCol] Percentile: p20=%.2f, p80=%.2f, raw_threshold=%.2f\n", p20, p80, valley_threshold);
    
    // But also cap at 15% of peak (don't be too permissive)
    float capped = std::min(valley_threshold, peak * 0.15f);
    if (capped < valley_threshold) {
      printf("[CoarseCol] Threshold capped from %.2f to %.2f (15%% of peak)\n", valley_threshold, capped);
    }
    valley_threshold = capped;
  } else {
    // Sparse data: fall back to 12% of peak
    valley_threshold = peak * 0.12f;
    printf("[CoarseCol] Sparse data: using 12%% of peak = %.2f\n", valley_threshold);
  }
  
  float min_gutter_width = std::max(params.gutter_min_width, 
                                     params.median_height * 1.5f);
  printf("[CoarseCol] Valley threshold=%.2f, min_gutter_width=%.1f\n", 
         valley_threshold, min_gutter_width);
  
  std::vector<float> gutters;
  bool in_valley = false;
  int valley_start = 0;
  float valley_min = 1e9f;
  int valley_min_idx = 0;
  
  for (int i = 0; i < hist_size; ++i) {
    if (histogram[i] < valley_threshold) {
      if (!in_valley) {
        in_valley = true;
        valley_start = i;
        valley_min = histogram[i];
        valley_min_idx = i;
      } else {
        if (histogram[i] < valley_min) {
          valley_min = histogram[i];
          valley_min_idx = i;
        }
      }
    } else {
      if (in_valley) {
        int valley_width = i - valley_start;
        // Require wider gutters for coarse detection (more conservative)
        if (valley_width >= static_cast<int>(min_gutter_width)) {
          // Convert to page X coordinate
          float gutter_x = page_left + static_cast<float>(valley_min_idx);
          
          // Reject gutters too close to page edges (< 15% or > 85%)
          float relative_pos = (gutter_x - page_left) / page_width;
          if (relative_pos > 0.15f && relative_pos < 0.85f) {
            gutters.push_back(gutter_x);
            printf("[CoarseCol] Found gutter: x=%.1f (%.1f%% of page), valley_width=%d, min_val=%.2f\n",
                   gutter_x, relative_pos * 100.0f, valley_width, valley_min);
          } else {
            printf("[CoarseCol] Rejected gutter at edge: x=%.1f (%.1f%% of page)\n",
                   gutter_x, relative_pos * 100.0f);
          }
        } else {
          printf("[CoarseCol] Valley too narrow: width=%d < %.0f required\n",
                 valley_width, min_gutter_width);
        }
        in_valley = false;
      }
    }
  }
  
  // Handle valley at end
  if (in_valley) {
    int valley_width = hist_size - valley_start;
    if (valley_width >= static_cast<int>(min_gutter_width)) {
      float gutter_x = page_left + static_cast<float>(valley_min_idx);
      float relative_pos = (gutter_x - page_left) / page_width;
      if (relative_pos > 0.15f && relative_pos < 0.85f) {
        gutters.push_back(gutter_x);
        printf("[CoarseCol] Found gutter at end: x=%.1f (%.1f%% of page), valley_width=%d\n",
               gutter_x, relative_pos * 100.0f, valley_width);
      }
    }
  }
  
  printf("[CoarseCol] Raw gutters found: %zu\n", gutters.size());
  for (size_t i = 0; i < gutters.size(); ++i) {
    printf("[CoarseCol]   Gutter %zu: x=%.1f\n", i, gutters[i]);
  }
  
  // Validate: for coarse detection, only trust 1-2 gutters max
  // (3+ column layouts are rare and we want to be conservative)
  if (gutters.size() > 2) {
    // Keep only the strongest (widest valley around center)
    std::sort(gutters.begin(), gutters.end(), 
              [page_left, page_width](float a, float b) {
                float center = page_left + page_width / 2.0f;
                return std::abs(a - center) < std::abs(b - center);
              });
    gutters.resize(1);
  }
  
  // Verify gutters: check that runs don't commonly cross them
  // FIXED: Use outlier-robust verification instead of simple rejection
  std::vector<float> verified_gutters;
  for (float gutter_x : gutters) {
    int crossing_count = 0;
    int total_count = 0;
    
    for (const auto& run : micro_runs_) {
      float run_width = RectWidth(run.bbox);
      
      // Only count runs that are wide enough to potentially cross
      if (run_width > params.median_height * 2.0f) {
        total_count++;
        
        bool crosses = RectLeft(run.bbox) < gutter_x - params.median_advance &&
                       RectRight(run.bbox) > gutter_x + params.median_advance;
        
        if (crosses) {
          // Check if this is an outlier (unusually wide run)
          // Very wide runs are likely spanning headlines or weird PDF artifacts
          // Don't let them veto the gutter
          if (run_width > page_width * 0.5f) {
            // Outlier: ignore entirely
          } else if (run_width > page_width * 0.35f) {
            // Medium-wide: count as half a crossing
            crossing_count++;  // Still counts but will be diluted
          } else {
            // Normal width: full crossing
            crossing_count += 2;  // Weight normal crossings more
          }
        }
      }
    }
    
    // FIXED: Outlier-robust decision
    // - Outlier crossings are ignored entirely
    // - Normal crossings weighted 2x vs medium crossings
    // - Threshold is 8% of (2 * total_count) to account for weighting
    int weighted_threshold = total_count * 2 * 8 / 100;  // 8% of weighted total
    
    printf("[CoarseCol] Gutter %.1f verification: total_runs=%d, crossing_count=%d, threshold=%d\n",
           gutter_x, total_count, crossing_count, std::max(weighted_threshold, 2));
    
    if (total_count == 0 || crossing_count < std::max(weighted_threshold, 2)) {
      verified_gutters.push_back(gutter_x);
      printf("[CoarseCol] Gutter %.1f VERIFIED\n", gutter_x);
    } else {
      printf("[CoarseCol] Gutter %.1f REJECTED (too many crossings)\n", gutter_x);
    }
  }
  
  if (!verified_gutters.empty()) {
    coarse_columns_.gutter_positions = std::move(verified_gutters);
    coarse_columns_.num_columns = 
        static_cast<int>(coarse_columns_.gutter_positions.size()) + 1;
    coarse_columns_.is_valid = true;
    
    // Sort gutters by position
    std::sort(coarse_columns_.gutter_positions.begin(),
              coarse_columns_.gutter_positions.end());
    
    printf("[CoarseCol] RESULT: is_valid=TRUE, num_columns=%d\n", coarse_columns_.num_columns);
    for (float g : coarse_columns_.gutter_positions) {
      printf("[CoarseCol]   Final gutter: x=%.1f\n", g);
    }
  } else {
    printf("[CoarseCol] RESULT: is_valid=FALSE (no verified gutters)\n");
  }
}

void LineBuilder::AssignMicroRunColumns() {
  if (!coarse_columns_.is_valid) {
    // Single column: all runs get column_id = 0
    for (auto& run : micro_runs_) {
      run.column_id = 0;
    }
    return;
  }
  
  // FIXED: Use gutter margin zone for robust assignment
  // When a run's center is near a gutter, use left edge or overlap-based assignment
  float gutter_margin = 0.0f;
  if (!micro_runs_.empty()) {
    // Compute median advance from runs (approximate)
    std::vector<float> widths;
    for (const auto& run : micro_runs_) {
      if (!run.glyph_indices.empty()) {
        widths.push_back(RectWidth(run.bbox) / run.glyph_indices.size());
      }
    }
    if (!widths.empty()) {
      std::nth_element(widths.begin(), widths.begin() + widths.size() / 2, widths.end());
      gutter_margin = widths[widths.size() / 2] * 3.0f;  // 3 char widths margin
    }
  }
  
  for (auto& run : micro_runs_) {
    float center_x = RectCenterX(run.bbox);
    float left_x = RectLeft(run.bbox);
    
    // Check if center is near any gutter
    bool near_gutter = false;
    for (float gutter_x : coarse_columns_.gutter_positions) {
      if (std::abs(center_x - gutter_x) < gutter_margin) {
        near_gutter = true;
        break;
      }
    }
    
    if (near_gutter) {
      // Use left edge for assignment (more stable)
      run.column_id = coarse_columns_.GetColumnForX(left_x);
    } else {
      // Use center (normal case)
      run.column_id = coarse_columns_.GetColumnForX(center_x);
    }
  }
}

// =============================================================================
// Modified: CanMergeRuns with Column Gating
// =============================================================================

bool LineBuilder::CanMergeRuns(const MicroRun& r1,
                                const MicroRun& r2,
                                const AdaptiveParams& params) const {
  // ========== COLUMN GATE (NEW - FIRST CHECK) ==========
  // This is the key fix: never merge runs from different columns
  if (coarse_columns_.is_valid && r1.column_id != r2.column_id) {
    return false;
  }
  
  // Even if columns aren't detected, reject merges that cross detected gutters
  if (coarse_columns_.is_valid) {
    float x1 = RectRight(r1.bbox);
    float x2 = RectLeft(r2.bbox);
    if (x1 > x2) {
      x1 = RectRight(r2.bbox);
      x2 = RectLeft(r1.bbox);
    }
    
    // Check if the gap between runs crosses a gutter
    float tolerance = params.median_advance;
    if (coarse_columns_.CrossesGutter(x1, x2, tolerance)) {
      return false;
    }
  }

  // Rotation must match
  if (r1.rotation_bucket != r2.rotation_bucket) {
    return false;
  }

  float min_h = std::min(r1.median_height, r2.median_height);
  float max_h = std::max(r1.median_height, r2.median_height);
  if (min_h < 0.1f) min_h = params.median_height;
  if (max_h < 0.1f) max_h = params.median_height;

  // Height ratio check (reject super/subscripts)
  float h_ratio = max_h / std::max(min_h, 0.001f);
  if (h_ratio > 1.8f) {
    return false;
  }

  // === BASELINE ANCHORED (PRIMARY GATE) ===
  float baseline_diff = std::abs(r1.baseline_y - r2.baseline_y);
  float baseline_tol = std::max(params.baseline_tol, 0.35f * min_h);
  bool baseline_ok = baseline_diff <= baseline_tol;

  if (!baseline_ok) {
    return false;  // Baseline is the anchor
  }

  // === SECONDARY: overlap OR vcenter ===
  float overlap_ratio = VerticalOverlapRatio(r1.bbox, r2.bbox);
  float vcenter_diff = std::abs(RectCenterY(r1.bbox) - RectCenterY(r2.bbox));

  // Adaptive overlap threshold based on height similarity
  float overlap_req = (h_ratio < 1.2f) ? 0.5f : 0.35f;

  bool overlap_ok = overlap_ratio >= overlap_req;
  bool vcenter_ok = vcenter_diff <= 0.5f * min_h;

  if (!(overlap_ok || vcenter_ok)) {
    return false;
  }

  // === HORIZONTAL GATING (TIGHTENED) ===
  float gap = HorizontalGap(r1.bbox, r2.bbox);
  
  // FIXED: Use advance-based threshold, not just height-based
  // Gutters are whitespace, so they're measured in "character widths"
  float max_join_dx = std::min(
      std::max(8.0f * params.median_advance, 4.0f * max_h),  // Primary: advance-based
      10.0f * params.median_height  // Hard cap
  );

  if (gap > max_join_dx) {
    return false;
  }

  // REMOVED: The weak "3.0f * max_h" check is replaced by column gating above

  return true;
}

float LineBuilder::ScoreMergeCandidate(const MicroRun& r1,
                                        const MicroRun& r2,
                                        const AdaptiveParams& params) const {
  float min_h = std::min(r1.median_height, r2.median_height);
  if (min_h < 0.1f) min_h = params.median_height;

  // Baseline score (1 = perfect, 0 = at tolerance limit)
  float baseline_diff = std::abs(r1.baseline_y - r2.baseline_y);
  float baseline_tol = std::max(params.baseline_tol, 0.35f * min_h);
  float baseline_score = 1.0f - std::clamp(baseline_diff / baseline_tol, 0.0f, 1.0f);

  // Overlap score
  float overlap_ratio = VerticalOverlapRatio(r1.bbox, r2.bbox);
  float overlap_score = std::clamp(overlap_ratio, 0.0f, 1.0f);

  // Gap score (smaller gap = better, allow slight negative for kerning)
  float gap = HorizontalGap(r1.bbox, r2.bbox);
  float gap_norm = gap / std::max(params.median_advance, 1.0f);
  float gap_score = (gap < 0) ? 0.9f : 1.0f - std::clamp(gap_norm / 5.0f, 0.0f, 1.0f);

  // Height ratio score
  float h_ratio = std::max(r1.median_height, r2.median_height) /
                  std::max(std::min(r1.median_height, r2.median_height), 0.001f);
  float height_score = 1.0f - std::clamp((h_ratio - 1.0f) / 0.8f, 0.0f, 1.0f);

  return baseline_score * 0.4f + overlap_score * 0.3f +
         gap_score * 0.2f + height_score * 0.1f;
}

void LineBuilder::MergeMicroRunsIntoLines(const std::vector<GlyphItem>& glyphs,
                                           const AdaptiveParams& params) {
  lines_.clear();

  if (micro_runs_.empty()) {
    return;
  }

  // ==========================================================================
  // STREAM-ORDER SEQUENTIAL MERGE
  // ==========================================================================
  // Key insight: Micro-runs are already in CPDF_TextPage stream order (char_index
  // sorted), which IS the correct reading order. We must NOT use spatial clustering
  // because that ignores stream order and merges across columns!
  //
  // Instead: Only merge CONSECUTIVE micro-runs that are geometrically compatible.
  // This naturally respects column boundaries because column transitions in the
  // stream have either a baseline jump or a backward X jump (which already broke
  // the micro-run in Phase 1).
  // ==========================================================================

  LineItem current_line;
  float current_baseline = 0.0f;
  float current_height = 0.0f;
  int current_rotation = -1;

  for (size_t i = 0; i < micro_runs_.size(); ++i) {
    const MicroRun& run = micro_runs_[i];

    bool start_new_line = false;

    if (current_line.glyph_indices.empty()) {
      // First run - start a new line
      start_new_line = false;
    } else {
      // Check if this run can extend the current line
      float min_h = std::min(run.median_height, current_height);
      if (min_h < 0.1f) min_h = params.median_height;

      float baseline_diff = std::abs(run.baseline_y - current_baseline);
      float baseline_tol = std::max(params.baseline_tol, 0.5f * min_h);

      // Check rotation match
      bool rotation_ok = (run.rotation_bucket == current_rotation);

      // Check baseline compatibility
      bool baseline_ok = (baseline_diff <= baseline_tol);

      // Check for horizontal continuity (no huge backward jump)
      // A backward jump indicates a column transition
      float run_left = RectLeft(run.bbox);
      float line_right = current_line.bbox.right;
      float dx = run_left - line_right;
      
      // Allow forward gaps up to a reasonable limit, but reject large backward jumps
      float max_forward_gap = std::max(8.0f * params.median_advance, 4.0f * min_h);
      bool horizontal_ok = (dx >= -min_h * 0.5f) && (dx <= max_forward_gap);

      if (!rotation_ok || !baseline_ok || !horizontal_ok) {
        start_new_line = true;

        if (debug_enabled_) {
          MergeDecision decision;
          decision.type = MergeDecision::Type::kGlyphToLine;
          decision.from_idx = static_cast<int>(i - 1);
          decision.to_idx = static_cast<int>(i);
          decision.score = 0.0f;
          decision.reason = !rotation_ok ? "rotation_mismatch" :
                            !baseline_ok ? "baseline_mismatch" :
                            "horizontal_gap";
          decision.accepted = false;
          merge_log_.push_back(decision);
        }
      } else {
        if (debug_enabled_) {
          MergeDecision decision;
          decision.type = MergeDecision::Type::kGlyphToLine;
          decision.from_idx = static_cast<int>(i - 1);
          decision.to_idx = static_cast<int>(i);
          decision.score = 1.0f;
          decision.reason = "sequential_merge";
          decision.accepted = true;
          merge_log_.push_back(decision);
        }
      }
    }

    if (start_new_line && !current_line.glyph_indices.empty()) {
      // Finalize current line
      lines_.push_back(std::move(current_line));
      current_line = LineItem();
    }

    // Add this run's glyphs to current line
    for (int gi : run.glyph_indices) {
      current_line.glyph_indices.push_back(gi);
    }

    // Update current line's bounding box
    if (current_line.glyph_indices.size() == run.glyph_indices.size()) {
      // First run in line - initialize bbox
      current_line.bbox = run.bbox;
    } else {
      // Extend bbox
      current_line.bbox.left = std::min(current_line.bbox.left, RectLeft(run.bbox));
      current_line.bbox.right = std::max(current_line.bbox.right, RectRight(run.bbox));
      current_line.bbox.bottom = std::min(current_line.bbox.bottom, RectBottom(run.bbox));
      current_line.bbox.top = std::max(current_line.bbox.top, RectTop(run.bbox));
    }

    // Update running stats (weighted average for baseline/height)
    size_t run_count = run.glyph_indices.size();
    size_t prev_count = current_line.glyph_indices.size() - run_count;
    if (prev_count == 0) {
      current_baseline = run.baseline_y;
      current_height = run.median_height;
    } else {
      float total = static_cast<float>(prev_count + run_count);
      current_baseline = (current_baseline * prev_count + run.baseline_y * run_count) / total;
      current_height = (current_height * prev_count + run.median_height * run_count) / total;
    }
    current_rotation = run.rotation_bucket;
  }

  // Don't forget the last line
  if (!current_line.glyph_indices.empty()) {
    lines_.push_back(std::move(current_line));
  }

  printf("[LineBuilder] Sequential merge: %zu micro-runs -> %zu lines\n",
         micro_runs_.size(), lines_.size());
}

void LineBuilder::SortGlyphsInLines(const std::vector<GlyphItem>& glyphs) {
  for (LineItem& line : lines_) {
    std::stable_sort(line.glyph_indices.begin(), line.glyph_indices.end(),
                     [&](int a, int b) {
                       const auto& ga = glyphs[a];
                       const auto& gb = glyphs[b];
                       // Primary: origin_x
                       // Tiebreaker: char_index (stream order)
                       if (std::abs(ga.origin_x - gb.origin_x) < 0.5f) {
                         return ga.char_index < gb.char_index;
                       }
                       return ga.origin_x < gb.origin_x;
                     });
  }
}

void LineBuilder::AnnotateSuspiciousGaps(const std::vector<GlyphItem>& glyphs,
                                          const AdaptiveParams& params) {
  printf("[LineBuilder] Annotating suspicious gaps in %zu lines\n", lines_.size());
  
  for (size_t line_idx = 0; line_idx < lines_.size(); ++line_idx) {
    LineItem& line = lines_[line_idx];
    if (line.glyph_indices.size() < 2) {
      continue;
    }

    // Compute largest internal gap
    float max_gap = 0.0f;
    float gap_x = 0.0f;

    for (size_t i = 1; i < line.glyph_indices.size(); ++i) {
      const GlyphItem& prev = glyphs[line.glyph_indices[i - 1]];
      const GlyphItem& curr = glyphs[line.glyph_indices[i]];

      float gap = curr.origin_x - RectRight(prev.bbox);
      if (gap > max_gap) {
        max_gap = gap;
        gap_x = (RectRight(prev.bbox) + curr.origin_x) / 2.0f;
      }
    }

    // Mark as suspicious if gap is gutter-like
    if (max_gap > params.gutter_min_width) {
      line.has_suspicious_gap = true;
      line.suspicious_gap_x = gap_x;
      line.suspicious_gap_size = max_gap;
      printf("[LineBuilder] Line %zu: SUSPICIOUS GAP at x=%.1f, size=%.1f (threshold=%.1f)\n",
             line_idx, gap_x, max_gap, params.gutter_min_width);
    }
  }
}

void LineBuilder::ComputeLineBoundsFromGlyphs(LineItem& line,
                                               const std::vector<GlyphItem>& glyphs) {
  if (line.glyph_indices.empty()) {
    return;
  }

  // Compute bounding box
  line.bbox = glyphs[line.glyph_indices[0]].bbox;
  for (size_t i = 1; i < line.glyph_indices.size(); ++i) {
    line.bbox = UnionRects(line.bbox, glyphs[line.glyph_indices[i]].bbox);
  }

  // Compute baseline (median)
  std::vector<float> baselines;
  std::vector<float> heights;
  baselines.reserve(line.glyph_indices.size());
  heights.reserve(line.glyph_indices.size());

  for (int gi : line.glyph_indices) {
    baselines.push_back(glyphs[gi].baseline_y);
    heights.push_back(glyphs[gi].ink_height());
  }

  std::nth_element(baselines.begin(), baselines.begin() + baselines.size() / 2,
                   baselines.end());
  line.baseline_y = baselines[baselines.size() / 2];

  std::nth_element(heights.begin(), heights.begin() + heights.size() / 2,
                   heights.end());
  line.line_height = heights[heights.size() / 2];

  // Collect object IDs
  line.object_ids.clear();
  for (int gi : line.glyph_indices) {
    if (glyphs[gi].object_id != kInvalidObjectId) {
      line.object_ids.insert(glyphs[gi].object_id);
    }
  }
}

int LineBuilder::BuildFromGlyphs(const std::vector<GlyphItem>& glyphs,
                                  const AdaptiveParams& params) {
  printf("\n[LineBuilder] ========== BuildFromGlyphs START ==========\n");
  printf("[LineBuilder] Input: %zu glyphs\n", glyphs.size());
  
  lines_.clear();
  micro_runs_.clear();
  merge_log_.clear();
  coarse_columns_ = CoarseColumnModel();

  if (glyphs.empty()) {
    printf("[LineBuilder] ABORT: No glyphs\n");
    return 0;
  }

  // Phase 1: Build micro-runs using stream order + geometry sanity gate
  // Micro-runs are now in CPDF_TextPage char_index order (correct reading order)
  BuildMicroRuns(glyphs, params);
  printf("[LineBuilder] Phase 1 complete: %zu micro-runs\n", micro_runs_.size());

  if (micro_runs_.empty()) {
    return 0;
  }

  // Phase 2: Merge micro-runs into lines using SEQUENTIAL pass
  // No spatial Union-Find! Only merge consecutive micro-runs that are compatible.
  // This respects stream order and naturally prevents cross-column merges.
  MergeMicroRunsIntoLines(glyphs, params);
  printf("[LineBuilder] Phase 2 complete: %zu lines\n", lines_.size());

  // Sort glyphs within each line by origin_x
  SortGlyphsInLines(glyphs);

  // Compute line bounds
  for (size_t i = 0; i < lines_.size(); ++i) {
    ComputeLineBoundsFromGlyphs(lines_[i], glyphs);
  }
  
  // DEBUG: Print line summary
  printf("[LineBuilder] Line summary (%zu lines):\n", lines_.size());
  for (size_t i = 0; i < lines_.size(); ++i) {
    const auto& line = lines_[i];
    printf("[LineBuilder]   Line %zu: glyphs=%zu, bbox=[%.1f,%.1f,%.1f,%.1f], baseline=%.1f\n",
           i, line.glyph_indices.size(),
           RectLeft(line.bbox), RectBottom(line.bbox),
           RectRight(line.bbox), RectTop(line.bbox),
           line.baseline_y);
  }

  // Annotate suspicious gaps (don't split, just mark)
  AnnotateSuspiciousGaps(glyphs, params);

  printf("[LineBuilder] ========== BuildFromGlyphs END ==========\n");
  printf("[LineBuilder] Final: %zu lines from %zu glyphs\n\n",
         lines_.size(), glyphs.size());

  return static_cast<int>(lines_.size());
}

// =============================================================================
// Legacy Word-Based Methods (kept for compatibility)
// =============================================================================

void LineBuilder::ClusterWords(const std::vector<WordItem>& words,
                                const SpatialGrid& grid,
                                const AdaptiveParams& params) {
  UnionFind uf(words.size());

  for (size_t i = 0; i < words.size(); ++i) {
    const WordItem& w1 = words[i];
    if (w1.is_empty()) {
      continue;
    }

    CFX_FloatRect query = w1.bbox;
    query.left -= params.line_merge_max_gap;
    query.right += params.line_merge_max_gap;
    query.top += params.baseline_tol;
    query.bottom -= params.baseline_tol;

    auto neighbors = grid.Query(query);

    for (int j : neighbors) {
      if (static_cast<size_t>(j) <= i) {
        continue;
      }

      const WordItem& w2 = words[j];
      if (w2.is_empty()) {
        continue;
      }

      float overlap_ratio = VerticalOverlapRatio(w1.bbox, w2.bbox);
      float baseline_diff = std::abs(w1.baseline_y - w2.baseline_y);
      bool v_aligned = overlap_ratio > params.line_overlap_tol ||
                       baseline_diff < params.baseline_tol;

      if (!v_aligned) {
        continue;
      }

      float h_gap = std::abs(HorizontalGap(w1.bbox, w2.bbox));
      bool h_close = h_gap < params.line_merge_max_gap;

      if (!h_close) {
        continue;
      }

      float h1 = w1.median_height;
      float h2 = w2.median_height;
      float h_ratio = std::max(h1, h2) / std::max(std::min(h1, h2), 0.001f);
      bool h_compat = h_ratio < 1.5f;

      if (!h_compat) {
        continue;
      }

      uf.Unite(static_cast<int>(i), j);
    }
  }

  auto clusters = uf.GetClusters();
  lines_.clear();
  lines_.reserve(clusters.size());

  for (const auto& cluster : clusters) {
    bool has_content = false;
    for (int wi : cluster) {
      if (!words[wi].is_empty()) {
        has_content = true;
        break;
      }
    }
    if (!has_content) {
      continue;
    }

    LineItem line;
    line.word_indices = cluster;
    ComputeLineBounds(line, words);
    lines_.push_back(std::move(line));
  }
}

void LineBuilder::ComputeLineBounds(LineItem& line,
                                     const std::vector<WordItem>& words) {
  if (line.word_indices.empty()) {
    return;
  }

  line.bbox = words[line.word_indices[0]].bbox;
  for (size_t i = 1; i < line.word_indices.size(); ++i) {
    line.bbox = UnionRects(line.bbox, words[line.word_indices[i]].bbox);
  }

  std::vector<float> baselines;
  baselines.reserve(line.word_indices.size());
  for (int wi : line.word_indices) {
    baselines.push_back(words[wi].baseline_y);
  }
  std::nth_element(baselines.begin(), baselines.begin() + baselines.size() / 2,
                   baselines.end());
  line.baseline_y = baselines[baselines.size() / 2];

  std::vector<float> heights;
  heights.reserve(line.word_indices.size());
  for (int wi : line.word_indices) {
    heights.push_back(words[wi].median_height);
  }
  std::nth_element(heights.begin(), heights.begin() + heights.size() / 2,
                   heights.end());
  line.line_height = heights[heights.size() / 2];

  line.object_ids.clear();
  for (int wi : line.word_indices) {
    for (ObjectId id : words[wi].object_ids) {
      line.object_ids.insert(id);
    }
  }
}

int LineBuilder::Build(const std::vector<WordItem>& words,
                        const std::vector<GlyphItem>& glyphs,
                        const AdaptiveParams& params) {
  lines_.clear();
  merge_log_.clear();

  if (words.empty()) {
    return 0;
  }

  CFX_FloatRect bounds;
  bounds.left = bounds.bottom = 1e9f;
  bounds.right = bounds.top = -1e9f;
  for (const auto& w : words) {
    if (!w.is_empty()) {
      bounds.left = std::min(bounds.left, RectLeft(w.bbox));
      bounds.bottom = std::min(bounds.bottom, RectBottom(w.bbox));
      bounds.right = std::max(bounds.right, RectRight(w.bbox));
      bounds.top = std::max(bounds.top, RectTop(w.bbox));
    }
  }

  if (bounds.left >= bounds.right || bounds.bottom >= bounds.top) {
    return 0;
  }

  SpatialGrid grid(bounds, params.grid_cell_size);
  for (size_t i = 0; i < words.size(); ++i) {
    if (!words[i].is_empty()) {
      grid.Insert(static_cast<int>(i), words[i].bbox);
    }
  }

  ClusterWords(words, grid, params);

  return static_cast<int>(lines_.size());
}

}  // namespace layout
}  // namespace pdfium
