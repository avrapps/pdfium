// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdftext/cpdf_block_builder.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

#include "core/fpdftext/cpdf_line_builder.h"

namespace pdfium {
namespace layout {

BlockBuilder::BlockBuilder() = default;

BlockBuilder::~BlockBuilder() = default;

void BlockBuilder::AssignLineColumns(std::vector<LineItem>& lines,
                                      const ColumnModel& columns) {
  for (auto& line : lines) {
    if (line.is_empty()) {
      line.column_id = -1;
      continue;
    }

    // Check for spanning (> 80% of combined column width or multiple columns)
    float line_width = RectWidth(line.bbox);
    float total_col_width = 0.0f;
    for (const auto& col : columns.column_bounds) {
      total_col_width += RectWidth(col);
    }

    // Count how many columns this line overlaps significantly
    int cols_overlapped = 0;
    int best_col = -1;
    float best_overlap = 0.0f;

    for (size_t i = 0; i < columns.column_bounds.size(); ++i) {
      float overlap = HorizontalOverlap(line.bbox, columns.column_bounds[i]);
      float ratio = RectWidth(line.bbox) > 0
                        ? overlap / RectWidth(line.bbox)
                        : 0;
      if (ratio > 0.3f) {
        cols_overlapped++;
      }
      if (overlap > best_overlap) {
        best_overlap = overlap;
        best_col = static_cast<int>(i);
      }
    }

    // If line spans multiple columns or is very wide, mark as spanning
    if (cols_overlapped > 1 ||
        (columns.column_bounds.size() > 1 &&
         line_width > total_col_width * 0.7f)) {
      line.column_id = -1;  // Spanning
    } else {
      line.column_id = best_col;
    }
  }
}

bool BlockBuilder::IsInsideTable(const CFX_FloatRect& bbox,
                                  const std::vector<Table>& tables) const {
  for (const auto& table : tables) {
    if (RectContains(table.bbox, bbox)) {
      return true;
    }
  }
  return false;
}

bool BlockBuilder::CrossesGutter(const LineItem& l1,
                                  const LineItem& l2,
                                  const std::vector<float>& gutters,
                                  float page_left) const {
  float left1 = RectLeft(l1.bbox);
  float right1 = RectRight(l1.bbox);
  float left2 = RectLeft(l2.bbox);
  float right2 = RectRight(l2.bbox);

  // Check if any gutter falls between the two lines' x ranges
  for (float g : gutters) {
    float gutter_x = page_left + g;
    // Lines on opposite sides of the gutter
    if ((right1 < gutter_x && left2 > gutter_x) ||
        (right2 < gutter_x && left1 > gutter_x)) {
      return true;
    }
  }
  return false;
}

void BlockBuilder::BuildNeighborGraph(const std::vector<LineItem>& lines,
                                       const ColumnModel& columns,
                                       const std::vector<Table>& tables,
                                       const AdaptiveParams& params,
                                       SpatialGrid& grid) {
  adjacency_.clear();
  adjacency_.resize(lines.size());

  for (size_t i = 0; i < lines.size(); ++i) {
    const LineItem& line = lines[i];
    if (line.is_empty()) {
      continue;
    }

    // Skip lines inside tables
    if (IsInsideTable(line.bbox, tables)) {
      continue;
    }

    // Query lines below (in PDF coords, lower Y = below)
    float window_h = line.line_height * 2.0f;
    CFX_FloatRect query = MakeRectBelow(line.bbox, window_h);
    query.left -= params.align_tol;
    query.right += params.align_tol;

    auto candidates = grid.Query(query);

    for (int j : candidates) {
      if (static_cast<size_t>(j) == i) {
        continue;
      }

      const LineItem& cand = lines[j];
      if (cand.is_empty()) {
        continue;
      }

      // Must be actually below (lower Y in PDF coords)
      if (!IsBelow(cand.bbox, line.bbox)) {
        continue;
      }

      // Must be same column (or both spanning)
      if (cand.column_id != line.column_id) {
        continue;
      }

      // Must not cross gutter
      if (CrossesGutter(line, cand, columns.gutter_positions, page_left_)) {
        continue;
      }

      // Similar font size (within 1.3x)
      float h_ratio =
          std::max(line.line_height, cand.line_height) /
          std::max(std::min(line.line_height, cand.line_height), 0.001f);
      if (h_ratio > 1.3f) {
        continue;
      }

      // Similar alignment (left, right, or center)
      if (!IsAligned(line.bbox, cand.bbox, params.align_tol)) {
        continue;
      }

      // Connect bidirectionally
      adjacency_[i].push_back(j);
      adjacency_[j].push_back(static_cast<int>(i));

      if (debug_enabled_) {
        MergeDecision decision;
        decision.type = MergeDecision::Type::kLineToBlock;
        decision.from_idx = static_cast<int>(i);
        decision.to_idx = j;
        decision.score = 1.0f;
        decision.reason = "neighbor_graph";
        decision.accepted = true;
        merge_log_.push_back(decision);
      }
    }
  }
}

void BlockBuilder::ClusterIntoBlocks(const std::vector<LineItem>& lines,
                                      const std::vector<WordItem>& words,
                                      const std::vector<GlyphItem>& glyphs,
                                      const AdaptiveParams& params) {
  blocks_.clear();

  // Use union-find to cluster connected components
  UnionFind uf(lines.size());

  for (size_t i = 0; i < adjacency_.size(); ++i) {
    for (int j : adjacency_[i]) {
      uf.Unite(static_cast<int>(i), j);
    }
  }

  auto clusters = uf.GetClusters();
  int block_id = 0;

  for (const auto& cluster : clusters) {
    // Skip empty clusters
    bool has_content = false;
    for (int li : cluster) {
      if (!lines[li].is_empty()) {
        has_content = true;
        break;
      }
    }
    if (!has_content) {
      continue;
    }

    // Sort lines by Y (top to bottom)
    std::vector<int> sorted_lines = cluster;
    std::stable_sort(sorted_lines.begin(), sorted_lines.end(),
                     [&](int a, int b) {
                       return RectTop(lines[a].bbox) > RectTop(lines[b].bbox);
                     });

    textblock::TextBlock block = CreateBlock(sorted_lines, lines, words, glyphs, block_id++);
    blocks_.push_back(std::move(block));
  }

  // Sort blocks by reading position (top to bottom, left to right)
  std::stable_sort(blocks_.begin(), blocks_.end(),
                   [](const textblock::TextBlock& a, const textblock::TextBlock& b) {
                     if (std::abs(RectTop(a.ink_bounds) - RectTop(b.ink_bounds)) > 1.0f) {
                       return RectTop(a.ink_bounds) > RectTop(b.ink_bounds);
                     }
                     return RectLeft(a.ink_bounds) < RectLeft(b.ink_bounds);
                   });

  // Reassign IDs after sorting
  for (size_t i = 0; i < blocks_.size(); ++i) {
    blocks_[i].id = static_cast<int32_t>(i);
  }
}

textblock::TextBlock BlockBuilder::CreateBlock(
    const std::vector<int>& line_indices,
    const std::vector<LineItem>& lines,
    const std::vector<WordItem>& words,
    const std::vector<GlyphItem>& glyphs,
    int block_id) {
  textblock::TextBlock block;
  block.id = block_id;
  block.type = textblock::TextBlockType::kParagraph;
  block.table_id = -1;
  block.row = -1;
  block.col = -1;
  block.contains_type3 = false;
  block.has_baseline = false;

  if (line_indices.empty()) {
    return block;
  }

  // Compute bounds
  block.ink_bounds = lines[line_indices[0]].bbox;
  for (size_t i = 1; i < line_indices.size(); ++i) {
    block.ink_bounds =
        UnionRects(block.ink_bounds, lines[line_indices[i]].bbox);
  }
  block.layout_bounds = block.ink_bounds;

  // Compute baseline (from first line)
  block.baseline_y = lines[line_indices[0]].baseline_y;
  block.has_baseline = true;

  // Compute approximate font size
  std::vector<float> heights;
  for (int li : line_indices) {
    heights.push_back(lines[li].line_height);
  }
  std::nth_element(heights.begin(), heights.begin() + heights.size() / 2,
                   heights.end());
  block.approx_font_size = heights[heights.size() / 2];

  // Build spans and cached text
  WideString text;
  std::vector<int> line_break_indices;

  for (size_t line_idx = 0; line_idx < line_indices.size(); ++line_idx) {
    int li = line_indices[line_idx];
    const LineItem& line = lines[li];

    // Sort words in line by X
    std::vector<int> sorted_words = line.word_indices;
    std::stable_sort(sorted_words.begin(), sorted_words.end(),
                     [&](int a, int b) {
                       return RectLeft(words[a].bbox) < RectLeft(words[b].bbox);
                     });

    for (size_t wi_idx = 0; wi_idx < sorted_words.size(); ++wi_idx) {
      int wi = sorted_words[wi_idx];
      const WordItem& word = words[wi];

      if (word.is_empty()) {
        continue;
      }

      // Create span for this word
      textblock::TextSpanRef span;
      span.char_index_start = word.glyph_indices.empty()
                                  ? -1
                                  : glyphs[word.glyph_indices[0]].char_index;
      span.char_count = static_cast<int>(word.glyph_indices.size());
      span.ink_bounds = word.bbox;
      span.is_type3 = false;

      // Get object ID (from first glyph)
      if (!word.glyph_indices.empty()) {
        span.object_id = glyphs[word.glyph_indices[0]].object_id;
        if (glyphs[word.glyph_indices[0]].is_type3) {
          span.is_type3 = true;
          block.contains_type3 = true;
        }
      } else {
        span.object_id = kInvalidObjectId;
      }

      block.spans.push_back(span);

      // Add word text
      text += word.text;

      // Add space between words (except last)
      if (wi_idx < sorted_words.size() - 1) {
        text += L' ';
      }
    }

    // Record line break position (except after last line)
    if (line_idx < line_indices.size() - 1) {
      line_break_indices.push_back(static_cast<int>(text.GetLength()));
      text += L'\n';
    }
  }

  block.cached_text = text;
  block.line_break_indices = line_break_indices;

  return block;
}

int BlockBuilder::Build(const std::vector<LineItem>& lines,
                         const std::vector<WordItem>& words,
                         const std::vector<GlyphItem>& glyphs,
                         const ColumnModel& columns,
                         const std::vector<Table>& tables,
                         const AdaptiveParams& params) {
  blocks_.clear();
  merge_log_.clear();

  if (lines.empty()) {
    return 0;
  }

  // Make a mutable copy of lines to assign column IDs
  std::vector<LineItem> mutable_lines = lines;
  AssignLineColumns(mutable_lines, columns);

  // Compute page left for gutter calculations
  page_left_ = 1e9f;
  for (const auto& col : columns.column_bounds) {
    page_left_ = std::min(page_left_, RectLeft(col));
  }

  // Build spatial grid for lines
  CFX_FloatRect bounds;
  bounds.left = bounds.bottom = 1e9f;
  bounds.right = bounds.top = -1e9f;
  for (const auto& line : mutable_lines) {
    if (!line.is_empty()) {
      bounds.left = std::min(bounds.left, RectLeft(line.bbox));
      bounds.bottom = std::min(bounds.bottom, RectBottom(line.bbox));
      bounds.right = std::max(bounds.right, RectRight(line.bbox));
      bounds.top = std::max(bounds.top, RectTop(line.bbox));
    }
  }

  if (bounds.left >= bounds.right || bounds.bottom >= bounds.top) {
    return 0;
  }

  SpatialGrid grid(bounds, params.grid_cell_size);
  for (size_t i = 0; i < mutable_lines.size(); ++i) {
    if (!mutable_lines[i].is_empty()) {
      grid.Insert(static_cast<int>(i), mutable_lines[i].bbox);
    }
  }

  // Build neighbor graph
  BuildNeighborGraph(mutable_lines, columns, tables, params, grid);

  // Cluster into blocks
  ClusterIntoBlocks(mutable_lines, words, glyphs, params);

  return static_cast<int>(blocks_.size());
}

}  // namespace layout
}  // namespace pdfium
