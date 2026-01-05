// Copyright 2025 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFAPI_EDIT_CPDF_FONT_SUBSETTER_H_
#define CORE_FPDFAPI_EDIT_CPDF_FONT_SUBSETTER_H_

#include <map>
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
  // |char_code_to_gid| - SECURITY: The exact char_code -> GID mappings for cmap rebuild.
  //                      Only these mappings will exist in the output font's cmap.
  //                      This prevents information leakage about redacted characters.
  // Returns a SubsetResult with the subsetted font data on success.
  SubsetResult SubsetByGlyphIds(pdfium::span<const uint8_t> font_data,
                                const std::set<uint16_t>& gids_to_keep,
                                const std::map<uint32_t, uint16_t>& char_code_to_gid);

  // Subsets a font and replaces the FontFile stream in the document.
  // |doc| - The PDF document.
  // |font| - The font to subset.
  // |gids_to_keep| - Set of glyph IDs that should be retained.
  // |char_code_to_gid| - SECURITY: The exact char_code -> GID mappings for cmap.
  // Returns true if the font was successfully subsetted and replaced.
  bool SubsetAndReplaceFont(CPDF_Document* doc,
                            CPDF_Font* font,
                            const std::set<uint16_t>& gids_to_keep,
                            const std::map<uint32_t, uint16_t>& char_code_to_gid);

 private:
  // Gets the font file stream from a font's descriptor.
  RetainPtr<CPDF_Stream> GetFontFileStream(CPDF_Font* font);
  
  // Determines the font file key name ("FontFile", "FontFile2", or "FontFile3").
  const char* GetFontFileKeyName(CPDF_Font* font);
  
  // SECURITY: Replaces the entire cmap table with a minimal one containing
  // only the exact char_code -> GID mappings we specify.
  // This is the key to preventing information leakage about redacted characters.
  // |font_data| - The font data to modify (will be resized).
  // |char_code_to_gid| - The exact mappings for the new cmap.
  // Returns true if cmap was successfully replaced.
  bool ReplaceEntireCmapTable(DataVector<uint8_t>& font_data,
                              const std::map<uint32_t, uint16_t>& char_code_to_gid);
  
  // Builds a Format 0 cmap subtable (for char codes 0-255).
  // Returns the complete cmap table including header and encoding record.
  DataVector<uint8_t> BuildCmapFormat0(
      const std::map<uint32_t, uint16_t>& char_code_to_gid);
  
  // Builds a Format 4 cmap subtable (for char codes 0-65535).
  // Uses glyphIdArray approach (no delta segments) for security.
  // Returns the complete cmap table including header and encoding record.
  DataVector<uint8_t> BuildCmapFormat4(
      const std::map<uint32_t, uint16_t>& char_code_to_gid);
  
  // Builds a Format 12 cmap subtable (for char codes > 65535, full Unicode).
  // Uses sequential groups - each contiguous range becomes one group.
  // Returns the complete cmap table including header and encoding record.
  DataVector<uint8_t> BuildCmapFormat12(
      const std::map<uint32_t, uint16_t>& char_code_to_gid);
  
  // Helper: Finds the cmap table entry in the table directory.
  // Returns the offset to the table directory entry, or 0 if not found.
  uint32_t FindCmapTableDirEntry(pdfium::span<const uint8_t> font_data);
};

#endif  // CORE_FPDFAPI_EDIT_CPDF_FONT_SUBSETTER_H_
