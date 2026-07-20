// Copyright 2026 The EmbedPDF Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// EmbedPDF: annotation font map for registered runtime fonts. This file is
// fork-owned and supports FreeText fallback font routing/subsetting.

#ifndef CORE_FPDFDOC_CPDF_ANNOTFONTMAP_H_
#define CORE_FPDFDOC_CPDF_ANNOTFONTMAP_H_

#include <stdint.h>

#include <map>
#include <vector>

#include "core/fpdfdoc/ipvt_fontmap.h"
#include "core/fxcrt/bytestring.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/unowned_ptr.h"
#include "core/fxge/cfx_fontregistry.h"

class CPDF_Dictionary;
class CPDF_Document;
class CPDF_Font;

class CPDF_AnnotFontMap final : public IPVT_FontMap {
 public:
  CPDF_AnnotFontMap(CPDF_Document* doc,
                    RetainPtr<CPDF_Font> default_font,
                    const ByteString& default_font_alias,
                    bool allow_registered_fallbacks);
  ~CPDF_AnnotFontMap() override;

  static bool EnsureRegisteredFontMarkerInDocument(
      CPDF_Document* doc,
      CFX_FontRegistry::FontId font_id,
      ByteString* resource_key);

  RetainPtr<CPDF_Dictionary> CreateFontResourceDict();

  // IPVT_FontMap:
  RetainPtr<CPDF_Font> GetPDFFont(int32_t font_index) override;
  ByteString GetPDFFontAlias(int32_t font_index) override;
  int32_t GetWordFontIndex(uint16_t word,
                           FX_Charset charset,
                           int32_t font_index) override;
  int32_t CharCodeFromUnicode(int32_t font_index, uint16_t word) override;
  FX_Charset CharSetFromUnicode(uint16_t word, FX_Charset old_charset) override;

 private:
  struct FontEntry {
    RetainPtr<CPDF_Font> font;
    ByteString alias;
    CFX_FontRegistry::FontId registered_font_id =
        CFX_FontRegistry::kInvalidFontId;
    std::map<uint32_t, uint32_t> glyph_to_unicode;
  };

  bool SupportsWord(int32_t font_index, uint16_t word) const;
  void DeleteTemporaryLayoutObjects();
  RetainPtr<CPDF_Font> CreateRegisteredLayoutFont(
      CFX_FontRegistry::FontId font_id);
  int32_t FindExistingRegisteredFont(CFX_FontRegistry::FontId font_id) const;
  int32_t AddRegisteredFallbackFont(CFX_FontRegistry::FontId font_id);

  UnownedPtr<CPDF_Document> const doc_;
  const bool allow_registered_fallbacks_;
  std::vector<FontEntry> fonts_;
  std::vector<uint32_t> temporary_layout_object_numbers_;
};

#endif  // CORE_FPDFDOC_CPDF_ANNOTFONTMAP_H_
