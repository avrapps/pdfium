// Copyright 2025 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFAPI_EDIT_CPDF_FONT_SUBSETTER_H_
#define CORE_FPDFAPI_EDIT_CPDF_FONT_SUBSETTER_H_

#include <set>
#include <vector>

#include "core/fxcrt/data_vector.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/span.h"

class CPDF_Document;
class CPDF_Font;
class CPDF_Stream;

// Result of a font subsetting operation.
struct SubsetResult {
  // True if subsetting succeeded.
  bool success = false;
  
  // The subsetted font data (empty on failure).
  DataVector<uint8_t> font_data;
  
  // Error message on failure (empty on success).
  const char* error_message = nullptr;
};

// Subsets TrueType and CFF fonts using HarfBuzz.
// This removes unused glyphs from the font program to reduce file size
// and eliminate potential information leaks from redacted content.
class CPDF_FontSubsetter {
 public:
  CPDF_FontSubsetter();
  ~CPDF_FontSubsetter();

  // Subsets a font to only include the specified glyph IDs.
  // |font_data| - The original font program data.
  // |gids_to_keep| - Set of glyph IDs that should be retained.
  // Returns a SubsetResult with the subsetted font data on success.
  SubsetResult SubsetByGlyphIds(pdfium::span<const uint8_t> font_data,
                                const std::set<uint16_t>& gids_to_keep);

  // Subsets a font and replaces the FontFile stream in the document.
  // |doc| - The PDF document.
  // |font| - The font to subset.
  // |gids_to_keep| - Set of glyph IDs that should be retained.
  // Returns true if the font was successfully subsetted and replaced.
  bool SubsetAndReplaceFont(CPDF_Document* doc,
                            CPDF_Font* font,
                            const std::set<uint16_t>& gids_to_keep);

 private:
  // Gets the font file stream from a font's descriptor.
  RetainPtr<CPDF_Stream> GetFontFileStream(CPDF_Font* font);
  
  // Determines the font file key name ("FontFile", "FontFile2", or "FontFile3").
  const char* GetFontFileKeyName(CPDF_Font* font);
};

#endif  // CORE_FPDFAPI_EDIT_CPDF_FONT_SUBSETTER_H_
