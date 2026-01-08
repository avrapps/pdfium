// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFTEXT_CPDF_COLUMN_DETECTOR_H_
#define CORE_FPDFTEXT_CPDF_COLUMN_DETECTOR_H_

#include <vector>

#include "core/fpdftext/cpdf_layout_types.h"

namespace pdfium {
namespace layout {

// Detects columns in a page using weighted X-histograms and suspicious gap seeds.
// This is Phase 3 of the layout detection pipeline.
class ColumnDetector {
 public:
  ColumnDetector();
  ~ColumnDetector();

  // Detect columns from lines.
  // Returns the column model (may be single-column if detection fails sanity checks).
  ColumnModel Detect(const std::vector<LineItem>& lines,
                     const std::vector<WordItem>& words,
                     const std::vector<ProvisionalTableZone>& table_zones,
                     const PageStats& stats,
                     const AdaptiveParams& params);

  // Get line weights (for debugging)
  const std::vector<LineWeight>& GetLineWeights() const {
    return line_weights_;
  }

 private:
  // Compute line weights with proportional downweighting for short lines
  void ComputeLineWeights(const std::vector<LineItem>& lines,
                          const std::vector<WordItem>& words,
                          const std::vector<ProvisionalTableZone>& table_zones,
                          const PageStats& stats,
                          const AdaptiveParams& params);

  // Build weighted X-histogram using difference array
  void BuildHistogram(const std::vector<LineItem>& lines,
                      const PageStats& stats);

  // Find valleys (gutters) in the histogram - returns page X coordinates
  std::vector<float> FindGutters(const PageStats& stats,
                                 const AdaptiveParams& params);

  // Collect suspicious gap seeds from lines
  void CollectSuspiciousGapSeeds(const std::vector<LineItem>& lines,
                                 const AdaptiveParams& params);

  // Cluster seed values within tolerance
  std::vector<float> ClusterSeeds(const std::vector<float>& raw_seeds,
                                  float tolerance);

  // Merge histogram gutters with seed clusters
  std::vector<float> MergeGutterCandidates(
      const std::vector<float>& histogram_gutters,
      const std::vector<float>& seed_clusters,
      const AdaptiveParams& params);

  // Build column bounds from gutters (already in page coords)
  void BuildColumnBounds(const std::vector<float>& gutters_x,
                         const PageStats& stats);

  // Validate column model
  bool ValidateModel(const std::vector<LineItem>& lines,
                     const PageStats& stats,
                     const AdaptiveParams& params);

  // Verify gutter low-density (uses stats.page_bounds.left)
  bool VerifyGutterDensity(const std::vector<LineItem>& lines,
                           const PageStats& stats);

  // Helper: check if line is inside a high-confidence table zone
  bool IsInsideHighConfidenceZone(
      const CFX_FloatRect& bbox,
      const std::vector<ProvisionalTableZone>& zones,
      float min_confidence);

  std::vector<LineWeight> line_weights_;
  std::vector<float> histogram_;
  std::vector<float> gutter_seed_clusters_;
  ColumnModel model_;
};

}  // namespace layout
}  // namespace pdfium

#endif  // CORE_FPDFTEXT_CPDF_COLUMN_DETECTOR_H_
