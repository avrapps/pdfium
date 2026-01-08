// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFTEXT_CPDF_TEXTBLOCK_DETECTOR_H_
#define CORE_FPDFTEXT_CPDF_TEXTBLOCK_DETECTOR_H_

#include <stdint.h>

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <vector>

#include "core/fpdfapi/render/cpdf_renderobjectfilter.h"
#include "core/fpdftext/cpdf_layout_types.h"
#include "core/fxcrt/fx_coordinates.h"
#include "core/fxcrt/widestring.h"

class CPDF_Page;
class CPDF_TextPage;

// Import ObjectId and ObjectTable from render namespace
using pdfium::render::ObjectId;
using pdfium::render::kInvalidObjectId;
using pdfium::render::ObjectTable;

namespace pdfium {
namespace textblock {

// Type of detected text block
enum class TextBlockType : int {
  kParagraph = 0,
  kTableCell = 1,  // Reserved for Phase 1C
};

// Why a character was excluded from blocks (useful for debugging)
enum class TextBlockExclusionReason : int {
  kNone = 0,
  kRTLOrComplexScript,
  kRotatedOrSkewed,
  kClippedOrMasked,
  kDecorativeOrTooSmall,
};

// Reference to a span of text within a page object.
// Uses ObjectId for stable addressing across nested Form XObjects.
struct TextSpanRef {
  ObjectId object_id;      // Flat ID from ObjectTable (supports nested forms)
  int char_index_start;    // CPDF_TextPage character index
  int char_count;
  CFX_FloatRect ink_bounds;  // Tight union of glyph/char boxes
  bool is_type3;           // True if this span uses a Type3 font

  TextSpanRef();
  TextSpanRef(ObjectId obj_id, int char_start, int count,
              const CFX_FloatRect& bounds, bool type3 = false);
  TextSpanRef(const TextSpanRef& other);
  ~TextSpanRef();
};

// A logical unit of editable text (paragraph or table cell)
struct TextBlock {
  int32_t id;             // Stable within a detection snapshot
  TextBlockType type;
  CFX_FloatRect ink_bounds;     // Tight bounding box (left, bottom, right, top)
  CFX_FloatRect layout_bounds;  // Expanded by line metrics (for future reflow)
  std::vector<TextSpanRef> spans;

  // Metadata for rendering/UI
  float approx_font_size;
  float baseline_y;       // Primary baseline (NaN if not set)
  bool has_baseline;      // True if baseline_y is valid

  // Type3 font support
  // Phase 1: Type3 blocks are fully editable for detection + rendering
  // Phase 3: Insertions use fallback font; existing glyphs unchanged
  bool contains_type3;

  // Cached text content (avoids rebuilding CPDF_TextPage)
  WideString cached_text;

  // Line break positions within cached_text (for multi-line blocks)
  std::vector<int> line_break_indices;

  // Table-specific (Phase 1C, -1 if not table)
  int table_id;
  int row;
  int col;

  TextBlock();
  TextBlock(const TextBlock& other);
  TextBlock(TextBlock&& other) noexcept;
  TextBlock& operator=(const TextBlock& other);
  TextBlock& operator=(TextBlock&& other) noexcept;
  ~TextBlock();
};

// Cached detection results stored on page
struct TextBlockSnapshot {
  uint32_t generation;  // Increments on invalidation
  int flags_used;       // Flags passed to DetectTextBlocks (cache key)
  std::vector<TextBlock> blocks;

  // ObjectTable for fast pointer->id lookup and subset rendering
  ObjectTable object_table;

  // Precomputed for fast rendering using ObjectId (sorted for binary_search)
  std::vector<std::vector<ObjectId>> block_to_object_ids;  // Per-block, sorted
  std::unordered_set<ObjectId> all_block_object_ids;       // For background exclusion

  // Hierarchical layout data (from new detection pipeline)
  std::vector<layout::GlyphItem> glyphs;
  std::vector<layout::WordItem> words;
  std::vector<layout::LineItem> lines;
  std::vector<layout::Table> tables;
  layout::ColumnModel columns;
  std::vector<layout::ReadingSection> sections;

  // Page statistics and adaptive parameters
  layout::PageStats stats;
  layout::AdaptiveParams params;

  // Debug / traceability
  std::vector<layout::MergeDecision> merge_log;
  bool debug_enabled;

  // Cache key for deterministic regeneration
  uint64_t content_hash;

  TextBlockSnapshot();
  TextBlockSnapshot(const TextBlockSnapshot& other);
  TextBlockSnapshot(TextBlockSnapshot&& other) noexcept;
  TextBlockSnapshot& operator=(TextBlockSnapshot&& other) noexcept;
  ~TextBlockSnapshot();

  // Check if a given ObjectId belongs to any text block
  bool IsObjectInAnyBlock(ObjectId object_id) const;

  // Check if a given ObjectId belongs to a specific block (uses binary_search)
  bool IsObjectInBlock(int block_index, ObjectId object_id) const;
};

// Detection flags (matches EPDF_TEXTBLOCK_* in fpdf_contentediting.h)
constexpr int kTextBlockDefault = 0x00;
constexpr int kTextBlockDetectTables = 0x01;        // Detect ruled tables
constexpr int kTextBlockStrictExclusions = 0x02;    // More aggressive filtering
constexpr int kTextBlockDetectUnruledTables = 0x04; // Detect unruled tables (opt-in)
constexpr int kTextBlockUseNewPipeline = 0x08;      // Use new hierarchical pipeline
constexpr int kTextBlockEnableDebug = 0x10;         // Enable debug logging

// Main detector class
class CPDF_TextBlockDetector {
 public:
  CPDF_TextBlockDetector();
  ~CPDF_TextBlockDetector();

  // Run detection on a page. Returns a snapshot of detected blocks.
  // The snapshot is owned by the caller.
  std::unique_ptr<TextBlockSnapshot> Detect(CPDF_Page* page,
                                            CPDF_TextPage* text_page,
                                            int flags);

 private:
  // New hierarchical detection pipeline
  std::unique_ptr<TextBlockSnapshot> DetectWithNewPipeline(
      CPDF_Page* page,
      CPDF_TextPage* text_page,
      int flags,
      std::unique_ptr<TextBlockSnapshot> snapshot);

  // Precompute object ID mappings for rendering
  void PrecomputeObjectMappings(TextBlockSnapshot* snapshot) const;
  // Internal line representation during detection
  struct Line {
    int start_char_index;
    int end_char_index;  // Exclusive
    CFX_FloatRect bounds;
    float avg_font_size;
    float max_font_size;  // For stable y_tolerance during line building
    float baseline_y;
    bool excluded;
    TextBlockExclusionReason exclusion_reason;
  };

  // Internal paragraph representation during detection
  struct Paragraph {
    std::vector<int> line_indices;  // Indices into lines_ vector
    CFX_FloatRect bounds;
    float left_margin;
    float right_margin;
  };

  // Step 0: Filter out characters that should be excluded
  bool ShouldExcludeChar(CPDF_TextPage* text_page,
                         int char_index,
                         int flags,
                         TextBlockExclusionReason* out_reason) const;

  // Step 1: Group characters into lines
  std::vector<Line> BuildLines(CPDF_TextPage* text_page, int flags) const;

  // Step 2: Group lines into paragraphs
  std::vector<Paragraph> GroupLinesIntoParagraphs(
      const std::vector<Line>& lines) const;

  // Step 3: Convert paragraphs to TextBlocks
  void EmitTextBlocks(CPDF_Page* page,
                      CPDF_TextPage* text_page,
                      const std::vector<Line>& lines,
                      const std::vector<Paragraph>& paragraphs,
                      TextBlockSnapshot* snapshot) const;

  // Helper: Check if text has significant rotation/skew
  bool HasSignificantRotation(const CFX_Matrix& matrix,
                              float threshold_degrees) const;

  // Helper: Check if line appears to be RTL
  bool IsLineRTL(CPDF_TextPage* text_page,
                 int start_index,
                 int end_index) const;

  // Helper: Get ObjectId for a character's text object using the ObjectTable
  ObjectId GetObjectIdForChar(const ObjectTable& obj_table,
                              CPDF_TextPage* text_page,
                              int char_index) const;

  // Helper: Check if a text object uses Type3 font
  bool IsType3TextObject(CPDF_TextPage* text_page, int char_index) const;
};

}  // namespace textblock
}  // namespace pdfium

#endif  // CORE_FPDFTEXT_CPDF_TEXTBLOCK_DETECTOR_H_
