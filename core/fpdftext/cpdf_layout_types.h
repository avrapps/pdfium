// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFTEXT_CPDF_LAYOUT_TYPES_H_
#define CORE_FPDFTEXT_CPDF_LAYOUT_TYPES_H_

#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "core/fpdfapi/render/cpdf_renderobjectfilter.h"
#include "core/fpdftext/cpdf_layout_geometry.h"
#include "core/fxcrt/fx_coordinates.h"
#include "core/fxcrt/widestring.h"

// Import ObjectId from render namespace
using pdfium::render::ObjectId;
using pdfium::render::kInvalidObjectId;

namespace pdfium {
namespace layout {

// =============================================================================
// Page Statistics
// =============================================================================

struct PageStats {
  float median_glyph_height = 12.0f;   // H
  float median_glyph_width = 6.0f;     // W
  float median_char_advance = 6.0f;    // A (typical horizontal advance)
  int total_glyphs = 0;
  int rotation_counts[4] = {0, 0, 0, 0};  // 0°, 90°, 180°, 270°
  
  // Page bounds
  CFX_FloatRect page_bounds;
  float page_width = 0.0f;
  float page_height = 0.0f;
};

// =============================================================================
// Adaptive Parameters (derived from PageStats)
// =============================================================================

struct AdaptiveParams {
  // Stored from PageStats for reference
  float median_height = 12.0f;        // H - store for scoring functions
  float median_width = 6.0f;          // W - store for scoring functions
  float median_advance = 6.0f;        // A - store for gap calculations

  // Derived thresholds
  float baseline_tol = 3.0f;          // 0.25 * H
  float line_overlap_tol = 0.5f;      // Min overlap ratio for same line
  float word_gap_threshold = 1.8f;    // 0.3 * W
  float char_margin = 12.0f;          // 2.0 * W (max gap for word chaining)
  float align_tol = 6.0f;             // 0.5 * H
  float gutter_min_width = 18.0f;     // 1.5 * H
  float grid_cell_size = 24.0f;       // 2.0 * H

  // Line clustering
  float line_merge_max_gap = 18.0f;   // 3.0 * A, clamped to <= 6 * W

  // Split-on-gap
  float line_split_gap_factor = 2.5f;  // multiplied by word_gap_threshold

  // Column detection
  float gutter_valley_ratio = 0.1f;    // valley must be < 10% of peak

  // Table detection
  float edge_snap_tol = 2.0f;
  float edge_join_tol = 3.0f;
  float intersection_tol = 3.0f;
  int min_table_cells = 4;
  int min_table_edges = 4;

  // Scoring thresholds
  float min_word_link_score = 0.3f;

  static AdaptiveParams FromStats(const PageStats& stats) {
    AdaptiveParams p;
    float H = stats.median_glyph_height;
    float W = stats.median_glyph_width;
    float A = stats.median_char_advance;

    // Store raw values
    p.median_height = H;
    p.median_width = W;
    p.median_advance = A;

    // Compute thresholds
    p.baseline_tol = 0.25f * H;
    p.line_overlap_tol = 0.5f;  // Ratio, not points
    p.word_gap_threshold = 0.3f * W;
    p.char_margin = 2.0f * W;
    p.align_tol = 0.5f * H;
    p.gutter_min_width = 1.5f * H;
    p.grid_cell_size = std::max(2.0f * H, 10.0f);  // Minimum 10pt

    // Line merge gap: based on advance, clamped
    p.line_merge_max_gap = std::min(3.0f * A, 6.0f * W);

    // Column detection
    p.gutter_valley_ratio = 0.1f;

    // Table detection
    p.edge_snap_tol = 2.0f;
    p.edge_join_tol = 3.0f;
    p.intersection_tol = 3.0f;
    p.min_table_cells = 4;
    p.min_table_edges = 4;

    return p;
  }

  // Default constructor with reasonable defaults
  static AdaptiveParams Default() {
    PageStats stats;
    stats.median_glyph_height = 12.0f;
    stats.median_glyph_width = 6.0f;
    stats.median_char_advance = 6.0f;
    return FromStats(stats);
  }
};

// =============================================================================
// GlyphItem - Atomic unit of text
// =============================================================================

struct GlyphItem {
  int char_index = -1;               // Index into CPDF_TextPage
  CFX_FloatRect bbox;                // Page coords
  float origin_x = 0.0f;             // Pen position X (for gap calculation)
  float baseline_y = 0.0f;           // From origin.y (pen position Y)
  float effective_font_size = 12.0f;  // max(char_height, raw_size * matrix_scale)
  int rotation_bucket = 0;           // 0, 90, 180, 270
  bool is_skewed = false;
  bool is_generated = false;         // Skip for layout
  wchar_t unicode = 0;
  bool is_space_like = false;        // NBSP, thin space, zero-width, etc.
  bool is_combining_mark = false;    // Combining diacritics
  bool is_punctuation = false;
  ObjectId object_id = kInvalidObjectId;
  bool is_type3 = false;

  // Helper methods
  float ink_width() const { return RectWidth(bbox); }
  float ink_height() const { return RectHeight(bbox); }
  float origin_y() const { return baseline_y; }  // Alias for clarity

  bool is_large_font(float median_h) const {
    return effective_font_size > median_h * 1.8f;
  }
};

// Space-like character detection
inline bool IsSpaceLikeChar(wchar_t c) {
  return c == L' ' || c == L'\u00A0' ||       // space, NBSP
         c == L'\u2009' || c == L'\u200A' ||  // thin space, hair space
         c == L'\u200B' || c == L'\u200C' ||  // zero-width space, ZWNJ
         c == L'\u200D' || c == L'\uFEFF' ||  // ZWJ, BOM
         c == L'\t' || c == L'\n' || c == L'\r';
}

// Combining mark detection
inline bool IsCombiningMark(wchar_t c) {
  return (c >= 0x0300 && c <= 0x036F) ||  // Combining Diacritical Marks
         (c >= 0x1AB0 && c <= 0x1AFF) ||  // Combining Diacritical Marks Extended
         (c >= 0x1DC0 && c <= 0x1DFF) ||  // Combining Diacritical Marks Supplement
         (c >= 0x20D0 && c <= 0x20FF) ||  // Combining Diacritical Marks for Symbols
         (c >= 0xFE20 && c <= 0xFE2F);    // Combining Half Marks
}

// Punctuation detection (common punctuation)
inline bool IsPunctuationChar(wchar_t c) {
  return (c >= 0x0021 && c <= 0x002F) ||  // !"#$%&'()*+,-./
         (c >= 0x003A && c <= 0x0040) ||  // :;<=>?@
         (c >= 0x005B && c <= 0x0060) ||  // [\]^_`
         (c >= 0x007B && c <= 0x007E) ||  // {|}~
         (c >= 0x2000 && c <= 0x206F);    // General punctuation
}

// =============================================================================
// WordItem - A sequence of glyphs forming a word
// =============================================================================

struct WordItem {
  std::vector<int> glyph_indices;  // Indices into glyphs vector
  CFX_FloatRect bbox;
  float baseline_y = 0.0f;         // Median of glyph baselines
  float median_height = 0.0f;
  WideString text;
  std::set<ObjectId> object_ids;   // All objects touched
  float confidence = 0.0f;         // Avg link score

  // Helper methods
  float ink_width() const { return RectWidth(bbox); }

  bool is_empty() const { return glyph_indices.empty(); }

  // Check if word has high variance in glyph Y positions (ragged)
  bool is_ragged(const std::vector<GlyphItem>& glyphs,
                 float threshold_ratio = 0.15f) const {
    if (glyph_indices.size() < 3) {
      return false;
    }
    std::vector<float> ys;
    ys.reserve(glyph_indices.size());
    for (int gi : glyph_indices) {
      ys.push_back(glyphs[gi].baseline_y);
    }
    float sum = 0.0f;
    for (float y : ys) {
      sum += y;
    }
    float mean = sum / ys.size();
    float variance = 0.0f;
    for (float y : ys) {
      variance += (y - mean) * (y - mean);
    }
    variance /= ys.size();
    return std::sqrt(variance) > median_height * threshold_ratio;
  }
};

// =============================================================================
// WordLink - Scored connection between glyphs during word building
// =============================================================================

struct WordLink {
  int from_idx = -1;
  int to_idx = -1;
  float score = 0.0f;

  // Score components (for debugging)
  float overlap_score = 0.0f;
  float baseline_score = 0.0f;
  float gap_score = 0.0f;
  float height_score = 0.0f;
};

// =============================================================================
// MicroRun - Atomic segment from stream-order grouping
// =============================================================================

struct MicroRun {
  std::vector<int> glyph_indices;  // Indices into glyphs vector
  CFX_FloatRect bbox;
  float baseline_y = 0.0f;         // Median baseline
  float median_height = 0.0f;      // Median glyph height
  ObjectId object_id = kInvalidObjectId;
  int rotation_bucket = 0;
  int column_id = 0;               // Assigned during coarse column detection

  bool is_empty() const { return glyph_indices.empty(); }

  void AddGlyph(int idx, const CFX_FloatRect& glyph_bbox, float glyph_baseline) {
    glyph_indices.push_back(idx);
    if (glyph_indices.size() == 1) {
      bbox = glyph_bbox;
      baseline_y = glyph_baseline;
    } else {
      bbox = UnionRects(bbox, glyph_bbox);
      // Update baseline as running average (will be recomputed as median later)
    }
  }

  void ComputeStats(const std::vector<GlyphItem>& glyphs) {
    if (glyph_indices.empty()) return;

    // Compute bbox
    bbox = glyphs[glyph_indices[0]].bbox;
    for (size_t i = 1; i < glyph_indices.size(); ++i) {
      bbox = UnionRects(bbox, glyphs[glyph_indices[i]].bbox);
    }

    // Compute median baseline
    std::vector<float> baselines;
    std::vector<float> heights;
    baselines.reserve(glyph_indices.size());
    heights.reserve(glyph_indices.size());
    for (int gi : glyph_indices) {
      baselines.push_back(glyphs[gi].baseline_y);
      heights.push_back(glyphs[gi].ink_height());
    }
    std::nth_element(baselines.begin(), baselines.begin() + baselines.size() / 2,
                     baselines.end());
    baseline_y = baselines[baselines.size() / 2];

    std::nth_element(heights.begin(), heights.begin() + heights.size() / 2,
                     heights.end());
    median_height = heights[heights.size() / 2];

    // Get object_id and rotation from first glyph
    object_id = glyphs[glyph_indices[0]].object_id;
    rotation_bucket = glyphs[glyph_indices[0]].rotation_bucket;
  }
};

// =============================================================================
// LineItem - A horizontal sequence of words
// =============================================================================

struct LineItem {
  std::vector<int> word_indices;   // Indices into words vector (legacy)
  std::vector<int> glyph_indices;  // Indices into glyphs vector (new pipeline)
  CFX_FloatRect bbox;
  float baseline_y = 0.0f;         // Median of word/glyph baselines
  float line_height = 0.0f;        // Median word/glyph height
  int column_id = -1;              // Assigned in column detection phase
  std::set<ObjectId> object_ids;
  float rtl_ratio = 0.0f;          // Ratio of RTL characters for script detection
  bool is_ragged = false;          // High variance in word positions

  // Optional baseline regression
  bool has_sloped_baseline = false;
  float baseline_slope = 0.0f;
  float baseline_intercept = 0.0f;

  // Suspicious gap annotation (for deferred column splitting)
  bool has_suspicious_gap = false;
  float suspicious_gap_x = 0.0f;
  float suspicious_gap_size = 0.0f;

  // Computed from word content
  float ink_width() const { return RectWidth(bbox); }

  bool is_empty() const { return word_indices.empty() && glyph_indices.empty(); }
};

// =============================================================================
// LineWeight - For histogram weighting during column detection
// =============================================================================

struct LineWeight {
  int line_idx = -1;
  float weight = 0.0f;
  bool excluded = false;
  std::string exclusion_reason;
};

// =============================================================================
// Column Model
// =============================================================================

struct ColumnModel {
  std::vector<float> gutter_positions;        // X positions of gutters
  std::vector<CFX_FloatRect> column_bounds;   // Sorted by X position
  bool is_valid = false;
  float confidence = 0.0f;

  int column_count() const {
    return static_cast<int>(column_bounds.size());
  }

  bool is_single_column() const { return column_bounds.size() <= 1; }
};

// =============================================================================
// Coarse Column Model (lightweight, for line builder gating)
// =============================================================================

struct CoarseColumnModel {
  std::vector<float> gutter_positions;  // Page X coordinates
  int num_columns = 1;
  bool is_valid = false;

  int GetColumnForX(float x) const {
    if (!is_valid || gutter_positions.empty()) {
      return 0;
    }
    int col = 0;
    for (float g : gutter_positions) {
      if (x > g) {
        col++;
      }
    }
    return col;
  }

  bool CrossesGutter(float x1, float x2, float tolerance) const {
    if (!is_valid || gutter_positions.empty()) {
      return false;
    }
    float left = std::min(x1, x2) + tolerance;
    float right = std::max(x1, x2) - tolerance;
    if (left >= right) {
      return false;  // Gap too small to cross anything
    }
    for (float g : gutter_positions) {
      if (g > left && g < right) {
        return true;
      }
    }
    return false;
  }
};

// =============================================================================
// Provisional Table Zone (early detection before full table analysis)
// =============================================================================

struct ProvisionalTableZone {
  CFX_FloatRect bbox;
  float confidence = 0.0f;
  int h_edge_count = 0;
  int v_edge_count = 0;
  int intersection_count = 0;
};

// =============================================================================
// Ruling Edge - For table detection
// =============================================================================

struct RulingEdge {
  CFX_FloatRect bbox;
  char orientation = '\0';  // 'h' or 'v'
  float thickness = 1.0f;

  bool is_horizontal() const { return orientation == 'h'; }
  bool is_vertical() const { return orientation == 'v'; }

  float length() const {
    return is_horizontal() ? RectWidth(bbox) : RectHeight(bbox);
  }

  // Get the primary coordinate (Y for horizontal, X for vertical)
  float primary_coord() const {
    return is_horizontal() ? RectCenterY(bbox) : RectCenterX(bbox);
  }
};

// =============================================================================
// Quantized Point - For stable intersection detection
// =============================================================================

struct QuantizedPoint {
  int qx = 0;
  int qy = 0;

  static QuantizedPoint FromFloat(float x, float y, float tol) {
    QuantizedPoint p;
    p.qx = static_cast<int>(std::round(x / tol));
    p.qy = static_cast<int>(std::round(y / tol));
    return p;
  }

  float ToFloatX(float tol) const { return qx * tol; }
  float ToFloatY(float tol) const { return qy * tol; }

  bool operator==(const QuantizedPoint& o) const {
    return qx == o.qx && qy == o.qy;
  }

  bool operator<(const QuantizedPoint& o) const {
    if (qx != o.qx)
      return qx < o.qx;
    return qy < o.qy;
  }
};

struct QuantizedPointHash {
  size_t operator()(const QuantizedPoint& p) const {
    return std::hash<int>()(p.qx) ^ (std::hash<int>()(p.qy) << 16);
  }
};

// =============================================================================
// Table Cell
// =============================================================================

struct TableCell {
  CFX_FloatRect bbox;
  int row = -1;
  int col = -1;
  std::vector<int> word_indices;   // Words contained in this cell
  std::vector<int> line_indices;   // Lines contained in this cell
};

// =============================================================================
// Table
// =============================================================================

struct Table {
  int id = -1;
  CFX_FloatRect bbox;
  std::vector<TableCell> cells;
  int row_count = 0;
  int col_count = 0;

  bool is_empty() const { return cells.empty(); }
};

// =============================================================================
// Reading Section - For reading order computation
// =============================================================================

struct ReadingSection {
  int start_block_idx = 0;
  int end_block_idx = 0;
  uint32_t column_signature = 0;  // Bitset of present columns
  bool is_spanning = false;
};

// =============================================================================
// Merge Decision - For debugging/traceability
// =============================================================================

struct MergeDecision {
  enum class Type {
    kGlyphToWord,
    kGlyphToLine,  // NEW: for line-first pipeline
    kWordToLine,
    kLineToBlock,
  };

  Type type = Type::kGlyphToWord;
  int from_idx = -1;
  int to_idx = -1;
  float score = 0.0f;
  std::string reason;
  bool accepted = false;
};

}  // namespace layout
}  // namespace pdfium

#endif  // CORE_FPDFTEXT_CPDF_LAYOUT_TYPES_H_
