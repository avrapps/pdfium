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

CPDF_FontSubsetter::CPDF_FontSubsetter() = default;
CPDF_FontSubsetter::~CPDF_FontSubsetter() = default;

SubsetResult CPDF_FontSubsetter::SubsetByGlyphIds(
    pdfium::span<const uint8_t> font_data,
    const std::set<uint16_t>& gids_to_keep) {
  SubsetResult result;

  if (font_data.empty()) {
    result.error_message = "Empty font data";
    return result;
  }

  if (gids_to_keep.empty()) {
    result.error_message = "No glyphs to keep";
    return result;
  }

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
    return result;
  }

  // Create face from blob.
  hb_face_t* face = hb_face_create(blob, 0);
  hb_blob_destroy(blob);
  if (!face) {
    result.error_message = "Failed to create HarfBuzz face";
    return result;
  }

  // Create subset input.
  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  if (!input) {
    hb_face_destroy(face);
    result.error_message = "Failed to create subset input";
    return result;
  }

  // Configure subset flags for PDF embedding.
  hb_subset_input_set_flags(input,
      HB_SUBSET_FLAGS_RETAIN_GIDS |           // Keep original GID mapping
      HB_SUBSET_FLAGS_PASSTHROUGH_UNRECOGNIZED |  // Keep unknown tables
      HB_SUBSET_FLAGS_DESUBROUTINIZE);        // Simplify CFF subroutines

  // Get the glyph set and add our glyphs.
  hb_set_t* glyph_set = hb_subset_input_glyph_set(input);
  
  // Always include glyph 0 (notdef).
  hb_set_add(glyph_set, 0);
  
  // Add all the glyphs we want to keep.
  for (uint16_t gid : gids_to_keep) {
    hb_set_add(glyph_set, gid);
  }

  // Perform the subset operation.
  hb_face_t* subset_face = hb_subset_or_fail(face, input);
  hb_subset_input_destroy(input);
  hb_face_destroy(face);

  if (!subset_face) {
    result.error_message = "HarfBuzz subset operation failed";
    return result;
  }

  // Extract the subsetted font data.
  hb_blob_t* subset_blob = hb_face_reference_blob(subset_face);
  hb_face_destroy(subset_face);

  if (!subset_blob) {
    result.error_message = "Failed to get subset blob";
    return result;
  }

  unsigned int length = 0;
  const char* data = hb_blob_get_data(subset_blob, &length);
  if (!data || length == 0) {
    hb_blob_destroy(subset_blob);
    result.error_message = "Empty subset result";
    return result;
  }

  // Copy the data to our result.
  result.font_data.resize(length);
  memcpy(result.font_data.data(), data, length);
  result.success = true;

  hb_blob_destroy(subset_blob);
  return result;

#else
  // Font sanitizer not enabled - return original data unchanged.
  result.font_data.assign(font_data.begin(), font_data.end());
  result.success = true;
  return result;
#endif
}

bool CPDF_FontSubsetter::SubsetAndReplaceFont(
    CPDF_Document* doc,
    CPDF_Font* font,
    const std::set<uint16_t>& gids_to_keep) {
  if (!doc || !font || gids_to_keep.empty())
    return false;

  // Get the font file stream.
  RetainPtr<CPDF_Stream> font_stream = GetFontFileStream(font);
  if (!font_stream)
    return false;  // No embedded font program.

  // Load the font data.
  auto stream_acc = pdfium::MakeRetain<CPDF_StreamAcc>(font_stream);
  stream_acc->LoadAllDataFiltered();
  pdfium::span<const uint8_t> font_data = stream_acc->GetSpan();
  if (font_data.empty())
    return false;

  // Perform subsetting.
  SubsetResult result = SubsetByGlyphIds(font_data, gids_to_keep);
  if (!result.success)
    return false;

  // Replace the font stream data.
  font_stream->SetDataAndRemoveFilter(result.font_data);

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
