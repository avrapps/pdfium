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
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_object.h"
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
    printf( "[FontUsageCollector] Error: No document\n");
    return;
  }

  const int page_count = document_->GetPageCount();
  printf( "[FontUsageCollector] Processing %d pages\n", page_count);
  
  for (int i = 0; i < page_count; ++i) {
    RetainPtr<CPDF_Dictionary> page_dict =
        document_->GetMutablePageDictionary(i);
    if (!page_dict) {
      printf( "[FontUsageCollector] Page %d: No page dict\n", i);
      continue;
    }

    auto page = pdfium::MakeRetain<CPDF_Page>(document_, std::move(page_dict));
    page->AddPageImageCache();
    page->ParseContent();
    
    size_t obj_count = 0;
    for (auto it = page->begin(); it != page->end(); ++it) {
      obj_count++;
    }
    printf( "[FontUsageCollector] Page %d: %zu objects\n", i, obj_count);
    
    CollectFromPage(page.Get());
  }
  
  printf("[FontUsageCollector] Total fonts found: %zu\n", usage_map_.size());
  for (const auto& entry : usage_map_) {
    const FontUsageInfo& info = entry.second;
    printf("[FontUsageCollector] Font obj#%u: is_type3=%d, is_cid=%d, "
           "%zu char codes, %zu GIDs, %zu char_code mappings, %zu unicode mappings\n",
           info.font_obj_num, info.is_type3, info.is_cid,
           info.used_char_codes.size(), info.used_gids.size(),
           info.char_code_to_gid.size(), info.unicode_to_gid.size());
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
      
      // Debug: Log font info on first encounter
      ByteString subtype = font_dict->GetByteStringFor("Subtype");
      ByteString base_font = font_dict->GetByteStringFor("BaseFont");
      printf( "[FontUsageCollector] New font obj#%u: Subtype=%s BaseFont=%s is_cid=%d\n",
              font_obj_num, subtype.c_str(), base_font.c_str(), info.is_cid);
      
      // Check for CIDToGIDMap (important for TrueType CID fonts)
      if (font_dict->KeyExist("DescendantFonts")) {
        RetainPtr<const CPDF_Array> descendants = font_dict->GetArrayFor("DescendantFonts");
        if (descendants && !descendants->IsEmpty()) {
          RetainPtr<const CPDF_Dictionary> cid_font_dict = descendants->GetDictAt(0);
          if (cid_font_dict) {
            ByteString cid_subtype = cid_font_dict->GetByteStringFor("Subtype");
            printf( "[FontUsageCollector]   CIDFont Subtype=%s\n", cid_subtype.c_str());
            
            if (cid_font_dict->KeyExist("CIDToGIDMap")) {
              RetainPtr<const CPDF_Object> cid_to_gid = cid_font_dict->GetObjectFor("CIDToGIDMap");
              if (cid_to_gid) {
                if (cid_to_gid->IsName()) {
                  printf( "[FontUsageCollector]   CIDToGIDMap=/%s\n",
                          cid_to_gid->GetString().c_str());
                } else if (cid_to_gid->IsStream()) {
                  printf( "[FontUsageCollector]   CIDToGIDMap=<stream>\n");
                }
              }
            } else {
              printf( "[FontUsageCollector]   WARNING: No CIDToGIDMap!\n");
            }
          }
        }
      }
    }
    
    it = usage_map_.emplace(font_obj_num, std::move(info)).first;
  }

  FontUsageInfo& info = it->second;
  info.used_char_codes.insert(char_code);

  // For ALL font types (except Type3), use GlyphFromCharCode to get the actual GID.
  // This correctly handles:
  // - Simple fonts with various encodings
  // - CIDFontType2 with Identity CIDToGIDMap (CID == GID)
  // - CIDFontType2 with CIDToGIDMap stream (CID != GID, stream lookup done internally)
  if (!font->IsType3Font()) {
    bool vert_glyph = false;
    int gid = font->GlyphFromCharCode(char_code, &vert_glyph);
    
    if (gid >= 0 && gid <= 0xFFFF) {
      uint16_t gid16 = static_cast<uint16_t>(gid);
      info.used_gids.insert(gid16);
      
      // SECURITY: Build the char_code -> GID mapping for cmap rebuild.
      // This is the ONLY mapping that will exist in the output font.
      info.char_code_to_gid[char_code] = gid16;
      
      // For CID fonts, also build the Unicode -> GID mapping.
      // For CID fonts, char_code is CID (not Unicode), so we need to use
      // UnicodeFromCharCode to get the actual Unicode codepoint for the cmap.
      if (info.is_cid) {
        WideString unicode_str = font->UnicodeFromCharCode(char_code);
        if (!unicode_str.IsEmpty()) {
          // Use the first Unicode codepoint from the mapping.
          // Most characters map to a single codepoint; ligatures may map to
          // multiple but we use only the first for cmap purposes.
          uint32_t unicode_cp = static_cast<uint32_t>(unicode_str[0]);
          info.unicode_to_gid[unicode_cp] = gid16;
          
          static int unicode_log_count = 0;
          if (unicode_log_count++ < 20) {
            printf("[FontUsageCollector] CID font obj#%u: CID=%u -> Unicode=0x%04X -> GID=%u\n",
                   font_obj_num, char_code, unicode_cp, gid16);
          }
        }
      }
    }
    
    // Debug: Log GID mapping with font type info
    static int gid_log_count = 0;
    if (gid_log_count++ < 30 || gid < 0) {
      printf("[FontUsageCollector] Font obj#%u (CID=%d): char_code=%u -> GID=%d%s\n",
              font_obj_num, info.is_cid, char_code, gid,
              gid < 0 ? " (INVALID - GID lookup failed!)" : "");
    }
  }
}

const FontUsageInfo* CPDF_FontUsageCollector::GetUsageInfoForFont(
    uint32_t font_obj_num) const {
  auto it = usage_map_.find(font_obj_num);
  return it != usage_map_.end() ? &it->second : nullptr;
}
