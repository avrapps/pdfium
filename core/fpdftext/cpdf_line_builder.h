// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFTEXT_CPDF_LINE_BUILDER_H_
#define CORE_FPDFTEXT_CPDF_LINE_BUILDER_H_

#include <vector>

#include "core/fpdftext/cpdf_layout_types.h"
#include "core/fpdftext/cpdf_spatial_grid.h"

namespace pdfium {
namespace layout {

// Union-Find data structure for clustering
class UnionFind {
 public:
  explicit UnionFind(size_t n);

  int Find(int x);
  void Unite(int x, int y);

  std::vector<std::vector<int>> GetClusters() const;

 private:
  std::vector<int> parent_;
  std::vector<int> rank_;
};

// Production-grade line builder using two-phase approach:
// Phase 1: Build micro-runs using stream order as a strong prior
// Phase 2: Merge micro-runs into lines using baseline-anchored checks
class LineBuilder {
 public:
  LineBuilder();
  ~LineBuilder();

  // Build lines from glyphs using two-phase approach.
  // Returns the number of lines built.
  int BuildFromGlyphs(const std::vector<GlyphItem>& glyphs,
                      const AdaptiveParams& params);

  // Legacy: Build lines from words (kept for compatibility)
  int Build(const std::vector<WordItem>& words,
            const std::vector<GlyphItem>& glyphs,
            const AdaptiveParams& params);

  // Get built lines
  const std::vector<LineItem>& GetLines() const { return lines_; }
  std::vector<LineItem>& GetMutableLines() { return lines_; }

  // Get micro-runs (for debugging)
  const std::vector<MicroRun>& GetMicroRuns() const { return micro_runs_; }

  // Get merge log (for debugging)
  const std::vector<MergeDecision>& GetMergeLog() const { return merge_log_; }

  // Enable/disable debug logging
  void SetDebugEnabled(bool enabled) { debug_enabled_ = enabled; }

 private:
  // Phase 1: Build micro-runs using stream order + dx sanity gate
  void BuildMicroRuns(const std::vector<GlyphItem>& glyphs,
                      const AdaptiveParams& params);

  // Phase 2: Merge micro-runs into lines
  void MergeMicroRunsIntoLines(const std::vector<GlyphItem>& glyphs,
                               const AdaptiveParams& params);

  // Check if two micro-runs can be merged (baseline-anchored)
  bool CanMergeRuns(const MicroRun& r1,
                    const MicroRun& r2,
                    const AdaptiveParams& params) const;

  // Score a merge candidate (for neighbor selection)
  float ScoreMergeCandidate(const MicroRun& r1,
                            const MicroRun& r2,
                            const AdaptiveParams& params) const;

  // Annotate lines with suspicious gaps (don't split, just mark)
  void AnnotateSuspiciousGaps(const std::vector<GlyphItem>& glyphs,
                              const AdaptiveParams& params);

  // Sort glyphs within each line by origin_x
  void SortGlyphsInLines(const std::vector<GlyphItem>& glyphs);

  // Compute line bounds from glyphs
  void ComputeLineBoundsFromGlyphs(LineItem& line,
                                   const std::vector<GlyphItem>& glyphs);

  // Coarse column detection (inserted between Phase 1 and Phase 2)
  void DetectCoarseColumns(const AdaptiveParams& params);
  void AssignMicroRunColumns();

  // Legacy methods for word-based approach
  void ClusterWords(const std::vector<WordItem>& words,
                    const SpatialGrid& grid,
                    const AdaptiveParams& params);
  void ComputeLineBounds(LineItem& line, const std::vector<WordItem>& words);

  std::vector<MicroRun> micro_runs_;
  std::vector<LineItem> lines_;
  std::vector<MergeDecision> merge_log_;
  CoarseColumnModel coarse_columns_;  // Coarse column model for gating
  bool debug_enabled_ = false;
};

}  // namespace layout
}  // namespace pdfium

#endif  // CORE_FPDFTEXT_CPDF_LINE_BUILDER_H_
