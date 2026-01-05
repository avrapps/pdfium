// Copyright 2025 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/edit/cpdf_font_usage_collector.h"

#include <utility>

#include "core/fpdfapi/font/cpdf_cidfont.h"
#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/font/cpdf_type3font.h"
#include "core/fpdfapi/page/cpdf_form.h"
#include "core/fpdfapi/page/cpdf_formobject.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/page/cpdf_pageobject.h"
#include "core/fpdfapi/page/cpdf_textobject.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fxcrt/fx_coordinates.h"

FontUsageInfo::FontUsageInfo() = default;
FontUsageInfo::~FontUsageInfo() = default;
FontUsageInfo::FontUsageInfo(const FontUsageInfo&) = default;
FontUsageInfo::FontUsageInfo(FontUsageInfo&&) = default;
FontUsageInfo& FontUsageInfo::operator=(const FontUsageInfo&) = default;
FontUsageInfo& FontUsageInfo::operator=(FontUsageInfo&&) = default;

CPDF_FontUsageCollector::CPDF_FontUsageCollector(CPDF_Document* doc)
    : document_(doc) {}

CPDF_FontUsageCollector::~CPDF_FontUsageCollector() = default;

void CPDF_FontUsageCollector::CollectFromAllPages() {
  if (!document_) {
    fprintf(stderr, "[FontUsageCollector] Error: No document\n");
    return;
  }

  const int page_count = document_->GetPageCount();
  fprintf(stderr, "[FontUsageCollector] Processing %d pages\n", page_count);
  
  for (int i = 0; i < page_count; ++i) {
    RetainPtr<CPDF_Dictionary> page_dict =
        document_->GetMutablePageDictionary(i);
    if (!page_dict) {
      fprintf(stderr, "[FontUsageCollector] Page %d: No page dict\n", i);
      continue;
    }

    auto page = pdfium::MakeRetain<CPDF_Page>(document_, std::move(page_dict));
    page->AddPageImageCache();
    page->ParseContent();
    
    size_t obj_count = 0;
    for (auto it = page->begin(); it != page->end(); ++it) {
      obj_count++;
    }
    fprintf(stderr, "[FontUsageCollector] Page %d: %zu objects\n", i, obj_count);
    
    CollectFromPage(page.Get());
  }
  
  fprintf(stderr, "[FontUsageCollector] Total fonts found: %zu\n", usage_map_.size());
  for (const auto& entry : usage_map_) {
    const FontUsageInfo& info = entry.second;
    fprintf(stderr, "[FontUsageCollector] Font obj#%u: is_type3=%d, %zu char codes used\n",
            info.font_obj_num, info.is_type3, info.used_char_codes.size());
  }
}

void CPDF_FontUsageCollector::CollectFromPage(CPDF_Page* page) {
  if (!page)
    return;

  // The identity matrix - page content is already in page space.
  CFX_Matrix identity;
  CollectFromHolder(page, identity);
}

void CPDF_FontUsageCollector::CollectFromHolder(
    CPDF_PageObjectHolder* holder,
    const CFX_Matrix& parent_to_page) {
  if (!holder)
    return;

  for (auto it = holder->begin(); it != holder->end(); ++it) {
    CPDF_PageObject* obj = it->get();
    if (!obj || !obj->IsActive())
      continue;

    if (CPDF_TextObject* text_obj = obj->AsText()) {
      CPDF_Font* font = text_obj->GetFont();
      if (!font)
        continue;

      // Collect all character codes from this text object.
      const size_t item_count = text_obj->CountItems();
      for (size_t i = 0; i < item_count; ++i) {
        const CPDF_TextObject::Item item = text_obj->GetItemInfo(i);
        // Skip kerning/spacing entries.
        if (item.char_code_ == CPDF_Font::kInvalidCharCode)
          continue;

        RecordGlyphUsage(font, item.char_code_);
      }
      continue;
    }

    // Recurse into Form XObjects.
    if (CPDF_FormObject* form_obj = obj->AsForm()) {
      CPDF_Form* form = form_obj->form();
      if (!form)
        continue;

      // Combine the form's placement matrix with the parent transform.
      const CFX_Matrix form_to_page = parent_to_page * form_obj->form_matrix();
      CollectFromHolder(form, form_to_page);
    }
  }
}

void CPDF_FontUsageCollector::RecordGlyphUsage(CPDF_Font* font,
                                               uint32_t char_code) {
  if (!font)
    return;

  const uint32_t font_obj_num = font->GetFontDictObjNum();
  if (font_obj_num == 0)
    return;  // Not an indirect object, can't track.

  // Get or create usage info for this font.
  auto it = usage_map_.find(font_obj_num);
  if (it == usage_map_.end()) {
    FontUsageInfo info;
    info.font_obj_num = font_obj_num;
    info.font = pdfium::WrapRetain(font);
    info.is_type3 = font->IsType3Font();
    info.is_cid = font->IsCIDFont();
    
    // Check if font has ToUnicode.
    RetainPtr<const CPDF_Dictionary> font_dict = font->GetFontDict();
    if (font_dict) {
      info.has_tounicode = font_dict->KeyExist("ToUnicode");
    }
    
    it = usage_map_.emplace(font_obj_num, std::move(info)).first;
  }

  FontUsageInfo& info = it->second;
  info.used_char_codes.insert(char_code);

  // For CID fonts, also collect the GID for subsetting.
  if (const CPDF_CIDFont* cid_font = font->AsCIDFont()) {
    // Map char_code -> CID -> GID
    const uint16_t cid = cid_font->CIDFromCharCode(char_code);
    // For CIDFontType2 (TrueType), the GID comes from CIDToGIDMap.
    // If CIDToGIDMap is "Identity", GID == CID.
    // The actual mapping is done during subsetting, but we track the CID here.
    info.used_gids.insert(cid);
  } else if (!font->IsType3Font()) {
    // For simple TrueType fonts, char_code is usually the glyph selector.
    // The actual GID mapping depends on encoding, handled during subsetting.
    bool vert_glyph = false;
    int gid = font->GlyphFromCharCode(char_code, &vert_glyph);
    if (gid > 0 && gid <= 0xFFFF) {
      info.used_gids.insert(static_cast<uint16_t>(gid));
    }
  }
}

const FontUsageInfo* CPDF_FontUsageCollector::GetUsageInfoForFont(
    uint32_t font_obj_num) const {
  auto it = usage_map_.find(font_obj_num);
  return it != usage_map_.end() ? &it->second : nullptr;
}
