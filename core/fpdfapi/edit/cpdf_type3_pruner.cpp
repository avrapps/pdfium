// Copyright 2025 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/edit/cpdf_type3_pruner.h"

#include <vector>

#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/font/cpdf_fontencoding.h"
#include "core/fpdfapi/font/cpdf_type3font.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_name.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fxge/fx_font.h"

CPDF_Type3Pruner::CPDF_Type3Pruner() = default;
CPDF_Type3Pruner::~CPDF_Type3Pruner() = default;

bool CPDF_Type3Pruner::PruneUnusedCharProcs(
    CPDF_Font* font,
    const std::set<uint32_t>& used_char_codes) {
  fprintf(stderr, "[Type3Pruner] PruneUnusedCharProcs called, used_char_codes: %zu\n", 
          used_char_codes.size());
          
  if (!font || !font->IsType3Font()) {
    fprintf(stderr, "[Type3Pruner] Not a Type3 font\n");
    return false;
  }

  CPDF_Type3Font* type3_font = font->AsType3Font();
  if (!type3_font) {
    fprintf(stderr, "[Type3Pruner] AsType3Font failed\n");
    return false;
  }

  RetainPtr<CPDF_Dictionary> font_dict = font->GetMutableFontDict();
  if (!font_dict) {
    fprintf(stderr, "[Type3Pruner] No font dict\n");
    return false;
  }

  RetainPtr<CPDF_Dictionary> char_procs =
      font_dict->GetMutableDictFor("CharProcs");
  if (!char_procs) {
    fprintf(stderr, "[Type3Pruner] No CharProcs\n");
    return false;
  }

  // Count CharProcs
  size_t total_charprocs = 0;
  {
    CPDF_DictionaryLocker locker(char_procs);
    for (const auto& entry : locker) {
      total_charprocs++;
    }
  }
  fprintf(stderr, "[Type3Pruner] Total CharProcs in font: %zu\n", total_charprocs);

  // Build set of glyph names that should be retained.
  std::set<ByteString> used_names;
  for (uint32_t char_code : used_char_codes) {
    const char* name = GetGlyphName(type3_font, char_code);
    if (name && name[0] != '\0') {
      used_names.insert(ByteString(name));
      fprintf(stderr, "[Type3Pruner] Used char_code %u -> glyph name '%s'\n", 
              char_code, name);
    } else {
      fprintf(stderr, "[Type3Pruner] char_code %u has no glyph name\n", char_code);
    }
  }

  // Always keep .notdef if present.
  used_names.insert(".notdef");
  
  fprintf(stderr, "[Type3Pruner] Keeping %zu glyph names\n", used_names.size());

  // Find CharProcs entries to remove.
  std::vector<ByteString> keys_to_remove;
  {
    CPDF_DictionaryLocker locker(char_procs);
    for (const auto& entry : locker) {
      if (used_names.find(entry.first) == used_names.end()) {
        keys_to_remove.push_back(entry.first);
        fprintf(stderr, "[Type3Pruner] Will remove CharProc: %s\n", 
                entry.first.c_str());
      }
    }
  }

  fprintf(stderr, "[Type3Pruner] Removing %zu CharProcs\n", keys_to_remove.size());

  // Remove unused CharProcs.
  for (const ByteString& key : keys_to_remove) {
    char_procs->RemoveFor(key.AsStringView());
  }

  // Also update the Encoding/Differences array to remove unused entries.
  // This is optional but keeps the font dictionary clean.
  RetainPtr<CPDF_Dictionary> encoding = font_dict->GetMutableDictFor("Encoding");
  if (encoding) {
    RetainPtr<CPDF_Array> differences =
        encoding->GetMutableArrayFor("Differences");
    if (differences) {
      // Rebuild the Differences array with only used mappings.
      auto new_differences = pdfium::MakeRetain<CPDF_Array>();
      
      int current_code = -1;
      bool need_code_marker = true;
      
      for (size_t i = 0; i < differences->size(); ++i) {
        RetainPtr<const CPDF_Object> obj = differences->GetObjectAt(i);
        if (!obj)
          continue;

        if (obj->IsNumber()) {
          current_code = obj->GetInteger();
          need_code_marker = true;
        } else if (obj->IsName()) {
          ByteString name = obj->GetString();
          if (used_names.find(name) != used_names.end()) {
            if (need_code_marker) {
              new_differences->AppendNew<CPDF_Number>(current_code);
              need_code_marker = false;
            }
            new_differences->AppendNew<CPDF_Name>(name);
          } else {
            // We skipped a glyph, so we need a new code marker for the next one
            need_code_marker = true;
          }
          current_code++;
        }
      }

      encoding->SetFor("Differences", std::move(new_differences));
    }
  }

  // Update FirstChar, LastChar, and Widths to only include the used range.
  // Also zero out widths for removed char codes.
  if (!used_char_codes.empty()) {
    uint32_t min_code = *used_char_codes.begin();
    uint32_t max_code = *used_char_codes.rbegin();
    
    // Get the existing Widths array.
    RetainPtr<const CPDF_Array> old_widths = font_dict->GetArrayFor("Widths");
    int old_first_char = font_dict->GetIntegerFor("FirstChar");
    
    if (old_widths && min_code >= static_cast<uint32_t>(old_first_char)) {
      // Create new Widths array for the range [min_code, max_code].
      auto new_widths = pdfium::MakeRetain<CPDF_Array>();
      
      for (uint32_t code = min_code; code <= max_code; ++code) {
        // Only keep width if this char code is still used
        if (used_char_codes.count(code) > 0) {
          size_t old_index = code - old_first_char;
          if (old_index < old_widths->size()) {
            float width = old_widths->GetFloatAt(old_index);
            new_widths->AppendNew<CPDF_Number>(width);
          } else {
            new_widths->AppendNew<CPDF_Number>(0);
          }
        } else {
          // Char code was removed, zero out its width
          new_widths->AppendNew<CPDF_Number>(0);
        }
      }
      
      // Update the font dictionary.
      font_dict->SetNewFor<CPDF_Number>("FirstChar", static_cast<int>(min_code));
      font_dict->SetNewFor<CPDF_Number>("LastChar", static_cast<int>(max_code));
      font_dict->SetFor("Widths", std::move(new_widths));
      
      fprintf(stderr, "[Type3Pruner] Updated Widths: FirstChar=%u, LastChar=%u, entries=%u\n",
              min_code, max_code, max_code - min_code + 1);
    }
  }

  return !keys_to_remove.empty();
}

const char* CPDF_Type3Pruner::GetGlyphName(CPDF_Type3Font* font,
                                           uint32_t char_code) {
  if (!font || char_code > 255)
    return nullptr;

  // Type3 fonts use the encoding to map character codes to glyph names.
  // The GetAdobeCharName function handles the encoding lookup.
  RetainPtr<const CPDF_Dictionary> font_dict = font->GetFontDict();
  if (!font_dict)
    return nullptr;

  // Get the encoding information.
  RetainPtr<const CPDF_Object> encoding_obj =
      font_dict->GetDirectObjectFor("Encoding");
  if (!encoding_obj)
    return nullptr;

  // For Type3 fonts, we need to look up the glyph name through the
  // font's internal encoding mechanism.
  // The CPDF_Font::GetAdobeCharName handles this.
  
  // Since we can't easily access the internal encoding arrays,
  // we use the font's character name mechanism.
  // For Type3, the LoadChar function uses GetAdobeCharName internally.
  
  // Try to get the name from the encoding Differences array.
  if (encoding_obj->IsDictionary()) {
    const CPDF_Dictionary* enc_dict = encoding_obj->AsDictionary();
    RetainPtr<const CPDF_Array> differences = enc_dict->GetArrayFor("Differences");
    if (differences) {
      int current_code = 0;
      for (size_t i = 0; i < differences->size(); ++i) {
        RetainPtr<const CPDF_Object> item = differences->GetObjectAt(i);
        if (!item)
          continue;
        if (item->IsNumber()) {
          current_code = item->GetInteger();
        } else if (item->IsName()) {
          if (static_cast<uint32_t>(current_code) == char_code) {
            // Return the name - note: this returns a pointer to internal storage.
            static thread_local ByteString s_name;
            s_name = item->GetString();
            return s_name.c_str();
          }
          current_code++;
        }
      }
    }
  }

  return nullptr;
}
