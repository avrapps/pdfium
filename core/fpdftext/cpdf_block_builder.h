// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFTEXT_CPDF_BLOCK_BUILDER_H_
#define CORE_FPDFTEXT_CPDF_BLOCK_BUILDER_H_

#include <vector>

#include "core/fpdftext/cpdf_layout_types.h"
#include "core/fpdftext/cpdf_spatial_grid.h"
#include "core/fpdftext/cpdf_textblock_detector.h"

namespace pdfium {
namespace layout {

// Builds text blocks from lines using neighbor graph clustering.
// This is Phase 5 of the layout detection pipeline.
class BlockBuilder {
 public:
  BlockBuilder();
  ~BlockBuilder();

  // Build blocks from lines.
  // Returns the number of blocks built.
  int Build(const std::vector<LineItem>& lines,
            const std::vector<WordItem>& words,
            const std::vector<GlyphItem>& glyphs,
            const ColumnModel& columns,
            const std::vector<Table>& tables,
            const AdaptiveParams& params);

  // Get built blocks (uses textblock::TextBlock from detector header)
  const std::vector<textblock::TextBlock>& GetBlocks() const { return blocks_; }
  std::vector<textblock::TextBlock>& GetMutableBlocks() { return blocks_; }

  // Get merge log (for debugging)
  const std::vector<MergeDecision>& GetMergeLog() const { return merge_log_; }

  // Enable/disable debug logging
  void SetDebugEnabled(bool enabled) { debug_enabled_ = enabled; }

 private:
  // Assign column IDs to lines
  void AssignLineColumns(std::vector<LineItem>& lines,
                         const ColumnModel& columns);

  // Check if a rect is inside any table
  bool IsInsideTable(const CFX_FloatRect& bbox,
                     const std::vector<Table>& tables) const;

  // Check if line pair crosses a gutter
  bool CrossesGutter(const LineItem& l1,
                     const LineItem& l2,
                     const std::vector<float>& gutters,
                     float page_left) const;

  // Build neighbor graph for block clustering
  void BuildNeighborGraph(const std::vector<LineItem>& lines,
                          const ColumnModel& columns,
                          const std::vector<Table>& tables,
                          const AdaptiveParams& params,
                          SpatialGrid& grid);

  // Cluster lines into blocks using the neighbor graph
  void ClusterIntoBlocks(const std::vector<LineItem>& lines,
                         const std::vector<WordItem>& words,
                         const std::vector<GlyphItem>& glyphs,
                         const AdaptiveParams& params);

  // Convert a line cluster to a TextBlock
  textblock::TextBlock CreateBlock(const std::vector<int>& line_indices,
                                   const std::vector<LineItem>& lines,
                                   const std::vector<WordItem>& words,
                                   const std::vector<GlyphItem>& glyphs,
                                   int block_id);

  // Adjacency list for neighbor graph
  std::vector<std::vector<int>> adjacency_;

  // Page left offset for gutter calculations
  float page_left_ = 0.0f;

  std::vector<textblock::TextBlock> blocks_;
  std::vector<MergeDecision> merge_log_;
  bool debug_enabled_ = false;
};

}  // namespace layout
}  // namespace pdfium

#endif  // CORE_FPDFTEXT_CPDF_BLOCK_BUILDER_H_
