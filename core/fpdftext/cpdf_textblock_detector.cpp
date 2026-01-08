// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdftext/cpdf_textblock_detector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/page/cpdf_pageobject.h"
#include "core/fpdfapi/page/cpdf_textobject.h"
#include "core/fpdftext/cpdf_block_builder.h"
#include "core/fpdftext/cpdf_column_detector.h"
#include "core/fpdftext/cpdf_glyph_extractor.h"
#include "core/fpdftext/cpdf_line_builder.h"
#include "core/fpdftext/cpdf_reading_order.h"
#include "core/fpdftext/cpdf_table_detector.h"
#include "core/fpdftext/cpdf_textpage.h"
#include "core/fpdftext/cpdf_word_builder.h"
#include "core/fxcrt/fx_unicode.h"

namespace pdfium {
namespace textblock {

namespace {

// Thresholds for detection
constexpr float kRotationThresholdDegrees = 5.0f;
constexpr float kSkewThresholdRatio = 0.1f;  // Shear magnitude threshold
constexpr float kMinFontSizeThreshold = 4.0f;  // Decorative text threshold
constexpr float kLineYProximityFactor = 0.5f;  // Y proximity as fraction of font size
constexpr float kParagraphGapFactor = 1.5f;    // Gap threshold as factor of line height
constexpr float kLeftMarginTolerance = 5.0f;   // Tolerance for left edge alignment

// Check if a Unicode character is in RTL script range
// Note: This is a simple heuristic checking strong RTL codepoint ranges.
// It does not perform full bidi analysis.
bool IsRTLCharacter(wchar_t unicode) {
  // Arabic: U+0600 - U+06FF, U+0750 - U+077F, U+08A0 - U+08FF
  // Hebrew: U+0590 - U+05FF
  // Syriac: U+0700 - U+074F
  // Thaana: U+0780 - U+07BF
  if ((unicode >= 0x0590 && unicode <= 0x05FF) ||  // Hebrew
      (unicode >= 0x0600 && unicode <= 0x06FF) ||  // Arabic
      (unicode >= 0x0700 && unicode <= 0x074F) ||  // Syriac
      (unicode >= 0x0750 && unicode <= 0x077F) ||  // Arabic Supplement
      (unicode >= 0x0780 && unicode <= 0x07BF) ||  // Thaana
      (unicode >= 0x08A0 && unicode <= 0x08FF) ||  // Arabic Extended-A
      (unicode >= 0xFB00 && unicode <= 0xFDFF) ||  // Hebrew/Arabic presentation forms
      (unicode >= 0xFE70 && unicode <= 0xFEFF)) {  // Arabic presentation forms
    return true;
  }
  return false;
}

// Get rotation angle from matrix in degrees
float GetRotationDegrees(const CFX_Matrix& matrix) {
  // For a 2D affine transform matrix [a, b, c, d, e, f]:
  // a = scale_x * cos(rotation)
  // b = scale_x * sin(rotation)
  // c = -scale_y * sin(rotation)
  // d = scale_y * cos(rotation)
  float angle_rad = std::atan2(matrix.b, matrix.a);
  return std::abs(angle_rad * 180.0f / 3.14159265358979f);
}

// Check if matrix has significant skew/shear
bool HasSignificantSkew(const CFX_Matrix& matrix) {
  // For a pure rotation+scale, we'd have:
  //   a*d - b*c = scale_x * scale_y (determinant)
  //   and a*c + b*d = 0
  // Skew breaks this relationship.
  // Simple check: compare shear components against scale
  float scale_x = std::sqrt(matrix.a * matrix.a + matrix.b * matrix.b);
  float scale_y = std::sqrt(matrix.c * matrix.c + matrix.d * matrix.d);
  if (scale_x < 0.001f || scale_y < 0.001f) {
    return true;  // Degenerate matrix
  }
  // Check if c/d ratio differs significantly from -b/a ratio (indicates skew)
  // For pure rotation: b/a = -c/d = tan(angle)
  float shear_indicator = std::abs(matrix.a * matrix.c + matrix.b * matrix.d);
  float magnitude = scale_x * scale_y;
  return (shear_indicator / magnitude) > kSkewThresholdRatio;
}

}  // namespace

// TextSpanRef implementation
TextSpanRef::TextSpanRef()
    : object_id(kInvalidObjectId),
      char_index_start(0),
      char_count(0),
      is_type3(false) {}

TextSpanRef::TextSpanRef(ObjectId obj_id,
                         int char_start,
                         int count,
                         const CFX_FloatRect& bounds,
                         bool type3)
    : object_id(obj_id),
      char_index_start(char_start),
      char_count(count),
      ink_bounds(bounds),
      is_type3(type3) {}

TextSpanRef::TextSpanRef(const TextSpanRef& other) = default;

TextSpanRef::~TextSpanRef() = default;

// TextBlock implementation
TextBlock::TextBlock()
    : id(-1),
      type(TextBlockType::kParagraph),
      approx_font_size(0.0f),
      baseline_y(0.0f),
      has_baseline(false),
      contains_type3(false),
      table_id(-1),
      row(-1),
      col(-1) {}

TextBlock::TextBlock(const TextBlock& other) = default;
TextBlock::TextBlock(TextBlock&& other) noexcept = default;
TextBlock& TextBlock::operator=(const TextBlock& other) = default;
TextBlock& TextBlock::operator=(TextBlock&& other) noexcept = default;
TextBlock::~TextBlock() = default;

// TextBlockSnapshot implementation
TextBlockSnapshot::TextBlockSnapshot()
    : generation(0),
      flags_used(0),
      debug_enabled(false),
      content_hash(0) {}

TextBlockSnapshot::TextBlockSnapshot(const TextBlockSnapshot& other) = default;
TextBlockSnapshot::TextBlockSnapshot(TextBlockSnapshot&& other) noexcept =
    default;
TextBlockSnapshot& TextBlockSnapshot::operator=(
    TextBlockSnapshot&& other) noexcept = default;
TextBlockSnapshot::~TextBlockSnapshot() = default;

bool TextBlockSnapshot::IsObjectInAnyBlock(ObjectId object_id) const {
  if (object_id == kInvalidObjectId) {
    return false;
  }
  return all_block_object_ids.find(object_id) != all_block_object_ids.end();
}

bool TextBlockSnapshot::IsObjectInBlock(int block_index,
                                        ObjectId object_id) const {
  if (block_index < 0 ||
      static_cast<size_t>(block_index) >= block_to_object_ids.size()) {
    return false;
  }
  if (object_id == kInvalidObjectId) {
    return false;
  }
  const auto& ids = block_to_object_ids[block_index];
  return std::binary_search(ids.begin(), ids.end(), object_id);
}

// CPDF_TextBlockDetector implementation
CPDF_TextBlockDetector::CPDF_TextBlockDetector() = default;

CPDF_TextBlockDetector::~CPDF_TextBlockDetector() = default;

std::unique_ptr<TextBlockSnapshot> CPDF_TextBlockDetector::Detect(
    CPDF_Page* page,
    CPDF_TextPage* text_page,
    int flags) {
  auto snapshot = std::make_unique<TextBlockSnapshot>();
  snapshot->flags_used = flags;
  snapshot->generation = 1;
  snapshot->debug_enabled = (flags & kTextBlockEnableDebug) != 0;

  if (!page || !text_page || text_page->size() == 0) {
    return snapshot;
  }

  // Step 0: Build ObjectTable for the entire page object tree.
  // This recursively traverses Form XObjects and assigns flat ObjectIds.
  snapshot->object_table.Build(page);

  // Check if we should use the new hierarchical pipeline
  if (flags & kTextBlockUseNewPipeline) {
    return DetectWithNewPipeline(page, text_page, flags, std::move(snapshot));
  }

  // Legacy pipeline
  // Step 1: Build lines from characters
  std::vector<Line> lines = BuildLines(text_page, flags);
  if (lines.empty()) {
    return snapshot;
  }

  // Step 2: Group lines into paragraphs
  std::vector<Paragraph> paragraphs = GroupLinesIntoParagraphs(lines);
  if (paragraphs.empty()) {
    return snapshot;
  }

  // Step 3: Emit TextBlocks using ObjectId from the table
  EmitTextBlocks(page, text_page, lines, paragraphs, snapshot.get());

  return snapshot;
}

std::unique_ptr<TextBlockSnapshot> CPDF_TextBlockDetector::DetectWithNewPipeline(
    CPDF_Page* page,
    CPDF_TextPage* text_page,
    int flags,
    std::unique_ptr<TextBlockSnapshot> snapshot) {
  bool debug_enabled = snapshot->debug_enabled;

  // Phase 0: Extract glyphs and compute page statistics
  layout::GlyphExtractor glyph_extractor;
  glyph_extractor.Extract(page, text_page, snapshot->object_table);

  snapshot->glyphs = glyph_extractor.GetGlyphs();
  snapshot->stats = glyph_extractor.GetStats();
  snapshot->params = glyph_extractor.GetAdaptiveParams();

  if (snapshot->glyphs.empty()) {
    return snapshot;
  }

  // Phase 0.5: Extract provisional table zones
  layout::TableDetector table_detector;
  auto provisional_zones =
      table_detector.ExtractProvisionalZones(page, snapshot->params);

  // Phase 1: Build LINES from glyphs (LINE-FIRST approach)
  // This is the correct order: once glyphs are in lines, word segmentation
  // becomes a trivial 1D problem.
  layout::LineBuilder line_builder;
  line_builder.SetDebugEnabled(debug_enabled);
  line_builder.BuildFromGlyphs(snapshot->glyphs, snapshot->params);
  snapshot->lines = line_builder.GetLines();

  if (debug_enabled) {
    for (const auto& decision : line_builder.GetMergeLog()) {
      snapshot->merge_log.push_back(decision);
    }
  }

  if (snapshot->lines.empty()) {
    return snapshot;
  }

  // Phase 2: Build WORDS from lines (1D gap-split inside each line)
  layout::WordBuilder word_builder;
  word_builder.SetDebugEnabled(debug_enabled);
  word_builder.BuildFromLines(snapshot->lines, snapshot->glyphs, snapshot->params);
  snapshot->words = word_builder.GetWords();

  if (debug_enabled) {
    for (const auto& decision : word_builder.GetMergeLog()) {
      snapshot->merge_log.push_back(decision);
    }
  }

  if (snapshot->words.empty()) {
    return snapshot;
  }

  // Phase 3: Detect columns
  layout::ColumnDetector column_detector;
  snapshot->columns = column_detector.Detect(
      snapshot->lines, snapshot->words, provisional_zones,
      snapshot->stats, snapshot->params);

  // Phase 4: Detect tables
  if (flags & kTextBlockDetectTables) {
    snapshot->tables = table_detector.DetectRuledTables(page, snapshot->params);
  }

  if (flags & kTextBlockDetectUnruledTables) {
    auto unruled = table_detector.DetectUnruledTables(
        snapshot->words, snapshot->lines, snapshot->columns, snapshot->params);
    for (auto& t : unruled) {
      t.id = static_cast<int>(snapshot->tables.size());
      snapshot->tables.push_back(std::move(t));
    }
  }

  // Phase 5: Build blocks from lines
  layout::BlockBuilder block_builder;
  block_builder.SetDebugEnabled(debug_enabled);
  block_builder.Build(snapshot->lines, snapshot->words, snapshot->glyphs,
                      snapshot->columns, snapshot->tables, snapshot->params);

  if (debug_enabled) {
    for (const auto& decision : block_builder.GetMergeLog()) {
      snapshot->merge_log.push_back(decision);
    }
  }

  snapshot->blocks = block_builder.GetBlocks();

  // Phase 6: Compute reading order
  layout::ReadingOrderComputer reading_order;
  reading_order.ComputeReadingOrder(snapshot->blocks, snapshot->columns);
  snapshot->sections = reading_order.GetSections();

  // Precompute object ID mappings for rendering
  PrecomputeObjectMappings(snapshot.get());

  return snapshot;
}

void CPDF_TextBlockDetector::PrecomputeObjectMappings(
    TextBlockSnapshot* snapshot) const {
  snapshot->block_to_object_ids.clear();
  snapshot->block_to_object_ids.resize(snapshot->blocks.size());
  snapshot->all_block_object_ids.clear();

  for (size_t block_idx = 0; block_idx < snapshot->blocks.size(); ++block_idx) {
    const TextBlock& block = snapshot->blocks[block_idx];
    std::vector<ObjectId>& object_ids = snapshot->block_to_object_ids[block_idx];

    for (const TextSpanRef& span : block.spans) {
      if (span.object_id != kInvalidObjectId) {
        object_ids.push_back(span.object_id);
        snapshot->all_block_object_ids.insert(span.object_id);
      }
    }

    // Sort for binary search
    std::sort(object_ids.begin(), object_ids.end());
    // Remove duplicates
    object_ids.erase(std::unique(object_ids.begin(), object_ids.end()),
                     object_ids.end());
  }
}

bool CPDF_TextBlockDetector::ShouldExcludeChar(
    CPDF_TextPage* text_page,
    int char_index,
    int flags,
    TextBlockExclusionReason* out_reason) const {
  if (char_index < 0 || static_cast<size_t>(char_index) >= text_page->size()) {
    *out_reason = TextBlockExclusionReason::kNone;
    return true;
  }

  const CPDF_TextPage::CharInfo& char_info = text_page->GetCharInfo(char_index);

  // Skip generated characters (spaces, line breaks added by text page)
  // These are NOT line breaks for our purposes - just skip them
  if (char_info.char_type() == CPDF_TextPage::CharType::kGenerated) {
    *out_reason = TextBlockExclusionReason::kNone;
    return true;
  }

  // Check for rotation/skew
  const CFX_Matrix& matrix = char_info.matrix();
  float rotation = GetRotationDegrees(matrix);
  if (rotation > kRotationThresholdDegrees || HasSignificantSkew(matrix)) {
    *out_reason = TextBlockExclusionReason::kRotatedOrSkewed;
    return true;
  }

  // Check for RTL character
  wchar_t unicode = char_info.unicode();
  if (IsRTLCharacter(unicode)) {
    bool strict = (flags & kTextBlockStrictExclusions) != 0;
    if (strict) {
      *out_reason = TextBlockExclusionReason::kRTLOrComplexScript;
      return true;
    }
    // In non-strict mode, we'll check at line level later
  }

  // Check for decorative/too small font
  // NOTE: Many PDFs use font size 1 with a text matrix that scales up (e.g., Tm
  // with large a/d values). We must compute the EFFECTIVE font size by
  // multiplying the raw font size by the matrix scale factor.
  float font_size = text_page->GetCharFontSize(char_index);
  // Compute scale factor from the text matrix (Y scale = sqrt(c² + d²))
  float scale_y = std::sqrt(matrix.c * matrix.c + matrix.d * matrix.d);
  float effective_font_size = font_size * scale_y;

  // Also check char box height as a fallback - this is already in page coords
  const CFX_FloatRect& char_box = char_info.char_box();
  float char_height = char_box.top - char_box.bottom;

  // Use the larger of effective font size or char box height
  float actual_size = std::max(effective_font_size, char_height);

  if (actual_size < kMinFontSizeThreshold) {
    *out_reason = TextBlockExclusionReason::kDecorativeOrTooSmall;
    return true;
  }

  *out_reason = TextBlockExclusionReason::kNone;
  return false;
}

std::vector<CPDF_TextBlockDetector::Line>
CPDF_TextBlockDetector::BuildLines(CPDF_TextPage* text_page, int flags) const {
  std::vector<Line> lines;
  int char_count = static_cast<int>(text_page->size());
  if (char_count == 0) {
    return lines;
  }

  Line current_line;
  current_line.start_char_index = -1;
  current_line.end_char_index = -1;
  current_line.avg_font_size = 0.0f;
  current_line.max_font_size = 0.0f;
  current_line.baseline_y = 0.0f;
  current_line.excluded = false;
  current_line.exclusion_reason = TextBlockExclusionReason::kNone;

  float accumulated_font_size = 0.0f;
  int font_size_count = 0;
  int rtl_char_count = 0;
  int total_char_count = 0;

  auto finalize_current_line = [&]() {
    if (current_line.start_char_index < 0) {
      return;  // No line to finalize
    }

    // Check if line is predominantly RTL
    if (total_char_count > 0 &&
        static_cast<float>(rtl_char_count) / total_char_count > 0.5f) {
      current_line.excluded = true;
      current_line.exclusion_reason =
          TextBlockExclusionReason::kRTLOrComplexScript;
    }

    if (!current_line.excluded &&
        current_line.end_char_index > current_line.start_char_index) {
      current_line.avg_font_size =
          font_size_count > 0 ? accumulated_font_size / font_size_count : 12.0f;
      lines.push_back(current_line);
    }

    // Reset for next line
    current_line.start_char_index = -1;
    current_line.end_char_index = -1;
    current_line.bounds = CFX_FloatRect();
    current_line.max_font_size = 0.0f;
    accumulated_font_size = 0.0f;
    font_size_count = 0;
    rtl_char_count = 0;
    total_char_count = 0;
  };

  for (int i = 0; i < char_count; ++i) {
    TextBlockExclusionReason reason;
    if (ShouldExcludeChar(text_page, i, flags, &reason)) {
      // FIX #1: Don't treat excluded chars as line breaks!
      // Just skip them - they don't break the line
      continue;
    }

    const CPDF_TextPage::CharInfo& char_info = text_page->GetCharInfo(i);
    const CFX_FloatRect& char_box = char_info.char_box();
    float char_y = char_info.origin().y;
    wchar_t unicode = char_info.unicode();

    // Compute effective font size (raw size * matrix scale)
    // Many PDFs use font size 1 with scaling in the text matrix
    float raw_font_size = text_page->GetCharFontSize(i);
    const CFX_Matrix& matrix = char_info.matrix();
    float scale_y = std::sqrt(matrix.c * matrix.c + matrix.d * matrix.d);
    float effective_font_size = raw_font_size * scale_y;
    // Also use char box height as fallback
    float char_height = char_box.top - char_box.bottom;
    float font_size = std::max(effective_font_size, char_height);

    // Track RTL characters for line-level detection
    if (IsRTLCharacter(unicode)) {
      ++rtl_char_count;
    }
    ++total_char_count;

    // Check if this character belongs to the current line (Y proximity)
    bool same_line = false;
    if (current_line.start_char_index >= 0) {
      // FIX #2: Use max_font_size which is tracked incrementally
      float y_tolerance = current_line.max_font_size * kLineYProximityFactor;
      if (y_tolerance < 1.0f) {
        y_tolerance = font_size * kLineYProximityFactor;
      }
      same_line = std::abs(char_y - current_line.baseline_y) <= y_tolerance;
    }

    if (!same_line && current_line.start_char_index >= 0) {
      // Finalize current line - this is a real line break (Y proximity failed)
      finalize_current_line();

      // Start tracking for new line with current char
      total_char_count = 1;
      if (IsRTLCharacter(unicode)) {
        rtl_char_count = 1;
      } else {
        rtl_char_count = 0;
      }
    }

    // Add character to current line
    if (current_line.start_char_index < 0) {
      current_line.start_char_index = i;
      current_line.baseline_y = char_y;
      current_line.bounds = char_box;
      current_line.max_font_size = font_size;
      current_line.excluded = false;
      current_line.exclusion_reason = TextBlockExclusionReason::kNone;
    } else {
      // Union bounds
      current_line.bounds.left =
          std::min(current_line.bounds.left, char_box.left);
      current_line.bounds.bottom =
          std::min(current_line.bounds.bottom, char_box.bottom);
      current_line.bounds.right =
          std::max(current_line.bounds.right, char_box.right);
      current_line.bounds.top = std::max(current_line.bounds.top, char_box.top);
      // Track max font size for stable y_tolerance
      current_line.max_font_size =
          std::max(current_line.max_font_size, font_size);
    }
    current_line.end_char_index = i + 1;
    accumulated_font_size += font_size;
    ++font_size_count;
  }

  // Finalize last line
  finalize_current_line();

  return lines;
}

std::vector<CPDF_TextBlockDetector::Paragraph>
CPDF_TextBlockDetector::GroupLinesIntoParagraphs(
    const std::vector<Line>& lines) const {
  std::vector<Paragraph> paragraphs;
  if (lines.empty()) {
    return paragraphs;
  }

  // Sort lines by Y position (top to bottom in page space = descending Y)
  std::vector<size_t> sorted_indices(lines.size());
  for (size_t i = 0; i < lines.size(); ++i) {
    sorted_indices[i] = i;
  }
  std::sort(sorted_indices.begin(), sorted_indices.end(),
            [&lines](size_t a, size_t b) {
              return lines[a].bounds.top > lines[b].bounds.top;
            });

  Paragraph current_para;
  current_para.left_margin = lines[sorted_indices[0]].bounds.left;
  current_para.right_margin = lines[sorted_indices[0]].bounds.right;
  current_para.bounds = lines[sorted_indices[0]].bounds;
  current_para.line_indices.push_back(static_cast<int>(sorted_indices[0]));

  for (size_t i = 1; i < sorted_indices.size(); ++i) {
    const Line& prev_line = lines[sorted_indices[i - 1]];
    const Line& curr_line = lines[sorted_indices[i]];

    // FIX #3: Use non-negative gap (handles overlapping lines)
    float line_gap =
        std::max(0.0f, prev_line.bounds.bottom - curr_line.bounds.top);

    // Alternative: use baseline gap for more stability
    float baseline_gap = prev_line.baseline_y - curr_line.baseline_y;

    float avg_line_height = (prev_line.bounds.top - prev_line.bounds.bottom +
                             curr_line.bounds.top - curr_line.bounds.bottom) /
                            2.0f;
    if (avg_line_height < 1.0f) {
      avg_line_height = prev_line.avg_font_size;
    }

    // Check if this line belongs to the same paragraph
    bool same_paragraph = true;

    // Check left margin alignment
    float left_diff =
        std::abs(curr_line.bounds.left - current_para.left_margin);
    if (left_diff > kLeftMarginTolerance &&
        left_diff > curr_line.avg_font_size) {
      // Allow for first-line indent
      if (current_para.line_indices.size() > 1) {
        same_paragraph = false;
      }
    }

    // Check gap - too large means new paragraph
    // Use both bounds gap and baseline gap for robustness
    if (line_gap > avg_line_height * kParagraphGapFactor ||
        baseline_gap > avg_line_height * (kParagraphGapFactor + 0.5f)) {
      same_paragraph = false;
    }

    if (!same_paragraph) {
      // Finalize current paragraph
      if (!current_para.line_indices.empty()) {
        paragraphs.push_back(std::move(current_para));
      }
      current_para = Paragraph();
      current_para.left_margin = curr_line.bounds.left;
      current_para.right_margin = curr_line.bounds.right;
      current_para.bounds = curr_line.bounds;
      current_para.line_indices.push_back(static_cast<int>(sorted_indices[i]));
    } else {
      // Add to current paragraph
      current_para.line_indices.push_back(static_cast<int>(sorted_indices[i]));
      // Update bounds
      current_para.bounds.left =
          std::min(current_para.bounds.left, curr_line.bounds.left);
      current_para.bounds.bottom =
          std::min(current_para.bounds.bottom, curr_line.bounds.bottom);
      current_para.bounds.right =
          std::max(current_para.bounds.right, curr_line.bounds.right);
      current_para.bounds.top =
          std::max(current_para.bounds.top, curr_line.bounds.top);
    }
  }

  // Finalize last paragraph
  if (!current_para.line_indices.empty()) {
    paragraphs.push_back(std::move(current_para));
  }

  return paragraphs;
}

void CPDF_TextBlockDetector::EmitTextBlocks(
    CPDF_Page* page,
    CPDF_TextPage* text_page,
    const std::vector<Line>& lines,
    const std::vector<Paragraph>& paragraphs,
    TextBlockSnapshot* snapshot) const {
  snapshot->blocks.reserve(paragraphs.size());
  snapshot->block_to_object_ids.resize(paragraphs.size());

  // Use the ObjectTable already built in Detect()
  const ObjectTable& obj_table = snapshot->object_table;

  int block_id = 0;
  for (const Paragraph& para : paragraphs) {
    TextBlock block;
    block.id = block_id;
    block.type = TextBlockType::kParagraph;
    block.ink_bounds = para.bounds;
    block.layout_bounds = para.bounds;  // Same as ink for now
    block.has_baseline = false;
    block.baseline_y = 0.0f;
    block.contains_type3 = false;
    block.table_id = -1;
    block.row = -1;
    block.col = -1;

    float total_font_size = 0.0f;
    int font_count = 0;
    std::set<ObjectId> unique_object_ids;
    WideString block_text;
    bool first_line = true;

    // Build spans from lines
    for (int line_idx : para.line_indices) {
      const Line& line = lines[line_idx];
      total_font_size += line.avg_font_size;
      ++font_count;

      // Use has_baseline flag instead of comparing to 0.0f
      if (!block.has_baseline) {
        block.baseline_y = line.baseline_y;
        block.has_baseline = true;
      }

      // Add line separator between lines (not first line)
      if (!first_line) {
        block.line_break_indices.push_back(
            static_cast<int>(block_text.GetLength()));
        block_text += L'\n';
      }
      first_line = false;

      // Compute span bounds incrementally (no inner loops!)
      int span_start = line.start_char_index;
      const CPDF_TextObject* current_obj = nullptr;
      ObjectId current_obj_id = kInvalidObjectId;
      bool current_is_type3 = false;
      CFX_FloatRect span_bounds;
      bool has_span_bounds = false;

      for (int ci = line.start_char_index; ci < line.end_char_index; ++ci) {
        const CPDF_TextPage::CharInfo& char_info = text_page->GetCharInfo(ci);
        const CPDF_TextObject* char_obj = char_info.text_object();
        const CFX_FloatRect& char_box = char_info.char_box();

        if (char_obj != current_obj) {
          // Emit previous span if any
          if (current_obj && ci > span_start && has_span_bounds) {
            block.spans.emplace_back(current_obj_id, span_start,
                                     ci - span_start, span_bounds,
                                     current_is_type3);
            if (current_obj_id != kInvalidObjectId) {
              unique_object_ids.insert(current_obj_id);
            }
            if (current_is_type3) {
              block.contains_type3 = true;
            }
          }

          // Start new span - use ObjectTable for O(1) lookup
          current_obj = char_obj;
          span_start = ci;
          current_obj_id = obj_table.GetId(char_obj);
          current_is_type3 = IsType3TextObject(text_page, ci);
          span_bounds = char_box;
          has_span_bounds = true;
        } else {
          // Accumulate bounds incrementally
          if (has_span_bounds) {
            span_bounds.left = std::min(span_bounds.left, char_box.left);
            span_bounds.bottom =
                std::min(span_bounds.bottom, char_box.bottom);
            span_bounds.right = std::max(span_bounds.right, char_box.right);
            span_bounds.top = std::max(span_bounds.top, char_box.top);
          } else {
            span_bounds = char_box;
            has_span_bounds = true;
          }
        }

        // Accumulate text for caching
        wchar_t unicode = char_info.unicode();
        if (unicode != 0) {
          block_text += unicode;
        }
      }

      // Emit last span for this line
      if (current_obj && line.end_char_index > span_start && has_span_bounds) {
        block.spans.emplace_back(current_obj_id, span_start,
                                 line.end_char_index - span_start, span_bounds,
                                 current_is_type3);
        if (current_obj_id != kInvalidObjectId) {
          unique_object_ids.insert(current_obj_id);
        }
        if (current_is_type3) {
          block.contains_type3 = true;
        }
      }
    }

    block.approx_font_size =
        font_count > 0 ? total_font_size / font_count : 12.0f;

    // Cache text content to avoid rebuilding CPDF_TextPage
    block.cached_text = std::move(block_text);

    // Store sorted ObjectIds for this block
    std::vector<ObjectId>& block_ids = snapshot->block_to_object_ids[block_id];
    block_ids.assign(unique_object_ids.begin(), unique_object_ids.end());
    std::sort(block_ids.begin(), block_ids.end());

    // Add to global set for background exclusion
    for (ObjectId id : block_ids) {
      snapshot->all_block_object_ids.insert(id);
    }

    snapshot->blocks.push_back(std::move(block));
    ++block_id;
  }
}

bool CPDF_TextBlockDetector::HasSignificantRotation(
    const CFX_Matrix& matrix,
    float threshold_degrees) const {
  float rotation = GetRotationDegrees(matrix);
  return rotation > threshold_degrees;
}

bool CPDF_TextBlockDetector::IsLineRTL(CPDF_TextPage* text_page,
                                       int start_index,
                                       int end_index) const {
  int rtl_count = 0;
  int total_count = 0;
  for (int i = start_index; i < end_index; ++i) {
    if (static_cast<size_t>(i) >= text_page->size()) {
      break;
    }
    const CPDF_TextPage::CharInfo& char_info = text_page->GetCharInfo(i);
    if (char_info.char_type() != CPDF_TextPage::CharType::kGenerated) {
      ++total_count;
      if (IsRTLCharacter(char_info.unicode())) {
        ++rtl_count;
      }
    }
  }
  return total_count > 0 && static_cast<float>(rtl_count) / total_count > 0.5f;
}

ObjectId CPDF_TextBlockDetector::GetObjectIdForChar(
    const ObjectTable& obj_table,
    CPDF_TextPage* text_page,
    int char_index) const {
  if (char_index < 0 || static_cast<size_t>(char_index) >= text_page->size()) {
    return kInvalidObjectId;
  }

  const CPDF_TextPage::CharInfo& char_info = text_page->GetCharInfo(char_index);
  const CPDF_TextObject* text_obj = char_info.text_object();
  if (!text_obj) {
    return kInvalidObjectId;
  }

  // O(1) lookup using ObjectTable
  return obj_table.GetId(text_obj);
}

bool CPDF_TextBlockDetector::IsType3TextObject(CPDF_TextPage* text_page,
                                               int char_index) const {
  if (char_index < 0 || static_cast<size_t>(char_index) >= text_page->size()) {
    return false;
  }

  const CPDF_TextPage::CharInfo& char_info = text_page->GetCharInfo(char_index);
  const CPDF_TextObject* text_obj = char_info.text_object();
  if (!text_obj) {
    return false;
  }

  // Check if the font is Type3
  RetainPtr<CPDF_Font> font = text_obj->GetFont();
  if (!font) {
    return false;
  }

  return font->IsType3Font();
}

}  // namespace textblock
}  // namespace pdfium
