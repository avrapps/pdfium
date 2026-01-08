// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFTEXT_CPDF_WORD_BUILDER_H_
#define CORE_FPDFTEXT_CPDF_WORD_BUILDER_H_

#include <vector>

#include "core/fpdftext/cpdf_layout_types.h"

namespace pdfium {
namespace layout {

// Builds words from lines using 1D gap analysis.
// This is the SECOND phase of the layout detection pipeline (after lines).
//
// The key insight: once glyphs are grouped into lines, word segmentation
// becomes a trivial 1D problem - just walk along the line and split on gaps.
class WordBuilder {
 public:
  WordBuilder();
  ~WordBuilder();

  // NEW: Build words from lines (the correct approach).
  // For each line, glyphs are already sorted by origin_x.
  // We walk them and split into words based on gaps.
  // Returns the number of words built.
  int BuildFromLines(const std::vector<LineItem>& lines,
                     const std::vector<GlyphItem>& glyphs,
                     const AdaptiveParams& params);

  // LEGACY: Build words from glyphs using greedy matching (kept for compat).
  int Build(std::vector<GlyphItem>& glyphs, const AdaptiveParams& params);

  // Get built words
  const std::vector<WordItem>& GetWords() const { return words_; }
  std::vector<WordItem>& GetMutableWords() { return words_; }

  // Get merge log (for debugging)
  const std::vector<MergeDecision>& GetMergeLog() const { return merge_log_; }

  // Enable/disable debug logging
  void SetDebugEnabled(bool enabled) { debug_enabled_ = enabled; }

 private:
  // Build words within a single line using 1D gap analysis
  void BuildWordsInLine(const LineItem& line,
                        const std::vector<GlyphItem>& glyphs,
                        const AdaptiveParams& params);

  // Compute word bounds and metadata
  void ComputeWordBounds(WordItem& word,
                         const std::vector<GlyphItem>& glyphs);

  std::vector<WordItem> words_;
  std::vector<MergeDecision> merge_log_;
  bool debug_enabled_ = false;
};

}  // namespace layout
}  // namespace pdfium

#endif  // CORE_FPDFTEXT_CPDF_WORD_BUILDER_H_
