// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFTEXT_CPDF_GLYPH_EXTRACTOR_H_
#define CORE_FPDFTEXT_CPDF_GLYPH_EXTRACTOR_H_

#include <vector>

#include "core/fpdftext/cpdf_layout_types.h"

class CPDF_Page;
class CPDF_TextPage;

namespace pdfium {
namespace render {
class ObjectTable;
}
}  // namespace pdfium

namespace pdfium {
namespace layout {

// Extracts glyphs from a CPDF_TextPage and computes page statistics.
// This is Phase 0 of the layout detection pipeline.
class GlyphExtractor {
 public:
  GlyphExtractor();
  ~GlyphExtractor();

  // Extract glyphs from the text page and compute statistics.
  // Returns the number of glyphs extracted.
  int Extract(CPDF_Page* page,
              CPDF_TextPage* text_page,
              const pdfium::render::ObjectTable& obj_table);

  // Get extracted glyphs (0-degree rotation only for v1)
  const std::vector<GlyphItem>& GetGlyphs() const { return glyphs_; }
  std::vector<GlyphItem>& GetMutableGlyphs() { return glyphs_; }

  // Get computed page statistics
  const PageStats& GetStats() const { return stats_; }

  // Get adaptive parameters derived from stats
  AdaptiveParams GetAdaptiveParams() const {
    return AdaptiveParams::FromStats(stats_);
  }

 private:
  // Compute rotation bucket from matrix (0, 90, 180, 270)
  static int ComputeRotationBucket(float angle_degrees);

  // Compute effective font size considering matrix scaling
  static float ComputeEffectiveFontSize(float raw_font_size,
                                         const CFX_FloatRect& char_box,
                                         float matrix_scale_y);

  // Compute median of a vector (modifies the vector)
  static float ComputeMedian(std::vector<float>& values);

  std::vector<GlyphItem> glyphs_;
  PageStats stats_;
};

}  // namespace layout
}  // namespace pdfium

#endif  // CORE_FPDFTEXT_CPDF_GLYPH_EXTRACTOR_H_
