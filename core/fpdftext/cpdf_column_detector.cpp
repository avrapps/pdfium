// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdftext/cpdf_column_detector.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pdfium {
namespace layout {

ColumnDetector::ColumnDetector() = default;

ColumnDetector::~ColumnDetector() = default;

bool ColumnDetector::IsInsideHighConfidenceZone(
    const CFX_FloatRect& bbox,
    const std::vector<ProvisionalTableZone>& zones,
    float min_confidence) {
  for (const auto& zone : zones) {
    if (zone.confidence >= min_confidence && RectContains(zone.bbox, bbox)) {
      return true;
    }
  }
  return false;
}

void ColumnDetector::ComputeLineWeights(
    const std::vector<LineItem>& lines,
    const std::vector<WordItem>& words,
    const std::vector<ProvisionalTableZone>& table_zones,
    const PageStats& stats,
    const AdaptiveParams& params) {
  line_weights_.clear();
  line_weights_.reserve(lines.size());

  for (size_t i = 0; i < lines.size(); ++i) {
    const LineItem& line = lines[i];
    LineWeight lw;
    lw.line_idx = static_cast<int>(i);
    lw.weight = 0.0f;
    lw.excluded = false;

    // Skip empty lines
    if (line.is_empty()) {
      lw.excluded = true;
      lw.exclusion_reason = "empty";
      line_weights_.push_back(lw);
      continue;
    }

    float line_width = RectWidth(line.bbox);

    // Exclusion: spanning lines (> 80% page width)
    if (line_width > stats.page_width * 0.8f) {
      lw.excluded = true;
      lw.exclusion_reason = "spanning";
    }
    // Exclusion: inside high-confidence provisional table zone
    else if (IsInsideHighConfidenceZone(line.bbox, table_zones, 0.7f)) {
      lw.excluded = true;
      lw.exclusion_reason = "table";
    } else {
      // Base weight by ink coverage
      lw.weight = line.ink_width();

      // PROPORTIONAL downweight for short lines (< 20% page width)
      // 18% line penalized less than 3% label
      if (line_width < stats.page_width * 0.2f) {
        float ratio = line_width / (stats.page_width * 0.2f);
        lw.weight *= std::clamp(ratio, 0.2f, 1.0f);
        // Don't exclude, just downweight
      }

      // Downweight large fonts (titles)
      if (line.line_height > stats.median_glyph_height * 1.8f) {
        lw.weight *= 0.3f;
      }

      // Downweight ragged lines
      if (line.is_ragged) {
        lw.weight *= 0.5f;
      }

      // Downweight lines in medium-confidence table zones
      for (const auto& zone : table_zones) {
        if (zone.confidence >= 0.4f && zone.confidence < 0.7f &&
            RectsIntersect(line.bbox, zone.bbox)) {
          lw.weight *= 0.5f;
          break;
        }
      }
    }

    line_weights_.push_back(lw);
  }
}

void ColumnDetector::BuildHistogram(const std::vector<LineItem>& lines,
                                     const PageStats& stats) {
  int hist_size = static_cast<int>(std::ceil(stats.page_width)) + 2;
  histogram_.clear();
  histogram_.resize(hist_size, 0.0f);

  // Use difference array for O(lines + page_width) complexity
  std::vector<float> diff(hist_size + 1, 0.0f);
  float page_left = RectLeft(stats.page_bounds);

  for (const LineWeight& lw : line_weights_) {
    if (lw.excluded) {
      continue;
    }

    const LineItem& line = lines[lw.line_idx];
    int left_idx = static_cast<int>(RectLeft(line.bbox) - page_left);
    int right_idx = static_cast<int>(RectRight(line.bbox) - page_left);

    left_idx = std::clamp(left_idx, 0, hist_size - 1);
    right_idx = std::clamp(right_idx, 0, hist_size - 1);

    diff[left_idx] += lw.weight;
    diff[right_idx + 1] -= lw.weight;
  }

  // Convert to histogram via prefix sum
  float cumsum = 0.0f;
  for (int i = 0; i < hist_size; ++i) {
    cumsum += diff[i];
    histogram_[i] = cumsum;
  }

  // Smooth histogram (simple box filter)
  constexpr int kSmoothWindow = 5;
  std::vector<float> smoothed(hist_size, 0.0f);
  for (int i = 0; i < hist_size; ++i) {
    float sum = 0.0f;
    int count = 0;
    for (int j = std::max(0, i - kSmoothWindow);
         j <= std::min(hist_size - 1, i + kSmoothWindow); ++j) {
      sum += histogram_[j];
      count++;
    }
    smoothed[i] = sum / count;
  }
  histogram_ = std::move(smoothed);
}

std::vector<float> ColumnDetector::FindGutters(const PageStats& stats,
                                                const AdaptiveParams& params) {
  std::vector<float> gutters_x;  // Page X coordinates, not indices

  if (histogram_.empty()) {
    return gutters_x;
  }

  float peak = *std::max_element(histogram_.begin(), histogram_.end());
  if (peak < 0.001f) {
    return gutters_x;  // No content
  }

  float page_left = RectLeft(stats.page_bounds);
  float valley_threshold = peak * params.gutter_valley_ratio;
  int hist_size = static_cast<int>(histogram_.size());

  // Find valleys: regions where histogram dips below threshold
  bool in_valley = false;
  int valley_start = 0;
  float valley_min = 1e9f;
  int valley_min_idx = 0;

  for (int i = 0; i < hist_size; ++i) {
    if (histogram_[i] < valley_threshold) {
      if (!in_valley) {
        in_valley = true;
        valley_start = i;
        valley_min = histogram_[i];
        valley_min_idx = i;
      } else {
        if (histogram_[i] < valley_min) {
          valley_min = histogram_[i];
          valley_min_idx = i;
        }
      }
    } else {
      if (in_valley) {
        // End of valley
        int valley_width = i - valley_start;
        if (valley_width >= static_cast<int>(params.gutter_min_width)) {
          // Convert to page X coordinate immediately
          float gutter_x = page_left + static_cast<float>(valley_min_idx);
          gutters_x.push_back(gutter_x);
        }
        in_valley = false;
      }
    }
  }

  // Handle valley at end
  if (in_valley) {
    int valley_width = hist_size - valley_start;
    if (valley_width >= static_cast<int>(params.gutter_min_width)) {
      float gutter_x = page_left + static_cast<float>(valley_min_idx);
      gutters_x.push_back(gutter_x);
    }
  }

  return gutters_x;
}

void ColumnDetector::CollectSuspiciousGapSeeds(
    const std::vector<LineItem>& lines,
    const AdaptiveParams& params) {
  std::vector<float> raw_seeds;

  for (const LineItem& line : lines) {
    if (line.has_suspicious_gap) {
      // Filter: ignore seeds from very short or very large lines
      float line_width = RectWidth(line.bbox);
      if (line_width > params.median_height * 3.0f &&
          line.line_height < params.median_height * 2.5f) {
        raw_seeds.push_back(line.suspicious_gap_x);
      }
    }
  }

  // Cluster seeds within tolerance
  float cluster_tol = std::max(params.gutter_min_width * 0.5f,
                               2.0f * params.median_advance);
  gutter_seed_clusters_ = ClusterSeeds(raw_seeds, cluster_tol);
}

std::vector<float> ColumnDetector::ClusterSeeds(
    const std::vector<float>& raw_seeds,
    float tolerance) {
  if (raw_seeds.empty()) {
    return {};
  }

  std::vector<float> sorted = raw_seeds;
  std::sort(sorted.begin(), sorted.end());

  std::vector<float> clusters;
  std::vector<float> current_cluster;
  current_cluster.push_back(sorted[0]);

  for (size_t i = 1; i < sorted.size(); ++i) {
    if (sorted[i] - sorted[i - 1] <= tolerance) {
      current_cluster.push_back(sorted[i]);
    } else {
      // Compute median of current cluster
      std::nth_element(current_cluster.begin(),
                       current_cluster.begin() + current_cluster.size() / 2,
                       current_cluster.end());
      clusters.push_back(current_cluster[current_cluster.size() / 2]);
      current_cluster.clear();
      current_cluster.push_back(sorted[i]);
    }
  }

  // Final cluster
  if (!current_cluster.empty()) {
    std::nth_element(current_cluster.begin(),
                     current_cluster.begin() + current_cluster.size() / 2,
                     current_cluster.end());
    clusters.push_back(current_cluster[current_cluster.size() / 2]);
  }

  return clusters;
}

std::vector<float> ColumnDetector::MergeGutterCandidates(
    const std::vector<float>& histogram_gutters,
    const std::vector<float>& seed_clusters,
    const AdaptiveParams& params) {
  std::vector<float> all;
  all.insert(all.end(), histogram_gutters.begin(), histogram_gutters.end());
  all.insert(all.end(), seed_clusters.begin(), seed_clusters.end());

  if (all.empty()) {
    return {};
  }

  std::sort(all.begin(), all.end());

  // Merge candidates within tolerance
  std::vector<float> merged;
  for (float x : all) {
    if (merged.empty() || x - merged.back() > params.gutter_min_width) {
      merged.push_back(x);
    } else {
      // Keep average (weighted median would be better but this is simpler)
      merged.back() = (merged.back() + x) / 2.0f;
    }
  }

  // Limit to 1 gutter unless we have strong evidence for more
  // (most 2-column pages need exactly one gutter)
  if (merged.size() > 1) {
    // Simple heuristic: if histogram didn't find multiple strong gutters,
    // trust only the strongest (middle-most) one
    if (histogram_gutters.size() <= 1) {
      // Keep the gutter closest to page center
      float page_center = 0.5f * (merged.front() + merged.back());
      std::sort(merged.begin(), merged.end(), [page_center](float a, float b) {
        return std::abs(a - page_center) < std::abs(b - page_center);
      });
      merged.resize(1);
    }
  }

  // Re-sort by position
  std::sort(merged.begin(), merged.end());
  return merged;
}

void ColumnDetector::BuildColumnBounds(const std::vector<float>& gutters_x,
                                        const PageStats& stats) {
  model_.gutter_positions = gutters_x;  // Now in page coords
  model_.column_bounds.clear();

  float page_left = RectLeft(stats.page_bounds);
  float page_right = RectRight(stats.page_bounds);
  float page_top = RectTop(stats.page_bounds);
  float page_bottom = RectBottom(stats.page_bounds);

  if (gutters_x.empty()) {
    // Single column
    CFX_FloatRect col;
    col.left = page_left;
    col.right = page_right;
    col.top = page_top;
    col.bottom = page_bottom;
    model_.column_bounds.push_back(col);
    return;
  }

  // Gutters are already in page coords - no conversion needed
  std::vector<float> sorted_gutters = gutters_x;
  std::sort(sorted_gutters.begin(), sorted_gutters.end());

  // Build columns between gutters
  float prev_x = page_left;
  for (float gx : sorted_gutters) {
    if (gx > prev_x + 10.0f) {  // Minimum column width
      CFX_FloatRect col;
      col.left = prev_x;
      col.right = gx;
      col.top = page_top;
      col.bottom = page_bottom;
      model_.column_bounds.push_back(col);
    }
    prev_x = gx;
  }

  // Final column
  if (page_right > prev_x + 10.0f) {
    CFX_FloatRect col;
    col.left = prev_x;
    col.right = page_right;
    col.top = page_top;
    col.bottom = page_bottom;
    model_.column_bounds.push_back(col);
  }

  // Sort by X position
  std::sort(model_.column_bounds.begin(), model_.column_bounds.end(),
            [](const CFX_FloatRect& a, const CFX_FloatRect& b) {
              return RectLeft(a) < RectLeft(b);
            });
}

bool ColumnDetector::VerifyGutterDensity(const std::vector<LineItem>& lines,
                                          const PageStats& stats) {
  // Now gutter_positions are in page coords, so we use them directly
  for (float gutter_x : model_.gutter_positions) {
    int lines_crossing = 0;
    int total_sampled = 0;

    for (const LineWeight& lw : line_weights_) {
      if (lw.excluded) {
        continue;
      }
      const LineItem& line = lines[lw.line_idx];
      total_sampled++;

      // Check if line crosses gutter (gutter_x is already in page coords)
      if (RectLeft(line.bbox) < gutter_x && RectRight(line.bbox) > gutter_x) {
        lines_crossing++;
      }
    }

    if (total_sampled > 0 &&
        static_cast<float>(lines_crossing) / total_sampled > 0.15f) {
      return false;  // Too many lines cross this gutter
    }
  }
  return true;
}

bool ColumnDetector::ValidateModel(const std::vector<LineItem>& lines,
                                    const PageStats& stats,
                                    const AdaptiveParams& params) {
  if (model_.column_bounds.size() < 2) {
    return true;  // Single column is always valid
  }

  int cleanly_assigned = 0;
  int total_eligible = 0;

  for (const LineWeight& lw : line_weights_) {
    if (lw.excluded) {
      continue;
    }
    total_eligible++;

    const LineItem& line = lines[lw.line_idx];

    // Find best column overlap
    float best_overlap = 0.0f;
    for (const auto& col : model_.column_bounds) {
      float overlap = HorizontalOverlap(line.bbox, col);
      float line_width = RectWidth(line.bbox);
      float ratio = line_width > 0 ? overlap / line_width : 0;
      best_overlap = std::max(best_overlap, ratio);
    }

    if (best_overlap >= 0.7f) {
      cleanly_assigned++;
    }
  }

  if (total_eligible == 0) {
    return true;  // No lines to validate
  }

  float assignment_ratio =
      static_cast<float>(cleanly_assigned) / total_eligible;

  // Check: columns shouldn't overlap heavily
  for (size_t i = 0; i + 1 < model_.column_bounds.size(); ++i) {
    float overlap =
        HorizontalOverlap(model_.column_bounds[i], model_.column_bounds[i + 1]);
    if (overlap > stats.page_width * 0.1f) {
      return false;
    }
  }

  // Check: gutter shouldn't be at page edge (using page coords now)
  float page_left = RectLeft(stats.page_bounds);
  for (float gutter_x : model_.gutter_positions) {
    float relative_pos = (gutter_x - page_left) / stats.page_width;
    if (relative_pos < 0.1f || relative_pos > 0.9f) {
      if (assignment_ratio < 0.8f) {
        return false;
      }
    }
  }

  // Check: columns should have similar fill ratios
  std::vector<float> fill_ratios;
  for (const auto& col : model_.column_bounds) {
    float filled = 0.0f;
    float total = 0.0f;
    for (const LineWeight& lw : line_weights_) {
      if (lw.excluded) {
        continue;
      }
      const LineItem& line = lines[lw.line_idx];
      float overlap = HorizontalOverlap(line.bbox, col);
      if (overlap > RectWidth(line.bbox) * 0.5f) {
        filled += lw.weight;
      }
      total += lw.weight;
    }
    if (total > 0) {
      fill_ratios.push_back(filled / total);
    }
  }

  if (fill_ratios.size() >= 2) {
    float max_ratio = *std::max_element(fill_ratios.begin(), fill_ratios.end());
    float min_ratio = *std::min_element(fill_ratios.begin(), fill_ratios.end());
    if (max_ratio > 0 && min_ratio / max_ratio < 0.2f) {
      return false;  // Very uneven fill
    }
  }

  // Verify gutter density (now with correct page coords)
  bool gutters_stable = VerifyGutterDensity(lines, stats);

  return assignment_ratio >= 0.7f && gutters_stable;
}

ColumnModel ColumnDetector::Detect(
    const std::vector<LineItem>& lines,
    const std::vector<WordItem>& words,
    const std::vector<ProvisionalTableZone>& table_zones,
    const PageStats& stats,
    const AdaptiveParams& params) {
  model_ = ColumnModel();
  gutter_seed_clusters_.clear();

  if (lines.empty()) {
    // Return single-column fallback
    CFX_FloatRect col = stats.page_bounds;
    model_.column_bounds.push_back(col);
    model_.is_valid = true;
    model_.confidence = 0.5f;
    return model_;
  }

  // Compute line weights with proportional downweighting
  ComputeLineWeights(lines, words, table_zones, stats, params);

  // Build histogram
  BuildHistogram(lines, stats);

  // Find gutters from histogram (returns page coords)
  auto histogram_gutters = FindGutters(stats, params);

  // Collect suspicious gap seeds from lines
  CollectSuspiciousGapSeeds(lines, params);

  // Merge histogram gutters with seed clusters
  auto merged_gutters = MergeGutterCandidates(
      histogram_gutters, gutter_seed_clusters_, params);

  // Build column bounds (gutters already in page coords)
  BuildColumnBounds(merged_gutters, stats);

  // Validate
  if (!ValidateModel(lines, stats, params)) {
    // Fallback to single column
    model_.gutter_positions.clear();
    model_.column_bounds.clear();
    model_.column_bounds.push_back(stats.page_bounds);
    model_.is_valid = true;
    model_.confidence = 0.5f;
  } else {
    model_.is_valid = true;
    model_.confidence = 0.9f;
  }

  return model_;
}

}  // namespace layout
}  // namespace pdfium
