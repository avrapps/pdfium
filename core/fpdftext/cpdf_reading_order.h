// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFTEXT_CPDF_READING_ORDER_H_
#define CORE_FPDFTEXT_CPDF_READING_ORDER_H_

#include <vector>

#include "core/fpdftext/cpdf_layout_types.h"
#include "core/fpdftext/cpdf_textblock_detector.h"

namespace pdfium {
namespace layout {

// Computes reading order for text blocks.
// This is Phase 6 of the layout detection pipeline.
class ReadingOrderComputer {
 public:
  ReadingOrderComputer();
  ~ReadingOrderComputer();

  // Segment blocks into sections and assign reading order.
  // Modifies blocks in place to set reading_order field.
  void ComputeReadingOrder(std::vector<textblock::TextBlock>& blocks,
                           const ColumnModel& columns);

  // Get computed sections (for debugging)
  const std::vector<ReadingSection>& GetSections() const { return sections_; }

 private:
  // Segment blocks into sections based on spanning behavior
  void SegmentIntoSections(const std::vector<textblock::TextBlock>& blocks,
                           const ColumnModel& columns);

  // Get column ID for a block based on its position
  int GetBlockColumnId(const textblock::TextBlock& block,
                       const ColumnModel& columns) const;

  // Sort blocks within a section
  std::vector<int> SortSectionBlocks(
      const std::vector<int>& block_indices,
      const std::vector<textblock::TextBlock>& blocks,
      const ColumnModel& columns,
      bool is_spanning) const;

  std::vector<ReadingSection> sections_;
};

}  // namespace layout
}  // namespace pdfium

#endif  // CORE_FPDFTEXT_CPDF_READING_ORDER_H_
