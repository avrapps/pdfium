// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdftext/cpdf_reading_order.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

namespace pdfium {
namespace layout {

ReadingOrderComputer::ReadingOrderComputer() = default;

ReadingOrderComputer::~ReadingOrderComputer() = default;

int ReadingOrderComputer::GetBlockColumnId(
    const textblock::TextBlock& block,
    const ColumnModel& columns) const {
  // Find column with best overlap
  int best_col = -1;
  float best_overlap = 0.0f;
  int cols_overlapped = 0;

  for (size_t i = 0; i < columns.column_bounds.size(); ++i) {
    float overlap = HorizontalOverlap(block.ink_bounds, columns.column_bounds[i]);
    float block_width = RectWidth(block.ink_bounds);
    float ratio = block_width > 0 ? overlap / block_width : 0;

    if (ratio > 0.3f) {
      cols_overlapped++;
    }
    if (overlap > best_overlap) {
      best_overlap = overlap;
      best_col = static_cast<int>(i);
    }
  }

  // If block spans multiple columns, mark as spanning
  if (cols_overlapped > 1) {
    return -1;  // Spanning
  }

  return best_col;
}

void ReadingOrderComputer::SegmentIntoSections(
    const std::vector<textblock::TextBlock>& blocks,
    const ColumnModel& columns) {
  sections_.clear();

  if (blocks.empty()) {
    return;
  }

  // Sort blocks by top Y (descending = top of page first)
  std::vector<int> block_order(blocks.size());
  std::iota(block_order.begin(), block_order.end(), 0);

  std::stable_sort(block_order.begin(), block_order.end(),
                   [&](int a, int b) {
                     float ya = RectTop(blocks[a].ink_bounds);
                     float yb = RectTop(blocks[b].ink_bounds);
                     if (std::abs(ya - yb) > 0.1f) {
                       return ya > yb;
                     }
                     return a < b;  // Tie-break by index
                   });

  // Segment by spanning transitions
  int section_start = 0;
  bool prev_spanning = false;
  uint32_t prev_signature = 0;

  for (size_t i = 0; i < block_order.size(); ++i) {
    int bi = block_order[i];
    int col_id = GetBlockColumnId(blocks[bi], columns);
    bool is_spanning = (col_id == -1);

    // Compute column signature
    uint32_t signature = 0;
    if (!is_spanning && col_id >= 0 && col_id < 32) {
      signature = 1u << col_id;
    }

    // Detect section break on spanning transition
    bool section_break = false;
    if (i > 0 && is_spanning != prev_spanning) {
      section_break = true;
    }

    if (section_break && static_cast<int>(i) > section_start) {
      // Finalize previous section
      ReadingSection section;
      section.start_block_idx = section_start;
      section.end_block_idx = static_cast<int>(i) - 1;
      section.is_spanning = prev_spanning;
      section.column_signature = prev_signature;
      sections_.push_back(section);

      section_start = static_cast<int>(i);
      prev_signature = 0;
    }

    // Accumulate signature
    if (!is_spanning) {
      prev_signature |= signature;
    }
    prev_spanning = is_spanning;
  }

  // Final section
  ReadingSection section;
  section.start_block_idx = section_start;
  section.end_block_idx = static_cast<int>(block_order.size()) - 1;
  section.is_spanning = prev_spanning;
  section.column_signature = prev_signature;
  sections_.push_back(section);
}

std::vector<int> ReadingOrderComputer::SortSectionBlocks(
    const std::vector<int>& block_indices,
    const std::vector<textblock::TextBlock>& blocks,
    const ColumnModel& columns,
    bool is_spanning) const {
  std::vector<int> sorted = block_indices;

  if (is_spanning) {
    // Spanning: sort by Y only (top to bottom)
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&](int a, int b) {
                       float ya = RectTop(blocks[a].ink_bounds);
                       float yb = RectTop(blocks[b].ink_bounds);
                       if (std::abs(ya - yb) > 0.1f) {
                         return ya > yb;
                       }
                       return a < b;
                     });
  } else {
    // Columnar: group by column, order columns by X, sort within by Y
    std::map<int, std::vector<int>> by_col;
    for (int bi : sorted) {
      int col_id = GetBlockColumnId(blocks[bi], columns);
      by_col[col_id].push_back(bi);
    }

    // Sort within each column by Y (top to bottom)
    for (auto& [col_id, col_blocks] : by_col) {
      std::stable_sort(col_blocks.begin(), col_blocks.end(),
                       [&](int a, int b) {
                         float ya = RectTop(blocks[a].ink_bounds);
                         float yb = RectTop(blocks[b].ink_bounds);
                         if (std::abs(ya - yb) > 0.1f) {
                           return ya > yb;
                         }
                         return a < b;
                       });
    }

    // Build column order sorted by X position
    std::vector<int> col_order(columns.column_bounds.size());
    std::iota(col_order.begin(), col_order.end(), 0);
    std::sort(col_order.begin(), col_order.end(),
              [&](int a, int b) {
                return RectLeft(columns.column_bounds[a]) <
                       RectLeft(columns.column_bounds[b]);
              });

    // Interleave columns in L→R order
    sorted.clear();
    for (int col_idx : col_order) {
      if (by_col.count(col_idx)) {
        for (int bi : by_col[col_idx]) {
          sorted.push_back(bi);
        }
      }
    }

    // Add any spanning blocks that were in this section (col_id == -1)
    if (by_col.count(-1)) {
      for (int bi : by_col[-1]) {
        sorted.push_back(bi);
      }
    }
  }

  return sorted;
}

void ReadingOrderComputer::ComputeReadingOrder(
    std::vector<textblock::TextBlock>& blocks,
    const ColumnModel& columns) {
  if (blocks.empty()) {
    return;
  }

  // Segment into sections
  SegmentIntoSections(blocks, columns);

  // Sort blocks by Y initially to get block indices for sections
  std::vector<int> block_order(blocks.size());
  std::iota(block_order.begin(), block_order.end(), 0);
  std::stable_sort(block_order.begin(), block_order.end(),
                   [&](int a, int b) {
                     float ya = RectTop(blocks[a].ink_bounds);
                     float yb = RectTop(blocks[b].ink_bounds);
                     if (std::abs(ya - yb) > 0.1f) {
                       return ya > yb;
                     }
                     return a < b;
                   });

  // Process each section and assign reading order
  int order = 0;

  for (const ReadingSection& section : sections_) {
    // Gather block indices in this section
    std::vector<int> section_block_indices;
    for (int i = section.start_block_idx; i <= section.end_block_idx; ++i) {
      if (i >= 0 && i < static_cast<int>(block_order.size())) {
        section_block_indices.push_back(block_order[i]);
      }
    }

    // Sort blocks within section
    auto sorted = SortSectionBlocks(section_block_indices, blocks, columns,
                                    section.is_spanning);

    // Assign reading order
    for (int bi : sorted) {
      // Note: textblock::TextBlock doesn't have reading_order field in current
      // definition. We'll need to extend it or use the block ID.
      // For now, we reorder the blocks vector and update IDs.
      // This is handled by reordering blocks before returning.
    }
  }

  // Rebuild block order and reassign IDs
  std::vector<int> final_order;
  for (const ReadingSection& section : sections_) {
    std::vector<int> section_block_indices;
    for (int i = section.start_block_idx; i <= section.end_block_idx; ++i) {
      if (i >= 0 && i < static_cast<int>(block_order.size())) {
        section_block_indices.push_back(block_order[i]);
      }
    }

    auto sorted = SortSectionBlocks(section_block_indices, blocks, columns,
                                    section.is_spanning);
    for (int bi : sorted) {
      final_order.push_back(bi);
    }
  }

  // Reorder blocks
  std::vector<textblock::TextBlock> reordered;
  reordered.reserve(blocks.size());
  for (int bi : final_order) {
    reordered.push_back(std::move(blocks[bi]));
  }

  // Reassign IDs
  for (size_t i = 0; i < reordered.size(); ++i) {
    reordered[i].id = static_cast<int32_t>(i);
  }

  blocks = std::move(reordered);
}

}  // namespace layout
}  // namespace pdfium
