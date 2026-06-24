// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fpdfdoc/cpvt_fontmap.h"

#include <utility>

#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/fpdf_parser_utility.h"
#include "core/fpdfdoc/cpdf_interactiveform.h"
#include "core/fxcrt/check.h"
#include "core/fxcrt/fx_codepage.h"

namespace {

// EmbedPDF: CPVT_FontMap is used by upstream FreeText appearance generation,
// but upstream left these font-selection hooks as NOTREACHED(). Registered
// annotation fonts need real default behavior so CPVT_VariableText can ask the
// active font map whether a specific glyph is supported.
bool FontSupportsUnicode(const RetainPtr<CPDF_Font>& font, uint16_t word) {
  if (!font) {
    return false;
  }

  uint32_t charcode = font->CharCodeFromUnicode(word);
  if (charcode == CPDF_Font::kInvalidCharCode || (charcode == 0 && word != 0)) {
    return false;
  }

  bool vert_glyph = false;
  return font->GlyphFromCharCode(charcode, &vert_glyph) > 0;
}

}  // namespace

CPVT_FontMap::CPVT_FontMap(CPDF_Document* doc,
                           RetainPtr<CPDF_Dictionary> pResDict,
                           RetainPtr<CPDF_Font> pDefFont,
                           const ByteString& sDefFontAlias)
    : document_(doc),
      res_dict_(std::move(pResDict)),
      def_font_(std::move(pDefFont)),
      def_font_alias_(sDefFontAlias) {}

CPVT_FontMap::~CPVT_FontMap() = default;

void CPVT_FontMap::SetupAnnotSysPDFFont() {
  if (!document_ || !res_dict_) {
    return;
  }

  RetainPtr<CPDF_Font> pPDFFont =
      CPDF_InteractiveForm::AddNativeInteractiveFormFont(document_,
                                                         &sys_font_alias_);
  if (!pPDFFont) {
    return;
  }

  RetainPtr<CPDF_Dictionary> font_list = res_dict_->GetMutableDictFor("Font");
  if (ValidateFontResourceDict(font_list.Get()) &&
      !font_list->KeyExist(sys_font_alias_.AsStringView())) {
    font_list->SetNewFor<CPDF_Reference>(sys_font_alias_, document_,
                                         pPDFFont->GetFontDictObjNum());
  }
  sys_font_ = std::move(pPDFFont);
}

RetainPtr<CPDF_Font> CPVT_FontMap::GetPDFFont(int32_t nFontIndex) {
  switch (nFontIndex) {
    case 0:
      return def_font_;
    case 1:
      if (!sys_font_) {
        SetupAnnotSysPDFFont();
      }
      return sys_font_;
    default:
      return nullptr;
  }
}

ByteString CPVT_FontMap::GetPDFFontAlias(int32_t nFontIndex) {
  switch (nFontIndex) {
    case 0:
      return def_font_alias_;
    case 1:
      if (!sys_font_) {
        SetupAnnotSysPDFFont();
      }
      return sys_font_alias_;
    default:
      return ByteString();
  }
}

int32_t CPVT_FontMap::GetWordFontIndex(uint16_t word,
                                       FX_Charset charset,
                                       int32_t nFontIndex) {
  // EmbedPDF: keep the current font when it can draw the word, otherwise fall
  // back to the default DA font and then PDFium's native annotation font.
  if (nFontIndex >= 0 && FontSupportsUnicode(GetPDFFont(nFontIndex), word)) {
    return nFontIndex;
  }
  if (FontSupportsUnicode(GetPDFFont(0), word)) {
    return 0;
  }
  if (FontSupportsUnicode(GetPDFFont(1), word)) {
    return 1;
  }
  return -1;
}

int32_t CPVT_FontMap::CharCodeFromUnicode(int32_t nFontIndex, uint16_t word) {
  // EmbedPDF: expose a common unicode-to-charcode hook so specialized font maps
  // can return registered-font subset glyph ids without changing PVT layout.
  RetainPtr<CPDF_Font> font = GetPDFFont(nFontIndex);
  if (!font) {
    return -1;
  }
  uint32_t charcode = font->CharCodeFromUnicode(word);
  if (charcode == CPDF_Font::kInvalidCharCode || (charcode == 0 && word != 0)) {
    return -1;
  }
  return charcode;
}

FX_Charset CPVT_FontMap::CharSetFromUnicode(uint16_t word,
                                            FX_Charset nOldCharset) {
  // EmbedPDF: provide a conservative default implementation for the PVT
  // provider hook; registered annotation maps may override as needed.
  if (word < 0x7F) {
    return FX_Charset::kANSI;
  }
  if (nOldCharset != FX_Charset::kDefault) {
    return nOldCharset;
  }
  return CFX_Font::GetCharSetFromUnicode(word);
}
