// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdftext/cpdf_word_builder.h"

#include <algorithm>
#include <cmath>

namespace pdfium {
namespace layout {

WordBuilder::WordBuilder() = default;

WordBuilder::~WordBuilder() = default;

void WordBuilder::BuildWordsInLine(const LineItem& line,
                                    const std::vector<GlyphItem>& glyphs,
                                    const AdaptiveParams& params) {
  if (line.glyph_indices.empty()) {
    return;
  }

  // Glyphs are already sorted by origin_x in the line
  float H = params.median_height;
  float base_threshold = 0.35f * H;  // Based on HEIGHT, not width

  WordItem current_word;
  float prev_right = -1e9f;
  ObjectId prev_object_id = kInvalidObjectId;
  int prev_char_index = -1;

  for (int idx : line.glyph_indices) {
    const GlyphItem& g = glyphs[idx];

    // Hard break on space
    if (g.is_space_like) {
      if (!current_word.glyph_indices.empty()) {
        ComputeWordBounds(current_word, glyphs);
        words_.push_back(std::move(current_word));
        current_word = WordItem();
      }
      prev_right = -1e9f;
      prev_object_id = kInvalidObjectId;
      prev_char_index = -1;
      continue;
    }

    // Skip generated characters
    if (g.is_generated) {
      continue;
    }

    bool start_new = false;

    if (!current_word.glyph_indices.empty()) {
      // Compute gap: use origin_x for current glyph, bbox right for previous
      float gap = g.origin_x - prev_right;
      float threshold = base_threshold;

      // Stream order prior: same object + consecutive = more permissive
      bool consecutive_in_stream =
          (g.object_id == prev_object_id) &&
          (prev_object_id != kInvalidObjectId) &&
          (g.char_index == prev_char_index + 1);

      if (consecutive_in_stream) {
        threshold *= 1.5f;  // Allow larger gaps within same text run
      }

      // Punctuation adjustment: be more lenient around punctuation
      if (g.is_punctuation) {
        threshold *= 1.3f;
      }

      // Combining marks: never break (attach to previous)
      if (g.is_combining_mark && gap < 0) {
        start_new = false;
      } else if (gap > threshold) {
        start_new = true;

        if (debug_enabled_) {
          MergeDecision decision;
          decision.type = MergeDecision::Type::kGlyphToWord;
          decision.from_idx = current_word.glyph_indices.back();
          decision.to_idx = idx;
          decision.score = gap;
          decision.reason = "gap_exceeds_threshold";
          decision.accepted = false;
          merge_log_.push_back(decision);
        }
      }
    }

    if (start_new && !current_word.glyph_indices.empty()) {
      ComputeWordBounds(current_word, glyphs);
      words_.push_back(std::move(current_word));
      current_word = WordItem();
    }

    current_word.glyph_indices.push_back(idx);
    prev_right = RectRight(g.bbox);
    prev_object_id = g.object_id;
    prev_char_index = g.char_index;
  }

  // Emit final word
  if (!current_word.glyph_indices.empty()) {
    ComputeWordBounds(current_word, glyphs);
    words_.push_back(std::move(current_word));
  }
}

void WordBuilder::ComputeWordBounds(WordItem& word,
                                     const std::vector<GlyphItem>& glyphs) {
  if (word.glyph_indices.empty()) {
    return;
  }

  // Compute bounding box
  word.bbox = glyphs[word.glyph_indices[0]].bbox;
  for (size_t i = 1; i < word.glyph_indices.size(); ++i) {
    word.bbox = UnionRects(word.bbox, glyphs[word.glyph_indices[i]].bbox);
  }

  // Compute baseline (median)
  std::vector<float> baselines;
  baselines.reserve(word.glyph_indices.size());
  for (int gi : word.glyph_indices) {
    baselines.push_back(glyphs[gi].baseline_y);
  }
  std::nth_element(baselines.begin(),
                   baselines.begin() + baselines.size() / 2, baselines.end());
  word.baseline_y = baselines[baselines.size() / 2];

  // Compute median height
  std::vector<float> heights;
  heights.reserve(word.glyph_indices.size());
  for (int gi : word.glyph_indices) {
    heights.push_back(glyphs[gi].ink_height());
  }
  std::nth_element(heights.begin(), heights.begin() + heights.size() / 2,
                   heights.end());
  word.median_height = heights[heights.size() / 2];

  // Build text
  word.text.clear();
  for (int gi : word.glyph_indices) {
    if (glyphs[gi].unicode != 0) {
      word.text += glyphs[gi].unicode;
    }
  }

  // Collect object IDs
  word.object_ids.clear();
  for (int gi : word.glyph_indices) {
    if (glyphs[gi].object_id != kInvalidObjectId) {
      word.object_ids.insert(glyphs[gi].object_id);
    }
  }
}

int WordBuilder::BuildFromLines(const std::vector<LineItem>& lines,
                                 const std::vector<GlyphItem>& glyphs,
                                 const AdaptiveParams& params) {
  words_.clear();
  merge_log_.clear();

  if (lines.empty() || glyphs.empty()) {
    return 0;
  }

  // Process each line
  for (const LineItem& line : lines) {
    BuildWordsInLine(line, glyphs, params);
  }

  return static_cast<int>(words_.size());
}

// =============================================================================
// Legacy Build method (kept for compatibility)
// =============================================================================

int WordBuilder::Build(std::vector<GlyphItem>& glyphs,
                        const AdaptiveParams& params) {
  words_.clear();
  merge_log_.clear();

  if (glyphs.empty()) {
    return 0;
  }

  // Legacy approach: sort all glyphs by origin_x and treat as one big line
  // This is NOT the recommended approach but kept for backwards compatibility.

  // Collect non-space, non-generated glyph indices
  std::vector<int> valid_indices;
  for (size_t i = 0; i < glyphs.size(); ++i) {
    if (!glyphs[i].is_space_like && !glyphs[i].is_generated &&
        glyphs[i].rotation_bucket == 0) {
      valid_indices.push_back(static_cast<int>(i));
    }
  }

  if (valid_indices.empty()) {
    return 0;
  }

  // Sort by origin_x
  std::stable_sort(valid_indices.begin(), valid_indices.end(),
                   [&](int a, int b) {
                     if (std::abs(glyphs[a].origin_x - glyphs[b].origin_x) < 0.5f) {
                       return glyphs[a].char_index < glyphs[b].char_index;
                     }
                     return glyphs[a].origin_x < glyphs[b].origin_x;
                   });

  // Create a synthetic line
  LineItem synthetic_line;
  synthetic_line.glyph_indices = valid_indices;

  // Build words using the 1D approach
  BuildWordsInLine(synthetic_line, glyphs, params);

  return static_cast<int>(words_.size());
}

}  // namespace layout
}  // namespace pdfium
