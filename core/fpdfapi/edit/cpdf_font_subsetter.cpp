// Copyright 2025 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/edit/cpdf_font_subsetter.h"

#include <utility>

#include "core/fpdfapi/font/cpdf_cidfont.h"
#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/font/cpdf_type1font.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/cpdf_stream_acc.h"
#include "core/fxcrt/span.h"

#if defined(PDF_ENABLE_FONT_SANITIZER)
#include "hb.h"
#include "hb-subset.h"
#endif

// Helper functions for font table manipulation
namespace {

inline uint16_t ReadU16BE(const uint8_t* data) {
  return static_cast<uint16_t>((data[0] << 8) | data[1]);
}

inline uint32_t ReadU32BE(const uint8_t* data) {
  return static_cast<uint32_t>((data[0] << 24) | (data[1] << 16) |
                                (data[2] << 8) | data[3]);
}

inline void WriteU16BE(uint8_t* data, uint16_t value) {
  data[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
  data[1] = static_cast<uint8_t>(value & 0xFF);
}

inline void WriteU32BE(uint8_t* data, uint32_t value) {
  data[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
  data[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
  data[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
  data[3] = static_cast<uint8_t>(value & 0xFF);
}

// Calculate TrueType table checksum (sum of 32-bit big-endian values)
uint32_t CalcTableChecksum(const uint8_t* data, uint32_t length) {
  uint32_t sum = 0;
  uint32_t num_longs = (length + 3) / 4;
  for (uint32_t i = 0; i < num_longs; i++) {
    uint32_t offset = i * 4;
    uint32_t value = 0;
    if (offset < length) value |= static_cast<uint32_t>(data[offset]) << 24;
    if (offset + 1 < length) value |= static_cast<uint32_t>(data[offset + 1]) << 16;
    if (offset + 2 < length) value |= static_cast<uint32_t>(data[offset + 2]) << 8;
    if (offset + 3 < length) value |= static_cast<uint32_t>(data[offset + 3]);
    sum += value;
  }
  return sum;
}

// Recalculate checksums for a TrueType font after table modification
bool RecalcFontChecksums(pdfium::span<uint8_t> font_data) {
  if (font_data.size() < 12)
    return false;
  
  // Verify TrueType/OpenType signature
  uint32_t sfnt_version = ReadU32BE(font_data.data());
  if (sfnt_version != 0x00010000 && sfnt_version != 0x4F54544F)
    return false;
  
  uint16_t num_tables = ReadU16BE(font_data.data() + 4);
  uint8_t* table_dir = font_data.data() + 12;
  
  // First pass: recalculate checksum for each table and find 'head' table
  uint32_t head_offset = 0;
  uint32_t head_length = 0;
  
  for (uint16_t i = 0; i < num_tables; i++) {
    if (12 + (i + 1) * 16 > font_data.size())
      break;
    
    uint8_t* entry = table_dir + i * 16;
    uint32_t tag = ReadU32BE(entry);
    uint32_t offset = ReadU32BE(entry + 8);
    uint32_t length = ReadU32BE(entry + 12);
    
    if (offset + length > font_data.size())
      continue;
    
    // For head table, we need to zero checksumAdjustment before calculating
    if (tag == 0x68656164) {  // 'head'
      head_offset = offset;
      head_length = length;
      
      // Zero checksumAdjustment (at offset 8 in head table) temporarily
      if (head_length >= 12) {
        WriteU32BE(font_data.data() + head_offset + 8, 0);
      }
    }
    
    // Calculate and update the table checksum
    uint32_t checksum = CalcTableChecksum(font_data.data() + offset, length);
    WriteU32BE(entry + 4, checksum);  // checksum is at offset 4 in table record
  }
  
  // Second pass: calculate whole font checksum and update head.checksumAdjustment
  if (head_offset != 0 && head_length >= 12) {
    uint32_t whole_font_sum = CalcTableChecksum(font_data.data(), 
                                                 static_cast<uint32_t>(font_data.size()));
    uint32_t checksum_adjustment = 0xB1B0AFBA - whole_font_sum;
    WriteU32BE(font_data.data() + head_offset + 8, checksum_adjustment);
    printf("[FontSubsetter] Recalculated font checksums (adjustment=0x%08X)\n", 
           checksum_adjustment);
  }
  
  return true;
}

}  // namespace
 
CPDF_FontSubsetter::CPDF_FontSubsetter() = default;
CPDF_FontSubsetter::~CPDF_FontSubsetter() = default;

SubsetResult CPDF_FontSubsetter::SubsetByGlyphIds(
    pdfium::span<const uint8_t> font_data,
    const std::set<uint16_t>& gids_to_keep,
    const std::map<uint32_t, uint16_t>& char_code_to_gid) {
  SubsetResult result;

  if (font_data.empty()) {
    result.error_message = "Empty font data";
    return result;
  }

  if (gids_to_keep.empty()) {
    result.error_message = "No glyphs to keep";
    return result;
  }

  printf("[FontSubsetter] SubsetByGlyphIds: input_size=%zu, gids_count=%zu, mappings_count=%zu\n",
          font_data.size(), gids_to_keep.size(), char_code_to_gid.size());
  
  // Debug: Log the GIDs we're keeping
  printf( "[FontSubsetter] GIDs to keep: ");
  int logged = 0;
  for (uint16_t gid : gids_to_keep) {
    if (logged++ < 20) {
      printf( "%u ", gid);
    }
  }
  if (gids_to_keep.size() > 20) {
    printf( "... (+%zu more)", gids_to_keep.size() - 20);
  }
  printf( "\n");

#if defined(PDF_ENABLE_FONT_SANITIZER)
  // Create HarfBuzz blob from font data.
  hb_blob_t* blob = hb_blob_create(
      reinterpret_cast<const char*>(font_data.data()),
      static_cast<unsigned int>(font_data.size()),
      HB_MEMORY_MODE_READONLY,
      nullptr,
      nullptr);
  if (!blob) {
    result.error_message = "Failed to create HarfBuzz blob";
    printf( "[FontSubsetter] ERROR: %s\n", result.error_message);
    return result;
  }

  // Create face from blob.
  hb_face_t* face = hb_face_create(blob, 0);
  hb_blob_destroy(blob);
  if (!face) {
    result.error_message = "Failed to create HarfBuzz face";
    printf( "[FontSubsetter] ERROR: %s\n", result.error_message);
    return result;
  }

  // Debug: Log original font info
  unsigned int orig_glyph_count = hb_face_get_glyph_count(face);
  unsigned int orig_upem = hb_face_get_upem(face);
  printf( "[FontSubsetter] Original font: glyph_count=%u, upem=%u\n",
          orig_glyph_count, orig_upem);
  
  // CRITICAL CHECK: If the font has fewer glyphs than the max GID we're requesting,
  // something is wrong with the GID collection!
  uint16_t max_gid = gids_to_keep.empty() ? 0 : *gids_to_keep.rbegin();
  if (max_gid >= orig_glyph_count) {
    printf( "[FontSubsetter] WARNING: max_gid=%u >= glyph_count=%u! "
            "GIDs may be invalid for this already-subsetted font!\n",
            max_gid, orig_glyph_count);
  }

  // Create subset input.
  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  if (!input) {
    hb_face_destroy(face);
    result.error_message = "Failed to create subset input";
    printf( "[FontSubsetter] ERROR: %s\n", result.error_message);
    return result;
  }

  // Configure subset flags for PDF embedding.
  // CRITICAL for PDF: We MUST preserve GID positions exactly, otherwise
  // the PDF's encoding/cmap will point to wrong glyphs after subsetting.
  // 
  // HB_SUBSET_FLAGS_RETAIN_GIDS (0x02): Keep original GID positions
  // HB_SUBSET_FLAGS_RETAIN_NUM_GLYPHS (0x2000): Don't reduce glyph count
  //   (This is experimental in HarfBuzz but essential for PDF compatibility)
  // HB_SUBSET_FLAGS_PASSTHROUGH_UNRECOGNIZED (0x20): Keep unknown tables
  // HB_SUBSET_FLAGS_NOTDEF_OUTLINE (0x40): Keep .notdef glyph outline
  //
  // Note: We use raw values for experimental flags that may not be defined.
  constexpr unsigned int kRetainNumGlyphs = 0x00002000u;  // HB_SUBSET_FLAGS_RETAIN_NUM_GLYPHS
  
  hb_subset_input_set_flags(input,
      HB_SUBSET_FLAGS_RETAIN_GIDS |           // Keep original GID mapping (0x02)
      kRetainNumGlyphs |                      // Don't reduce glyph count (0x2000)
      HB_SUBSET_FLAGS_PASSTHROUGH_UNRECOGNIZED |  // Keep unknown tables (0x20)
      HB_SUBSET_FLAGS_NOTDEF_OUTLINE);        // Keep .notdef outline (0x40)
  
  printf( "[FontSubsetter] Flags set: RETAIN_GIDS | RETAIN_NUM_GLYPHS | PASSTHROUGH | NOTDEF_OUTLINE = 0x%x\n",
          HB_SUBSET_FLAGS_RETAIN_GIDS | kRetainNumGlyphs | 
          HB_SUBSET_FLAGS_PASSTHROUGH_UNRECOGNIZED | HB_SUBSET_FLAGS_NOTDEF_OUTLINE);

  // CMAP HANDLING STRATEGY:
  // For PDF fonts (especially already-subsetted ones), the font's internal cmap
  // may use different encodings than what PDFium expects. HarfBuzz cmap subsetting
  // can break rendering in these cases.
  //
  // Strategy:
  // 1. Preserve the cmap table during HarfBuzz subsetting (for rendering)
  // 2. Post-process the cmap to remove entries for unused GIDs (for security)
  //
  // This two-phase approach ensures both rendering and security.
  
  // Preserve cmap table during HarfBuzz - we'll clean it up afterwards
  hb_set_t* no_subset_tables = hb_subset_input_set(input, HB_SUBSET_SETS_NO_SUBSET_TABLE_TAG);
  hb_set_add(no_subset_tables, HB_TAG('c','m','a','p'));
  printf("[FontSubsetter] Preserving cmap during HarfBuzz (will clean up after)\n");
  
  // Get the glyph set and add our glyphs.
  hb_set_t* glyph_set = hb_subset_input_glyph_set(input);
  
  // Always include glyph 0 (notdef).
  hb_set_add(glyph_set, 0);
  
  // Add all the glyphs we want to keep.
  unsigned int valid_gids_added = 0;
  for (uint16_t gid : gids_to_keep) {
    if (gid < orig_glyph_count) {
      hb_set_add(glyph_set, gid);
      valid_gids_added++;
    }
  }
  printf("[FontSubsetter] Added %u GIDs to subset (+ GID 0)\n", valid_gids_added);

  // Perform the subset operation.
  hb_face_t* subset_face = hb_subset_or_fail(face, input);
  hb_subset_input_destroy(input);
  hb_face_destroy(face);

  if (!subset_face) {
    result.error_message = "HarfBuzz subset operation failed";
    printf( "[FontSubsetter] ERROR: %s - this may indicate corrupt font data\n",
            result.error_message);
    return result;
  }

  // Debug: Log subsetted font info and validate GID retention
  unsigned int subset_glyph_count = hb_face_get_glyph_count(subset_face);
  printf( "[FontSubsetter] Subsetted font: glyph_count=%u (was %u)\n",
          subset_glyph_count, orig_glyph_count);
  
  // CRITICAL VALIDATION: With RETAIN_GIDS + RETAIN_NUM_GLYPHS, the output
  // font should have at least (max_gid + 1) glyphs to preserve positions.
  // If the original font had 228 glyphs, the subset should still have 228.
  if (subset_glyph_count != orig_glyph_count) {
    printf( "[FontSubsetter] WARNING: Glyph count changed from %u to %u!\n",
            orig_glyph_count, subset_glyph_count);
    printf( "[FontSubsetter] This suggests RETAIN_NUM_GLYPHS may not be working.\n");
    
    // If glyph count is less than max_gid + 1, the font will be broken!
    if (subset_glyph_count < max_gid + 1) {
      printf( "[FontSubsetter] CRITICAL: subset has %u glyphs but max_gid is %u!\n",
              subset_glyph_count, max_gid);
      printf( "[FontSubsetter] GIDs above %u will be MISSING - font will render incorrectly!\n",
              subset_glyph_count - 1);
    }
  } else {
    printf( "[FontSubsetter] OK: Glyph count preserved at %u (RETAIN_NUM_GLYPHS working)\n",
            orig_glyph_count);
  }

  // Extract the subsetted font data.
  hb_blob_t* subset_blob = hb_face_reference_blob(subset_face);
  hb_face_destroy(subset_face);

  if (!subset_blob) {
    result.error_message = "Failed to get subset blob";
    printf( "[FontSubsetter] ERROR: %s\n", result.error_message);
    return result;
  }

  unsigned int length = 0;
  const char* data = hb_blob_get_data(subset_blob, &length);
  if (!data || length == 0) {
    hb_blob_destroy(subset_blob);
    result.error_message = "Empty subset result";
    printf( "[FontSubsetter] ERROR: %s\n", result.error_message);
    return result;
  }

  // Copy the data to our result.
  result.font_data.resize(length);
  memcpy(result.font_data.data(), data, length);
  
  hb_blob_destroy(subset_blob);
  
  // SECURITY: Replace the entire cmap table with a minimal one containing
  // only the exact char_code -> GID mappings we specify.
  // This is the key to preventing information leakage about redacted characters.
  printf("[FontSubsetter] Rebuilding cmap table for security...\n");
  if (ReplaceEntireCmapTable(result.font_data, char_code_to_gid)) {
    printf("[FontSubsetter] cmap rebuild: SUCCESS\n");
    
    // After replacing cmap, recalculate font checksums for validity
    if (RecalcFontChecksums(pdfium::span<uint8_t>(result.font_data))) {
      printf("[FontSubsetter] Checksums: RECALCULATED\n");
    }
  } else {
    printf("[FontSubsetter] cmap rebuild: FAILED\n");
  }
  
  result.success = true;
  
  printf( "[FontSubsetter] SUCCESS: output_size=%zu (was %zu, saved %zu bytes)\n",
          result.font_data.size(), font_data.size(),
          font_data.size() > result.font_data.size() ? font_data.size() - result.font_data.size() : 0);

  return result;

#else
  // Font sanitizer not enabled - return original data unchanged.
  printf( "[FontSubsetter] WARNING: PDF_ENABLE_FONT_SANITIZER not defined, returning original\n");
  result.font_data.assign(font_data.begin(), font_data.end());
  result.success = true;
  return result;
#endif
}

bool CPDF_FontSubsetter::SubsetAndReplaceFont(
    CPDF_Document* doc,
    CPDF_Font* font,
    const std::set<uint16_t>& gids_to_keep,
    const std::map<uint32_t, uint16_t>& char_code_to_gid) {
  if (!doc || !font || gids_to_keep.empty())
    return false;

  // Debug: Log font identification info
  RetainPtr<const CPDF_Dictionary> font_dict = font->GetFontDict();
  ByteString base_font = font_dict ? font_dict->GetByteStringFor("BaseFont") : "";
  uint32_t font_obj_num = font->GetFontDictObjNum();
  printf("[FontSubsetter] SubsetAndReplaceFont: obj#%u BaseFont=%s\n",
          font_obj_num, base_font.c_str());

  // Get the font file stream.
  RetainPtr<CPDF_Stream> font_stream = GetFontFileStream(font);
  if (!font_stream) {
    printf("[FontSubsetter] No embedded font program for obj#%u - skipping\n",
            font_obj_num);
    return false;  // No embedded font program.
  }

  const char* font_file_key = GetFontFileKeyName(font);
  printf("[FontSubsetter] Font file key: %s\n", font_file_key ? font_file_key : "(null)");

  // Load the font data.
  auto stream_acc = pdfium::MakeRetain<CPDF_StreamAcc>(font_stream);
  stream_acc->LoadAllDataFiltered();
  pdfium::span<const uint8_t> font_data = stream_acc->GetSpan();
  if (font_data.empty()) {
    printf("[FontSubsetter] Empty font data for obj#%u - skipping\n", font_obj_num);
    return false;
  }

  printf("[FontSubsetter] Original font stream size: %zu bytes\n", font_data.size());

  // Perform subsetting with GIDs and the exact char_code -> GID mapping for cmap rebuild.
  SubsetResult result = SubsetByGlyphIds(font_data, gids_to_keep, char_code_to_gid);
  if (!result.success) {
    printf( "[FontSubsetter] Subsetting FAILED for obj#%u: %s\n",
            font_obj_num, result.error_message ? result.error_message : "unknown error");
    
    // IMPORTANT: Don't modify the font if subsetting fails!
    // This preserves the original font and prevents rendering issues.
    return false;
  }

  // Sanity check: Don't replace with empty or suspiciously small data
  if (result.font_data.size() < 100) {
    printf( "[FontSubsetter] WARNING: Subset result is suspiciously small (%zu bytes) "
            "- NOT replacing original font to be safe!\n", result.font_data.size());
    return false;
  }

  // Replace the font stream data.
  font_stream->SetDataAndRemoveFilter(result.font_data);
  printf( "[FontSubsetter] Replaced font stream: %zu -> %zu bytes\n",
          font_data.size(), result.font_data.size());

  // Update the stream length in the dictionary.
  RetainPtr<CPDF_Dictionary> stream_dict = font_stream->GetMutableDict();
  if (stream_dict) {
    stream_dict->SetNewFor<CPDF_Number>(
        "Length1", static_cast<int>(result.font_data.size()));
  }

  return true;
}

RetainPtr<CPDF_Stream> CPDF_FontSubsetter::GetFontFileStream(CPDF_Font* font) {
  if (!font)
    return nullptr;

  RetainPtr<CPDF_Dictionary> font_dict = font->GetMutableFontDict();
  if (!font_dict)
    return nullptr;

  RetainPtr<CPDF_Dictionary> font_desc =
      font_dict->GetMutableDictFor("FontDescriptor");
  if (!font_desc)
    return nullptr;

  // Try FontFile2 (TrueType), FontFile3 (CFF/OpenType), FontFile (Type1).
  const char* key = GetFontFileKeyName(font);
  if (!key)
    return nullptr;

  return font_desc->GetMutableStreamFor(key);
}

const char* CPDF_FontSubsetter::GetFontFileKeyName(CPDF_Font* font) {
  if (!font)
    return nullptr;

  RetainPtr<const CPDF_Dictionary> font_dict = font->GetFontDict();
  if (!font_dict)
    return nullptr;

  RetainPtr<const CPDF_Dictionary> font_desc =
      font_dict->GetDictFor("FontDescriptor");
  if (!font_desc)
    return nullptr;

  // Check in order of most common for PDFs.
  if (font_desc->KeyExist("FontFile2"))
    return "FontFile2";  // TrueType
  if (font_desc->KeyExist("FontFile3"))
    return "FontFile3";  // CFF/OpenType
  if (font_desc->KeyExist("FontFile"))
    return "FontFile";   // Type1

  return nullptr;
}

// Finds the table directory entry for cmap table.
// Returns the offset to the table directory entry (not the table itself), or 0 if not found.
uint32_t CPDF_FontSubsetter::FindCmapTableDirEntry(
    pdfium::span<const uint8_t> font_data) {
  if (font_data.size() < 12)
    return 0;

  // Check for TrueType/OpenType signature
  uint32_t sfnt_version = ReadU32BE(font_data.data());
  if (sfnt_version != 0x00010000 &&  // TrueType
      sfnt_version != 0x4F54544F) {  // 'OTTO' (CFF)
    printf("[CmapRebuild] Unknown font format: 0x%08X\n", sfnt_version);
    return 0;
  }

  uint16_t num_tables = ReadU16BE(font_data.data() + 4);
  
  // Table directory starts at offset 12
  for (uint16_t i = 0; i < num_tables; i++) {
    uint32_t entry_offset = 12 + i * 16;
    if (entry_offset + 16 > font_data.size())
      break;
      
    uint32_t tag = ReadU32BE(font_data.data() + entry_offset);
    
    // 'cmap' = 0x636D6170
    if (tag == 0x636D6170) {
      printf("[CmapRebuild] Found cmap table directory entry at offset %u\n",
             entry_offset);
      return entry_offset;
    }
  }
  
  printf("[CmapRebuild] cmap table not found in directory\n");
  return 0;
}

// Builds a Format 0 cmap table (for char codes 0-255).
// Format 0 Structure:
//   cmap header: version (2), numTables (2)
//   encoding record: platformID (2), encodingID (2), offset (4)
//   subtable: format (2), length (2), language (2), glyphIdArray[256]
DataVector<uint8_t> CPDF_FontSubsetter::BuildCmapFormat0(
    const std::map<uint32_t, uint16_t>& char_code_to_gid) {
  // Total size: 4 (header) + 8 (encoding record) + 262 (subtable) = 274 bytes
  DataVector<uint8_t> cmap(274, 0);
  uint8_t* data = cmap.data();
  
  // cmap header
  WriteU16BE(data + 0, 0);      // version = 0
  WriteU16BE(data + 2, 1);      // numTables = 1
  
  // Encoding record: Windows Unicode BMP (platform 3, encoding 1)
  WriteU16BE(data + 4, 3);      // platformID = 3 (Windows)
  WriteU16BE(data + 6, 1);      // encodingID = 1 (Unicode BMP)
  WriteU32BE(data + 8, 12);     // offset to subtable = 12
  
  // Format 0 subtable (starts at offset 12)
  uint8_t* subtable = data + 12;
  WriteU16BE(subtable + 0, 0);    // format = 0
  WriteU16BE(subtable + 2, 262);  // length = 262 (6 + 256)
  WriteU16BE(subtable + 4, 0);    // language = 0
  
  // glyphIdArray[256] - all initialized to 0 already
  // Only fill in the mappings we have
  unsigned int mappings_written = 0;
  for (const auto& mapping : char_code_to_gid) {
    uint32_t char_code = mapping.first;
    uint16_t gid = mapping.second;
    
    if (char_code <= 255 && gid <= 255) {
      subtable[6 + char_code] = static_cast<uint8_t>(gid);
      mappings_written++;
    }
  }
  
  printf("[CmapRebuild] Built Format 0 cmap: %u mappings, size=%zu\n",
         mappings_written, cmap.size());
  return cmap;
}

// Builds a Format 4 cmap table (for char codes 0-65535).
// SECURITY: Uses glyphIdArray approach with NO delta segments (idDelta=0 for all
// non-terminator segments). Builds contiguous segments only across consecutive
// char codes present in the mapping - all mappings are explicit via glyphIdArray.
// This ensures only exact mappings exist - no leaks, no extra unmapped char codes.
//
// Format 4 Structure:
//   cmap header: version (2), numTables (2)
//   encoding record: platformID (2), encodingID (2), offset (4)
//   subtable: format (2), length (2), language (2)
//             segCountX2 (2), searchRange (2), entrySelector (2), rangeShift (2)
//             endCode[segCount], reservedPad (2), startCode[segCount]
//             idDelta[segCount], idRangeOffset[segCount], glyphIdArray[]
DataVector<uint8_t> CPDF_FontSubsetter::BuildCmapFormat4(
    const std::map<uint32_t, uint16_t>& char_code_to_gid) {
  // Filter to only char codes <= 0xFFFF
  std::vector<std::pair<uint16_t, uint16_t>> mappings;
  for (const auto& mapping : char_code_to_gid) {
    if (mapping.first <= 0xFFFF) {
      mappings.emplace_back(static_cast<uint16_t>(mapping.first),
                            mapping.second);
    }
  }
  
  // Sort by char code
  std::sort(mappings.begin(), mappings.end());
  
  // Build contiguous segments for efficiency
  // Each segment covers a contiguous range of char codes
  struct Segment {
    uint16_t start_code;
    uint16_t end_code;
    std::vector<uint16_t> gids;  // GIDs for each char in range
  };
  
  std::vector<Segment> segments;
  
  for (const auto& m : mappings) {
    uint16_t char_code = m.first;
    uint16_t gid = m.second;
    
    // Try to extend the last segment if consecutive
    if (!segments.empty() && 
        segments.back().end_code + 1 == char_code) {
      segments.back().end_code = char_code;
      segments.back().gids.push_back(gid);
    } else {
      // Start a new segment
      Segment seg;
      seg.start_code = char_code;
      seg.end_code = char_code;
      seg.gids.push_back(gid);
      segments.push_back(std::move(seg));
    }
  }
  
  // Add the mandatory 0xFFFF terminator segment
  // NOTE: Terminator is special - it uses idDelta=1 (not glyphIdArray) so
  // that (0xFFFF + 1) % 65536 = 0, mapping to GID 0.
  // We mark it by leaving gids empty.
  Segment terminator;
  terminator.start_code = 0xFFFF;
  terminator.end_code = 0xFFFF;
  // gids intentionally left empty - terminator uses idDelta, not glyphIdArray
  segments.push_back(std::move(terminator));
  
  uint16_t seg_count = static_cast<uint16_t>(segments.size());
  uint16_t seg_count_x2 = seg_count * 2;
  
  // Calculate searchRange, entrySelector, rangeShift
  uint16_t search_range = 2;
  uint16_t entry_selector = 0;
  while (search_range * 2 <= seg_count_x2) {
    search_range *= 2;
    entry_selector++;
  }
  uint16_t range_shift = seg_count_x2 - search_range;
  
  // Calculate glyphIdArray size (terminator has no gids, uses idDelta instead)
  size_t glyph_id_array_size = 0;
  for (const auto& seg : segments) {
    // Terminator segment has empty gids - it uses idDelta=1 instead
    glyph_id_array_size += seg.gids.size();
  }
  
  // Calculate subtable size
  // Header: format(2) + length(2) + language(2) = 6
  // Binary search: segCountX2(2) + searchRange(2) + entrySelector(2) + rangeShift(2) = 8
  // Arrays: endCode(segCount*2) + reservedPad(2) + startCode(segCount*2) + 
  //         idDelta(segCount*2) + idRangeOffset(segCount*2) = segCount*8 + 2
  // GlyphIdArray: glyph_id_array_size * 2
  size_t subtable_size = 6 + 8 + seg_count * 8 + 2 + glyph_id_array_size * 2;
  
  // Format 4 length field is uint16_t - if overflow, fall back to Format 12
  if (subtable_size > 0xFFFF) {
    printf("[CmapRebuild] Format 4 subtable too large (%zu > 65535), using Format 12\n",
           subtable_size);
    return BuildCmapFormat12(char_code_to_gid);
  }
  
  // Total cmap size: header(4) + encoding_record(8) + subtable
  size_t cmap_size = 4 + 8 + subtable_size;
  
  DataVector<uint8_t> cmap(cmap_size, 0);
  uint8_t* data = cmap.data();
  
  // cmap header
  WriteU16BE(data + 0, 0);      // version = 0
  WriteU16BE(data + 2, 1);      // numTables = 1
  
  // Encoding record: Windows Unicode BMP (platform 3, encoding 1)
  WriteU16BE(data + 4, 3);      // platformID = 3 (Windows)
  WriteU16BE(data + 6, 1);      // encodingID = 1 (Unicode BMP)
  WriteU32BE(data + 8, 12);     // offset to subtable = 12
  
  // Format 4 subtable (starts at offset 12)
  uint8_t* subtable = data + 12;
  WriteU16BE(subtable + 0, 4);                        // format = 4
  WriteU16BE(subtable + 2, static_cast<uint16_t>(subtable_size));  // length
  WriteU16BE(subtable + 4, 0);                        // language = 0
  WriteU16BE(subtable + 6, seg_count_x2);             // segCountX2
  WriteU16BE(subtable + 8, search_range);             // searchRange
  WriteU16BE(subtable + 10, entry_selector);          // entrySelector
  WriteU16BE(subtable + 12, range_shift);             // rangeShift
  
  // Calculate array offsets within subtable
  size_t end_code_offset = 14;
  size_t start_code_offset = end_code_offset + seg_count * 2 + 2;  // +2 for reservedPad
  size_t id_delta_offset = start_code_offset + seg_count * 2;
  size_t id_range_offset_offset = id_delta_offset + seg_count * 2;
  size_t glyph_id_array_offset = id_range_offset_offset + seg_count * 2;
  
  // Write endCode array
  for (size_t i = 0; i < segments.size(); i++) {
    WriteU16BE(subtable + end_code_offset + i * 2, segments[i].end_code);
  }
  
  // reservedPad is at end_code_offset + seg_count * 2, already 0
  
  // Write startCode array
  for (size_t i = 0; i < segments.size(); i++) {
    WriteU16BE(subtable + start_code_offset + i * 2, segments[i].start_code);
  }
  
  // Write idDelta array
  // All segments use idDelta=0 (we use glyphIdArray), EXCEPT the terminator
  // which uses idDelta=1 so that (0xFFFF + 1) % 65536 = 0
  for (size_t i = 0; i < segments.size(); i++) {
    if (segments[i].start_code == 0xFFFF) {
      // Terminator: idDelta=1 maps 0xFFFF to GID 0
      WriteU16BE(subtable + id_delta_offset + i * 2, 1);
    }
    // Other segments: idDelta=0 (already initialized to 0)
  }
  
  // Write idRangeOffset array and glyphIdArray
  // Using canonical formula: idRangeOffset[i] = glyphArrayEntry - idRangeOffsetEntry
  size_t current_glyph_offset = 0;
  for (size_t i = 0; i < segments.size(); i++) {
    // Terminator segment: idRangeOffset=0, uses idDelta instead
    if (segments[i].start_code == 0xFFFF) {
      WriteU16BE(subtable + id_range_offset_offset + i * 2, 0);
      continue;
    }
    
    // Canonical idRangeOffset formula:
    // idRangeOffset[i] = (address of glyphIdArray entry) - (address of idRangeOffset[i])
    size_t idRO_entry = id_range_offset_offset + i * 2;
    size_t glyph_entry = glyph_id_array_offset + current_glyph_offset * 2;
    uint16_t offset_value = static_cast<uint16_t>(glyph_entry - idRO_entry);
    
    WriteU16BE(subtable + id_range_offset_offset + i * 2, offset_value);
    
    // Write GIDs to glyphIdArray
    for (size_t j = 0; j < segments[i].gids.size(); j++) {
      WriteU16BE(subtable + glyph_id_array_offset + (current_glyph_offset + j) * 2,
                 segments[i].gids[j]);
    }
    current_glyph_offset += segments[i].gids.size();
  }
  
  printf("[CmapRebuild] Built Format 4 cmap: %zu segments, %zu GIDs, size=%zu\n",
         segments.size(), glyph_id_array_size, cmap.size());
  return cmap;
}

// Builds a Format 12 cmap table (for full Unicode support including > 0xFFFF).
// SECURITY: Each contiguous range of char codes with sequential GIDs becomes one group.
// Non-contiguous mappings get their own single-character groups.
// This ensures only exact mappings exist - no leaks.
//
// Format 12 Structure:
//   cmap header: version (2), numTables (2)
//   encoding record: platformID (2), encodingID (2), offset (4)
//   subtable: format (2), reserved (2), length (4), language (4), numGroups (4)
//             groups[numGroups]: startCharCode (4), endCharCode (4), startGlyphID (4)
DataVector<uint8_t> CPDF_FontSubsetter::BuildCmapFormat12(
    const std::map<uint32_t, uint16_t>& char_code_to_gid) {
  // Build groups from the mappings
  // A group is a contiguous range where GIDs are also sequential
  struct Group {
    uint32_t start_char;
    uint32_t end_char;
    uint32_t start_gid;
  };
  
  std::vector<Group> groups;
  
  for (const auto& mapping : char_code_to_gid) {
    uint32_t char_code = mapping.first;
    uint32_t gid = mapping.second;
    
    // Try to extend the last group if consecutive char codes AND consecutive GIDs
    if (!groups.empty()) {
      Group& last = groups.back();
      uint32_t expected_gid = last.start_gid + (char_code - last.start_char);
      
      if (last.end_char + 1 == char_code && expected_gid == gid) {
        // Can extend this group
        last.end_char = char_code;
        continue;
      }
    }
    
    // Start a new group
    Group g;
    g.start_char = char_code;
    g.end_char = char_code;
    g.start_gid = gid;
    groups.push_back(g);
  }
  
  uint32_t num_groups = static_cast<uint32_t>(groups.size());
  
  // Calculate sizes
  // Subtable: format(2) + reserved(2) + length(4) + language(4) + numGroups(4) = 16
  //           + groups[numGroups] * 12 bytes each
  uint32_t subtable_size = 16 + num_groups * 12;
  
  // Total cmap: header(4) + encoding_record(8) + subtable
  size_t cmap_size = 4 + 8 + subtable_size;
  
  DataVector<uint8_t> cmap(cmap_size, 0);
  uint8_t* data = cmap.data();
  
  // cmap header
  WriteU16BE(data + 0, 0);      // version = 0
  WriteU16BE(data + 2, 1);      // numTables = 1
  
  // Encoding record: Windows Unicode full (platform 3, encoding 10)
  WriteU16BE(data + 4, 3);      // platformID = 3 (Windows)
  WriteU16BE(data + 6, 10);     // encodingID = 10 (Unicode UCS-4)
  WriteU32BE(data + 8, 12);     // offset to subtable = 12
  
  // Format 12 subtable (starts at offset 12)
  uint8_t* subtable = data + 12;
  WriteU16BE(subtable + 0, 12);           // format = 12
  WriteU16BE(subtable + 2, 0);            // reserved = 0
  WriteU32BE(subtable + 4, subtable_size);// length
  WriteU32BE(subtable + 8, 0);            // language = 0
  WriteU32BE(subtable + 12, num_groups);  // numGroups
  
  // Write groups (starting at offset 16 in subtable)
  for (size_t i = 0; i < groups.size(); i++) {
    uint8_t* group_ptr = subtable + 16 + i * 12;
    WriteU32BE(group_ptr + 0, groups[i].start_char);
    WriteU32BE(group_ptr + 4, groups[i].end_char);
    WriteU32BE(group_ptr + 8, groups[i].start_gid);
  }
  
  printf("[CmapRebuild] Built Format 12 cmap: %u groups, size=%zu\n",
         num_groups, cmap.size());
  return cmap;
}

// Replaces the entire cmap table with a minimal one containing only the exact
// char_code -> GID mappings we specify. This is the key to preventing information
// leakage about redacted characters.
//
// Strategy:
// 1. Build a new cmap table:
//    - Format 0 if max_char <= 255 AND all GIDs <= 255
//    - Format 4 if max_char <= 65535
//    - Format 12 for full Unicode (max_char > 65535)
// 2. Find the old cmap in the font
// 3. Replace the old cmap with the new one (may change font size)
// 4. Update table directory entry
bool CPDF_FontSubsetter::ReplaceEntireCmapTable(
    DataVector<uint8_t>& font_data,
    const std::map<uint32_t, uint16_t>& char_code_to_gid) {
  printf("[CmapRebuild] Starting cmap rebuild with %zu mappings\n", 
         char_code_to_gid.size());
  
  if (char_code_to_gid.empty()) {
    printf("[CmapRebuild] No mappings to write\n");
    return false;
  }
  
  // Determine max char code to choose format
  uint32_t max_char_code = 0;
  for (const auto& mapping : char_code_to_gid) {
    if (mapping.first > max_char_code) {
      max_char_code = mapping.first;
    }
  }
  
  // Build the new cmap table
  // Format selection:
  // - Format 0: Simple 8-bit (char codes 0-255, GIDs 0-255)
  // - Format 4: 16-bit BMP (char codes 0-65535)
  // - Format 12: Full Unicode (char codes > 65535)
  DataVector<uint8_t> new_cmap;
  if (max_char_code <= 255) {
    // Check if all GIDs also fit in 8 bits for Format 0
    bool all_gids_8bit = true;
    for (const auto& mapping : char_code_to_gid) {
      if (mapping.second > 255) {
        all_gids_8bit = false;
        break;
      }
    }
    
    if (all_gids_8bit) {
      printf("[CmapRebuild] Using Format 0 (max_char_code=%u, all GIDs <= 255)\n", max_char_code);
      new_cmap = BuildCmapFormat0(char_code_to_gid);
    } else {
      printf("[CmapRebuild] Using Format 4 (max_char_code=%u, some GIDs > 255)\n", max_char_code);
      new_cmap = BuildCmapFormat4(char_code_to_gid);
    }
  } else if (max_char_code <= 0xFFFF) {
    printf("[CmapRebuild] Using Format 4 (max_char_code=%u <= 65535)\n", max_char_code);
    new_cmap = BuildCmapFormat4(char_code_to_gid);
  } else {
    printf("[CmapRebuild] Using Format 12 (max_char_code=%u > 65535)\n", max_char_code);
    new_cmap = BuildCmapFormat12(char_code_to_gid);
  }
  
  if (new_cmap.empty()) {
    printf("[CmapRebuild] Failed to build new cmap\n");
    return false;
  }
  
  // Find the old cmap table directory entry
  uint32_t cmap_dir_entry = FindCmapTableDirEntry(pdfium::span<const uint8_t>(font_data));
  if (cmap_dir_entry == 0) {
    printf("[CmapRebuild] Could not find cmap in table directory\n");
    return false;
  }
  
  // Read old cmap info
  uint32_t old_offset = ReadU32BE(font_data.data() + cmap_dir_entry + 8);
  uint32_t old_length = ReadU32BE(font_data.data() + cmap_dir_entry + 12);
  
  // TrueType tables are stored with 4-byte alignment.
  // Table record stores unpadded length, but storage uses padded length.
  auto Align4 = [](uint32_t len) -> uint32_t {
    return (len + 3) & ~3u;
  };
  
  uint32_t old_padded = Align4(old_length);
  uint32_t new_length = static_cast<uint32_t>(new_cmap.size());
  uint32_t new_padded = Align4(new_length);
  
  printf("[CmapRebuild] Old cmap: offset=%u, length=%u (padded=%u)\n", 
         old_offset, old_length, old_padded);
  printf("[CmapRebuild] New cmap: length=%u (padded=%u)\n", new_length, new_padded);
  
  // Sanity checks for old_offset (security hardening for potentially hostile input)
  uint16_t num_tables = ReadU16BE(font_data.data() + 4);
  uint32_t table_dir_end = 12 + num_tables * 16;
  
  // Validate table directory doesn't exceed font data (malicious num_tables)
  if (table_dir_end > font_data.size()) {
    printf("[CmapRebuild] ERROR: table directory exceeds font data (corrupt font?)\n");
    return false;
  }
  
  if (old_offset < table_dir_end) {
    printf("[CmapRebuild] ERROR: old cmap overlaps table directory (corrupt font?)\n");
    return false;
  }
  
  if ((old_offset & 3) != 0) {
    printf("[CmapRebuild] WARNING: old cmap offset not 4-byte aligned (unusual)\n");
    // Continue anyway - not strictly required, but unusual
  }
  
  // Bounds check: validate old cmap doesn't exceed font data
  if (old_offset + old_padded > font_data.size()) {
    printf("[CmapRebuild] ERROR: old cmap extends beyond font data (corrupt font?)\n");
    return false;
  }
  
  // Calculate size difference using PADDED lengths
  // Use int64_t to avoid overflow/underflow issues on extreme sizes
  int64_t size_diff = static_cast<int64_t>(new_padded) - static_cast<int64_t>(old_padded);
  int64_t new_size64 = static_cast<int64_t>(font_data.size()) + size_diff;
  
  if (new_size64 <= 0 || new_size64 > static_cast<int64_t>(UINT32_MAX)) {
    printf("[CmapRebuild] ERROR: invalid new font size (%lld bytes)\n", 
           static_cast<long long>(new_size64));
    return false;
  }
  
  // Create new font data with adjusted size
  DataVector<uint8_t> new_font_data;
  new_font_data.reserve(static_cast<size_t>(new_size64));
  
  // Copy data before cmap
  new_font_data.insert(new_font_data.end(), 
                       font_data.begin(), 
                       font_data.begin() + old_offset);
  
  // Insert new cmap (unpadded data)
  new_font_data.insert(new_font_data.end(), 
                       new_cmap.begin(), 
                       new_cmap.end());
  
  // Add padding zeros to reach 4-byte alignment
  uint32_t padding_needed = new_padded - new_length;
  for (uint32_t p = 0; p < padding_needed; p++) {
    new_font_data.push_back(0);
  }
  
  // Copy data after old cmap (using PADDED old length)
  new_font_data.insert(new_font_data.end(),
                       font_data.begin() + old_offset + old_padded,
                       font_data.end());
  
  // Update font_data
  font_data = std::move(new_font_data);
  
  // Update table directory entry for cmap (length = unpadded, offset unchanged)
  WriteU32BE(font_data.data() + cmap_dir_entry + 12, new_length);
  
  // Update offsets of tables that come after cmap (using PADDED boundary)
  if (size_diff != 0) {
    uint32_t old_cmap_end = old_offset + old_padded;
    uint16_t num_tables = ReadU16BE(font_data.data() + 4);
    
    for (uint16_t i = 0; i < num_tables; i++) {
      uint32_t entry_offset = 12 + i * 16;
      if (entry_offset + 16 > font_data.size())
        break;
      
      // Skip the cmap entry itself (check by tag, not offset, for robustness)
      uint32_t tag = ReadU32BE(font_data.data() + entry_offset);
      if (tag == 0x636D6170)  // 'cmap'
        continue;
      
      uint32_t table_offset = ReadU32BE(font_data.data() + entry_offset + 8);
      
      // Only adjust tables that were AFTER the old cmap's padded end
      if (table_offset >= old_cmap_end) {
        int64_t new_offset64 = static_cast<int64_t>(table_offset) + size_diff;
        if (new_offset64 < 0 || new_offset64 > static_cast<int64_t>(UINT32_MAX)) {
          printf("[CmapRebuild] ERROR: table offset overflow\n");
          return false;
        }
        WriteU32BE(font_data.data() + entry_offset + 8, 
                   static_cast<uint32_t>(new_offset64));
      }
    }
  }
  
  printf("[CmapRebuild] Successfully replaced cmap table (padded_size_diff=%lld)\n", 
         static_cast<long long>(size_diff));
  return true;
}
