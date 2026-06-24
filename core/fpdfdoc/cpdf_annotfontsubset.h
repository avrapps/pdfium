// Copyright 2026 The EmbedPDF Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// EmbedPDF: fork-owned helper for registered annotation font layout and
// per-annotation/layer subset embedding.

#ifndef CORE_FPDFDOC_CPDF_ANNOTFONTSUBSET_H_
#define CORE_FPDFDOC_CPDF_ANNOTFONTSUBSET_H_

#include <stdint.h>

#include <map>
#include <vector>

#include "core/fxcrt/retain_ptr.h"
#include "core/fxge/cfx_fontregistry.h"

class CPDF_Dictionary;
class CPDF_Document;
class CPDF_Font;

class CPDF_AnnotFontSubset final {
 public:
  using GlyphUnicodeMap = std::map<uint32_t, uint32_t>;

  struct LayoutFont {
    LayoutFont();
    LayoutFont(LayoutFont&& that) noexcept;
    LayoutFont& operator=(LayoutFont&& that) noexcept;
    ~LayoutFont();

    LayoutFont(const LayoutFont&) = delete;
    LayoutFont& operator=(const LayoutFont&) = delete;

    RetainPtr<CPDF_Font> font;
    std::vector<uint32_t> temporary_object_numbers;
  };

  static LayoutFont CreateLayoutFont(CPDF_Document* doc,
                                     CFX_FontRegistry::FontId font_id);

  static RetainPtr<CPDF_Dictionary> CreateSubsetFontDict(
      CPDF_Document* doc,
      CFX_FontRegistry::FontId font_id,
      const GlyphUnicodeMap& glyph_to_unicode);

  static RetainPtr<CPDF_Dictionary> CreateMarkerFontDict(
      CPDF_Document* doc,
      CFX_FontRegistry::FontId font_id);
};

#endif  // CORE_FPDFDOC_CPDF_ANNOTFONTSUBSET_H_
