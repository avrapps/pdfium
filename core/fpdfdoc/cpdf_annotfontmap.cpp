// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0
//
// EmbedPDF: annotation font map for registered runtime fonts. This lets
// FreeText appearance generation pick per-glyph fallback fonts and later embed
// only the glyph subset used by the annotation/layer.

#include "core/fpdfdoc/cpdf_annotfontmap.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfdoc/cpdf_annotfontsubset.h"
#include "core/fpdfdoc/cpdf_interactiveform.h"
#include "core/fxcrt/check.h"
#include "core/fxcrt/fx_codepage.h"
#include "core/fxcrt/fx_safe_types.h"
#include "core/fxcrt/numerics/safe_conversions.h"
#include "core/fxcrt/stl_util.h"
#include "core/fxge/cfx_font.h"

namespace {

constexpr char kRegisteredFontResourcePrefix[] = "ERegF";

ByteString ResourceKeyForRegisteredFont(CFX_FontRegistry::FontId font_id) {
  return ByteString::Format("%s%u", kRegisteredFontResourcePrefix, font_id);
}

bool PDFontSupportsUnicode(const RetainPtr<CPDF_Font>& font, uint16_t word) {
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

CPDF_AnnotFontMap::CPDF_AnnotFontMap(CPDF_Document* doc,
                                     RetainPtr<CPDF_Font> default_font,
                                     const ByteString& default_font_alias,
                                     bool allow_registered_fallbacks)
    : doc_(doc), allow_registered_fallbacks_(allow_registered_fallbacks) {
  FontEntry entry;
  entry.font = std::move(default_font);
  entry.alias = default_font_alias;
  RetainPtr<const CPDF_Dictionary> default_font_dict =
      entry.font ? entry.font->GetFontDict() : nullptr;
  if (auto font_id =
          CPDF_AnnotFontSubset::GetRegisteredFontIdFromMarkerFontDict(
              default_font_dict.Get())) {
    RetainPtr<CPDF_Font> registered_font = CreateRegisteredLayoutFont(*font_id);
    if (registered_font) {
      entry.font = std::move(registered_font);
      entry.registered_font_id = *font_id;
    }
  }
  fonts_.push_back(std::move(entry));
}

CPDF_AnnotFontMap::~CPDF_AnnotFontMap() {
  DeleteTemporaryLayoutObjects();
}

// static
bool CPDF_AnnotFontMap::EnsureRegisteredFontMarkerInDocument(
    CPDF_Document* doc,
    CFX_FontRegistry::FontId font_id,
    ByteString* resource_key) {
  if (!doc || !resource_key || !CFX_FontRegistry::IsValidFont(font_id)) {
    return false;
  }

  RetainPtr<CPDF_Dictionary> root_dict = doc->GetMutableRoot();
  if (!root_dict) {
    return false;
  }

  RetainPtr<CPDF_Dictionary> acroform_dict =
      root_dict->GetMutableDictFor("AcroForm");
  if (!acroform_dict) {
    acroform_dict = CPDF_InteractiveForm::InitAcroFormDict(doc);
    CHECK(acroform_dict);
  }

  RetainPtr<CPDF_Dictionary> font_res =
      acroform_dict->GetOrCreateDictFor("DR")->GetOrCreateDictFor("Font");

  ByteString key = ResourceKeyForRegisteredFont(font_id);
  if (RetainPtr<CPDF_Dictionary> existing_font_dict =
          font_res->GetMutableDictFor(key.AsStringView())) {
    // EmbedPDF: registered-font identity is stored in the marker dictionary,
    // not inferred from the resource alias. This survives alias collisions,
    // suffixes, and resource renaming during save/merge.
    if (CPDF_AnnotFontSubset::GetRegisteredFontIdFromMarkerFontDict(
            existing_font_dict.Get()) == font_id) {
      *resource_key = key;
      return true;
    }
  }

  const ByteString base_key = key;
  for (int suffix = 1; font_res->KeyExist(key.AsStringView()); ++suffix) {
    key = ByteString::Format("%s_%d", base_key.c_str(), suffix);
  }

  RetainPtr<CPDF_Dictionary> marker_font_dict =
      CPDF_AnnotFontSubset::CreateMarkerFontDict(doc, font_id);
  if (!marker_font_dict) {
    return false;
  }

  font_res->SetNewFor<CPDF_Reference>(key, doc, marker_font_dict->GetObjNum());
  *resource_key = key;
  return true;
}

RetainPtr<CPDF_Dictionary> CPDF_AnnotFontMap::CreateFontResourceDict() {
  if (!doc_) {
    return nullptr;
  }

  auto resource_font_dict = doc_->New<CPDF_Dictionary>();
  for (FontEntry& entry : fonts_) {
    if (!entry.font || entry.alias.IsEmpty()) {
      continue;
    }

    if (entry.registered_font_id != CFX_FontRegistry::kInvalidFontId) {
      RetainPtr<CPDF_Dictionary> subset_font_dict =
          CPDF_AnnotFontSubset::CreateSubsetFontDict(
              doc_, entry.registered_font_id, entry.glyph_to_unicode);
      if (subset_font_dict) {
        resource_font_dict->SetNewFor<CPDF_Reference>(
            entry.alias, doc_, subset_font_dict->GetObjNum());
      }
      continue;
    }

    RetainPtr<const CPDF_Dictionary> font_dict = entry.font->GetFontDict();
    if (!font_dict) {
      continue;
    }

    const uint32_t font_obj_num = font_dict->GetObjNum();
    if (font_obj_num != 0) {
      resource_font_dict->SetNewFor<CPDF_Reference>(entry.alias, doc_,
                                                    font_obj_num);
    } else {
      resource_font_dict->SetFor(entry.alias, font_dict->Clone());
    }
  }
  return resource_font_dict;
}

RetainPtr<CPDF_Font> CPDF_AnnotFontMap::GetPDFFont(int32_t font_index) {
  return fxcrt::IndexInBounds(fonts_, font_index) ? fonts_[font_index].font
                                                  : nullptr;
}

ByteString CPDF_AnnotFontMap::GetPDFFontAlias(int32_t font_index) {
  return fxcrt::IndexInBounds(fonts_, font_index) ? fonts_[font_index].alias
                                                  : ByteString();
}

int32_t CPDF_AnnotFontMap::GetWordFontIndex(uint16_t word,
                                            FX_Charset charset,
                                            int32_t font_index) {
  if (SupportsWord(font_index, word)) {
    return font_index;
  }
  if (SupportsWord(0, word)) {
    return 0;
  }

  for (size_t i = 1; i < fonts_.size(); ++i) {
    if (SupportsWord(pdfium::checked_cast<int32_t>(i), word)) {
      return pdfium::checked_cast<int32_t>(i);
    }
  }

  if (!allow_registered_fallbacks_ || !fonts_.front().font) {
    return -1;
  }

  const int weight =
      fonts_.front().font->GetFontWeight().value_or(pdfium::kFontWeightNormal);
  const bool italic = fonts_.front().font->GetItalicAngle() != 0;
  std::optional<CFX_FontRegistry::FontId> font_id =
      CFX_FontRegistry::FindFallbackFont(word, weight, italic);
  if (!font_id.has_value()) {
    return -1;
  }

  int32_t existing_font_index = FindExistingRegisteredFont(*font_id);
  if (existing_font_index >= 0) {
    return existing_font_index;
  }

  return AddRegisteredFallbackFont(*font_id);
}

int32_t CPDF_AnnotFontMap::CharCodeFromUnicode(int32_t font_index,
                                               uint16_t word) {
  RetainPtr<CPDF_Font> font = GetPDFFont(font_index);
  if (!font) {
    return -1;
  }

  uint32_t charcode = font->CharCodeFromUnicode(word);
  if (charcode == CPDF_Font::kInvalidCharCode || (charcode == 0 && word != 0)) {
    return -1;
  }
  if (fxcrt::IndexInBounds(fonts_, font_index)) {
    FontEntry& entry = fonts_[font_index];
    if (entry.registered_font_id != CFX_FontRegistry::kInvalidFontId) {
      entry.glyph_to_unicode.emplace(charcode, word);
    }
  }
  return pdfium::checked_cast<int32_t>(charcode);
}

FX_Charset CPDF_AnnotFontMap::CharSetFromUnicode(uint16_t word,
                                                 FX_Charset old_charset) {
  if (word < 0x7F) {
    return FX_Charset::kANSI;
  }
  if (old_charset != FX_Charset::kDefault) {
    return old_charset;
  }
  return CFX_Font::GetCharSetFromUnicode(word);
}

bool CPDF_AnnotFontMap::SupportsWord(int32_t font_index, uint16_t word) const {
  if (!fxcrt::IndexInBounds(fonts_, font_index)) {
    return false;
  }
  const FontEntry& entry = fonts_[font_index];
  if (!entry.font) {
    return false;
  }
  if (entry.registered_font_id != CFX_FontRegistry::kInvalidFontId) {
    return CFX_FontRegistry::SupportsUnicode(entry.registered_font_id, word);
  }
  return PDFontSupportsUnicode(entry.font, word);
}

void CPDF_AnnotFontMap::DeleteTemporaryLayoutObjects() {
  fonts_.clear();
  if (!doc_) {
    temporary_layout_object_numbers_.clear();
    return;
  }

  for (uint32_t obj_num : temporary_layout_object_numbers_) {
    doc_->DeleteIndirectObject(obj_num);
  }
  temporary_layout_object_numbers_.clear();
}

RetainPtr<CPDF_Font> CPDF_AnnotFontMap::CreateRegisteredLayoutFont(
    CFX_FontRegistry::FontId font_id) {
  CPDF_AnnotFontSubset::LayoutFont layout_font =
      CPDF_AnnotFontSubset::CreateLayoutFont(doc_, font_id);
  temporary_layout_object_numbers_.insert(
      temporary_layout_object_numbers_.end(),
      layout_font.temporary_object_numbers.begin(),
      layout_font.temporary_object_numbers.end());
  return std::move(layout_font.font);
}

int32_t CPDF_AnnotFontMap::FindExistingRegisteredFont(
    CFX_FontRegistry::FontId font_id) const {
  for (size_t i = 0; i < fonts_.size(); ++i) {
    if (fonts_[i].registered_font_id == font_id) {
      return pdfium::checked_cast<int32_t>(i);
    }
  }
  return -1;
}

int32_t CPDF_AnnotFontMap::AddRegisteredFallbackFont(
    CFX_FontRegistry::FontId font_id) {
  RetainPtr<CPDF_Font> font = CreateRegisteredLayoutFont(font_id);
  if (!font) {
    return -1;
  }

  FontEntry entry;
  entry.font = std::move(font);
  entry.alias = ResourceKeyForRegisteredFont(font_id);
  entry.registered_font_id = font_id;
  fonts_.push_back(std::move(entry));
  return pdfium::checked_cast<int32_t>(fonts_.size() - 1);
}
