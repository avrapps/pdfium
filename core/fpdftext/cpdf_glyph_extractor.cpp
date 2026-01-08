// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdftext/cpdf_glyph_extractor.h"

#include <algorithm>
#include <cmath>

#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/page/cpdf_textobject.h"
#include "core/fpdfapi/render/cpdf_renderobjectfilter.h"
#include "core/fpdftext/cpdf_textpage.h"

namespace pdfium {
namespace layout {

namespace {

// Thresholds
constexpr float kMinFontSizeThreshold = 4.0f;
constexpr float kRotationThresholdDegrees = 5.0f;
constexpr float kSkewThresholdRatio = 0.1f;

// Get rotation angle from matrix in degrees
float GetRotationDegrees(const CFX_Matrix& matrix) {
  float angle_rad = std::atan2(matrix.b, matrix.a);
  return std::abs(angle_rad * 180.0f / 3.14159265358979f);
}

// Check if matrix has significant skew/shear
bool HasSignificantSkew(const CFX_Matrix& matrix) {
  float scale_x = std::sqrt(matrix.a * matrix.a + matrix.b * matrix.b);
  float scale_y = std::sqrt(matrix.c * matrix.c + matrix.d * matrix.d);
  if (scale_x < 0.001f || scale_y < 0.001f) {
    return true;  // Degenerate matrix
  }
  float shear_indicator = std::abs(matrix.a * matrix.c + matrix.b * matrix.d);
  float magnitude = scale_x * scale_y;
  return (shear_indicator / magnitude) > kSkewThresholdRatio;
}

// Check if a Unicode character is in RTL script range
bool IsRTLCharacter(wchar_t unicode) {
  if ((unicode >= 0x0590 && unicode <= 0x05FF) ||  // Hebrew
      (unicode >= 0x0600 && unicode <= 0x06FF) ||  // Arabic
      (unicode >= 0x0700 && unicode <= 0x074F) ||  // Syriac
      (unicode >= 0x0750 && unicode <= 0x077F) ||  // Arabic Supplement
      (unicode >= 0x0780 && unicode <= 0x07BF) ||  // Thaana
      (unicode >= 0x08A0 && unicode <= 0x08FF) ||  // Arabic Extended-A
      (unicode >= 0xFB00 && unicode <= 0xFDFF) ||  // Hebrew/Arabic presentation
      (unicode >= 0xFE70 && unicode <= 0xFEFF)) {  // Arabic presentation
    return true;
  }
  return false;
}

}  // namespace

GlyphExtractor::GlyphExtractor() = default;

GlyphExtractor::~GlyphExtractor() = default;

int GlyphExtractor::ComputeRotationBucket(float angle_degrees) {
  // Normalize to 0-360
  while (angle_degrees < 0)
    angle_degrees += 360.0f;
  while (angle_degrees >= 360.0f)
    angle_degrees -= 360.0f;

  if (angle_degrees < 45.0f || angle_degrees >= 315.0f) {
    return 0;
  } else if (angle_degrees < 135.0f) {
    return 90;
  } else if (angle_degrees < 225.0f) {
    return 180;
  } else {
    return 270;
  }
}

float GlyphExtractor::ComputeEffectiveFontSize(float raw_font_size,
                                                const CFX_FloatRect& char_box,
                                                float matrix_scale_y) {
  float effective = raw_font_size * matrix_scale_y;
  float char_height = RectHeight(char_box);
  return std::max(effective, char_height);
}

float GlyphExtractor::ComputeMedian(std::vector<float>& values) {
  if (values.empty()) {
    return 0.0f;
  }
  size_t n = values.size();
  std::nth_element(values.begin(), values.begin() + n / 2, values.end());
  if (n % 2 == 0) {
    float mid1 = values[n / 2];
    std::nth_element(values.begin(), values.begin() + n / 2 - 1, values.end());
    float mid2 = values[n / 2 - 1];
    return (mid1 + mid2) / 2.0f;
  }
  return values[n / 2];
}

int GlyphExtractor::Extract(CPDF_Page* page,
                            CPDF_TextPage* text_page,
                            const pdfium::render::ObjectTable& obj_table) {
  glyphs_.clear();

  if (!page || !text_page || text_page->size() == 0) {
    stats_ = PageStats();
    return 0;
  }

  // Get page bounds
  stats_.page_bounds = page->GetBBox();
  stats_.page_width = RectWidth(stats_.page_bounds);
  stats_.page_height = RectHeight(stats_.page_bounds);

  int char_count = static_cast<int>(text_page->size());
  glyphs_.reserve(char_count);

  // Vectors for computing statistics
  std::vector<float> heights;
  std::vector<float> widths;
  std::vector<float> advances;
  heights.reserve(char_count);
  widths.reserve(char_count);
  advances.reserve(char_count);

  float prev_x = -1e9f;

  for (int i = 0; i < char_count; ++i) {
    const CPDF_TextPage::CharInfo& char_info = text_page->GetCharInfo(i);

    GlyphItem glyph;
    glyph.char_index = i;
    glyph.bbox = char_info.char_box();
    glyph.origin_x = char_info.origin().x;
    glyph.baseline_y = char_info.origin().y;
    glyph.unicode = char_info.unicode();

    // Check if generated character (spaces, line breaks)
    if (char_info.char_type() == CPDF_TextPage::CharType::kGenerated) {
      glyph.is_generated = true;
    }

    // Check character properties
    glyph.is_space_like = IsSpaceLikeChar(glyph.unicode);
    glyph.is_combining_mark = IsCombiningMark(glyph.unicode);
    glyph.is_punctuation = IsPunctuationChar(glyph.unicode);

    // Get matrix and compute rotation/skew
    const CFX_Matrix& matrix = char_info.matrix();
    float rotation = GetRotationDegrees(matrix);
    glyph.is_skewed = HasSignificantSkew(matrix);
    glyph.rotation_bucket = ComputeRotationBucket(rotation);

    // Compute effective font size
    float raw_font_size = text_page->GetCharFontSize(i);
    float scale_y = std::sqrt(matrix.c * matrix.c + matrix.d * matrix.d);
    glyph.effective_font_size =
        ComputeEffectiveFontSize(raw_font_size, glyph.bbox, scale_y);

    // Get object ID
    const CPDF_TextObject* text_obj = char_info.text_object();
    if (text_obj) {
      glyph.object_id = obj_table.GetId(text_obj);

      // Check for Type3 font
      RetainPtr<CPDF_Font> font = text_obj->GetFont();
      if (font && font->IsType3Font()) {
        glyph.is_type3 = true;
      }
    }

    // Update rotation counts
    int rot_idx = glyph.rotation_bucket / 90;
    if (rot_idx >= 0 && rot_idx < 4) {
      stats_.rotation_counts[rot_idx]++;
    }

    // Collect statistics for non-generated, non-space glyphs
    if (!glyph.is_generated && !glyph.is_space_like) {
      float h = RectHeight(glyph.bbox);
      float w = RectWidth(glyph.bbox);

      if (h > 0.1f) {
        heights.push_back(h);
      }
      if (w > 0.1f) {
        widths.push_back(w);
      }

      // Compute advance (gap from previous glyph)
      float curr_x = RectLeft(glyph.bbox);
      if (prev_x > -1e8f && curr_x > prev_x) {
        float advance = curr_x - prev_x;
        if (advance > 0.1f && advance < 100.0f) {  // Reasonable range
          advances.push_back(advance);
        }
      }
      prev_x = RectRight(glyph.bbox);
    }

    glyphs_.push_back(std::move(glyph));
  }

  // Compute median statistics
  stats_.total_glyphs = static_cast<int>(glyphs_.size());
  stats_.median_glyph_height =
      heights.empty() ? 12.0f : ComputeMedian(heights);
  stats_.median_glyph_width = widths.empty() ? 6.0f : ComputeMedian(widths);
  stats_.median_char_advance =
      advances.empty() ? stats_.median_glyph_width : ComputeMedian(advances);

  // Sanity clamp
  stats_.median_glyph_height =
      std::clamp(stats_.median_glyph_height, 4.0f, 200.0f);
  stats_.median_glyph_width =
      std::clamp(stats_.median_glyph_width, 2.0f, 100.0f);
  stats_.median_char_advance =
      std::clamp(stats_.median_char_advance, 2.0f, 100.0f);

  return static_cast<int>(glyphs_.size());
}

}  // namespace layout
}  // namespace pdfium
