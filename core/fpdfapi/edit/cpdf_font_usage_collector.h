// Copyright 2025 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFAPI_EDIT_CPDF_FONT_USAGE_COLLECTOR_H_
#define CORE_FPDFAPI_EDIT_CPDF_FONT_USAGE_COLLECTOR_H_

#include <map>
#include <set>
#include <vector>

#include "core/fxcrt/retain_ptr.h"

class CPDF_Document;
class CPDF_Font;
class CPDF_Page;
class CPDF_PageObjectHolder;
class CFX_Matrix;

// Information about how a font is used in the document.
// Used for font subsetting and ToUnicode map rebuilding.
struct FontUsageInfo {
  FontUsageInfo();
  ~FontUsageInfo();
  FontUsageInfo(const FontUsageInfo&);
  FontUsageInfo(FontUsageInfo&&);
  FontUsageInfo& operator=(const FontUsageInfo&);
  FontUsageInfo& operator=(FontUsageInfo&&);

  // The font's dictionary object number.
  uint32_t font_obj_num = 0;
  
  // Retained reference to the font.
  RetainPtr<CPDF_Font> font;

  // Character codes that are used (as they appear in text objects).
  std::set<uint32_t> used_char_codes;
  
  // Glyph IDs used (for TrueType/CID fonts - mapped from char codes).
  std::set<uint16_t> used_gids;
  
  // Whether this font has a ToUnicode CMap.
  bool has_tounicode = false;
  
  // Whether this is a Type3 font (needs special CharProcs handling).
  bool is_type3 = false;
  
  // Whether this is a CID font.
  bool is_cid = false;
};

// Collects information about which glyphs are actually used in a PDF document.
// This is used to determine which glyphs can be safely removed during
// font subsetting for redaction or file size optimization.
class CPDF_FontUsageCollector {
 public:
  explicit CPDF_FontUsageCollector(CPDF_Document* doc);
  ~CPDF_FontUsageCollector();

  // Collects usage information from all pages in the document.
  // After calling this, GetUsageInfo() returns the collected data.
  void CollectFromAllPages();

  // Collects usage information from a single page.
  void CollectFromPage(CPDF_Page* page);

  // Returns the collected font usage information.
  // Key is the font dictionary object number.
  const std::map<uint32_t, FontUsageInfo>& GetUsageInfo() const {
    return usage_map_;
  }

  // Gets usage info for a specific font, or nullptr if not found.
  const FontUsageInfo* GetUsageInfoForFont(uint32_t font_obj_num) const;

 private:
  // Collects usage from a page object holder (page or form XObject).
  void CollectFromHolder(CPDF_PageObjectHolder* holder,
                         const CFX_Matrix& parent_to_page);

  // Records usage of a single glyph.
  void RecordGlyphUsage(CPDF_Font* font, uint32_t char_code);

  CPDF_Document* document_;
  std::map<uint32_t, FontUsageInfo> usage_map_;
};

#endif  // CORE_FPDFAPI_EDIT_CPDF_FONT_USAGE_COLLECTOR_H_
