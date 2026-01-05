// Copyright 2025 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/edit/cpdf_font_sanitizer.h"

#include <utility>

#include "core/fpdfapi/edit/cpdf_font_subsetter.h"
#include "core/fpdfapi/edit/cpdf_font_usage_collector.h"
#include "core/fpdfapi/edit/cpdf_tounicode_builder.h"
#include "core/fpdfapi/edit/cpdf_type3_pruner.h"
#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_number.h"

namespace {

// Sanitize the Widths array for simple (non-CID) fonts by zeroing out
// widths for char codes that are no longer used.
bool SanitizeSimpleFontWidths(CPDF_Font* font,
                               const std::set<uint32_t>& used_char_codes) {
  if (!font)
    return false;

  RetainPtr<CPDF_Dictionary> font_dict = font->GetMutableFontDict();
  if (!font_dict)
    return false;

  RetainPtr<CPDF_Array> widths = font_dict->GetMutableArrayFor("Widths");
  if (!widths || widths->IsEmpty())
    return false;

  int first_char = font_dict->GetIntegerFor("FirstChar");
  int last_char = font_dict->GetIntegerFor("LastChar");

  bool modified = false;
  
  // Zero out widths for unused char codes
  for (int char_code = first_char; char_code <= last_char; ++char_code) {
    size_t index = char_code - first_char;
    if (index >= widths->size())
      break;

    // If this char code is not in the used set, zero its width
    if (used_char_codes.count(char_code) == 0) {
      float current_width = widths->GetFloatAt(index);
      if (current_width != 0) {
        widths->SetNewAt<CPDF_Number>(index, 0);
        modified = true;
      }
    }
  }

  if (modified) {
    printf( "[FontSanitizer] Zeroed unused widths for font\n");
  }

  return modified;
}

}  // namespace

CPDF_FontSanitizer::CPDF_FontSanitizer(CPDF_Document* doc)
    : document_(doc),
      usage_collector_(std::make_unique<CPDF_FontUsageCollector>(doc)),
      subsetter_(std::make_unique<CPDF_FontSubsetter>()),
      tounicode_builder_(std::make_unique<CPDF_ToUnicodeBuilder>()),
      type3_pruner_(std::make_unique<CPDF_Type3Pruner>()) {}

CPDF_FontSanitizer::~CPDF_FontSanitizer() = default;

FontSanitizerResult CPDF_FontSanitizer::Sanitize() {
  FontSanitizerOptions default_options;
  return SanitizeWithOptions(default_options);
}

FontSanitizerResult CPDF_FontSanitizer::SanitizeWithOptions(
    const FontSanitizerOptions& options) {
  FontSanitizerResult result;

  if (!document_) {
    result.error_message = "No document provided";
    return result;
  }

  printf( "\n========== FONT SANITIZER START ==========\n");
  printf( "Options: subset_fonts=%d, prune_type3=%d, rebuild_tounicode=%d\n",
          options.subset_fonts, options.prune_type3, options.rebuild_tounicode);

  // Step 1: Collect usage information from all pages.
  usage_collector_->CollectFromAllPages();
  const auto& usage_map = usage_collector_->GetUsageInfo();

  if (usage_map.empty()) {
    printf( "No fonts found in document.\n");
    printf( "========== FONT SANITIZER END ==========\n\n");
    // No fonts to process - this is still a success.
    result.success = true;
    return result;
  }

  const int total_fonts = static_cast<int>(usage_map.size());
  int current_font = 0;

  printf( "\n--- Processing %d fonts ---\n\n", total_fonts);

  // Step 2: Process each font.
  for (const auto& entry : usage_map) {
    const FontUsageInfo& info = entry.second;
    current_font++;

    // Call progress callback if provided.
    if (options.progress_callback) {
      if (!options.progress_callback(current_font, total_fonts)) {
        result.error_message = "Operation cancelled";
        return result;
      }
    }

    if (!info.font)
      continue;

    // Debug: Font processing header
    RetainPtr<const CPDF_Dictionary> font_dict = info.font->GetFontDict();
    ByteString base_font = font_dict ? font_dict->GetByteStringFor("BaseFont") : "";
    ByteString subtype = font_dict ? font_dict->GetByteStringFor("Subtype") : "";
    
    printf( ">>> Font %d/%d: obj#%u %s (%s)\n",
            current_font, total_fonts, info.font_obj_num,
            base_font.c_str(), subtype.c_str());
    printf( "    is_type3=%d, is_cid=%d, has_tounicode=%d\n",
            info.is_type3, info.is_cid, info.has_tounicode);
    printf( "    used_char_codes=%zu, used_gids=%zu\n",
            info.used_char_codes.size(), info.used_gids.size());
    
    // Debug: Log GID range if available
    if (!info.used_gids.empty()) {
      uint16_t min_gid = *info.used_gids.begin();
      uint16_t max_gid = *info.used_gids.rbegin();
      printf( "    GID range: %u to %u\n", min_gid, max_gid);
    } else if (!info.is_type3) {
      printf( "    WARNING: No GIDs collected! Subsetting will be SKIPPED.\n");
      printf( "    This may indicate a problem with GlyphFromCharCode for this font.\n");
    }

    result.fonts_processed++;
    bool font_modified = false;

    // Step 2a: Handle Type3 fonts specially.
    if (info.is_type3 && options.prune_type3) {
      printf( "    -> Pruning Type3 CharProcs...\n");
      if (type3_pruner_->PruneUnusedCharProcs(info.font.Get(),
                                               info.used_char_codes)) {
        font_modified = true;
        printf( "    -> Type3 pruning: MODIFIED\n");
      } else {
        printf( "    -> Type3 pruning: no change\n");
      }
    }

    // Step 2b: For non-Type3 fonts, sanitize the Widths array.
    if (!info.is_type3 && !info.is_cid) {
      printf( "    -> Sanitizing simple font widths...\n");
      if (SanitizeSimpleFontWidths(info.font.Get(), info.used_char_codes)) {
        font_modified = true;
        printf( "    -> Widths sanitization: MODIFIED\n");
      } else {
        printf( "    -> Widths sanitization: no change\n");
      }
    }

    // Step 2c: Subset non-Type3 embedded fonts (font program).
    // SECURITY: We pass the exact char_code -> GID mapping for cmap rebuild.
    // Only these mappings will exist in the output font - no information leakage.
    if (!info.is_type3 && options.subset_fonts && !info.used_gids.empty()) {
      printf("    -> Subsetting font program...\n");
      printf("    -> char_code_to_gid mappings: %zu\n", info.char_code_to_gid.size());
      if (subsetter_->SubsetAndReplaceFont(document_, info.font.Get(),
                                            info.used_gids, info.char_code_to_gid)) {
        font_modified = true;
        printf("    -> Font subsetting: MODIFIED\n");
      } else {
        printf("    -> Font subsetting: FAILED or no change\n");
      }
    } else if (!info.is_type3 && options.subset_fonts && info.used_gids.empty()) {
      printf("    -> SKIPPING subsetting: no GIDs collected\n");
    }

    // Step 2d: Rebuild ToUnicode for all fonts with ToUnicode.
    if (info.has_tounicode && options.rebuild_tounicode) {
      printf( "    -> Rebuilding ToUnicode...\n");
      if (tounicode_builder_->RebuildToUnicode(document_, info.font.Get(),
                                                info.used_char_codes)) {
        font_modified = true;
        printf( "    -> ToUnicode rebuild: MODIFIED\n");
      } else {
        printf( "    -> ToUnicode rebuild: no change\n");
      }
    }

    if (font_modified) {
      result.fonts_modified++;
      printf( "    RESULT: Font was modified\n");
    } else {
      printf( "    RESULT: Font unchanged\n");
    }
    printf( "\n");
  }

  result.success = true;
  
  printf( "========== FONT SANITIZER SUMMARY ==========\n");
  printf( "Total fonts: %d\n", total_fonts);
  printf( "Fonts processed: %d\n", result.fonts_processed);
  printf( "Fonts modified: %d\n", result.fonts_modified);
  printf( "Success: %s\n", result.success ? "YES" : "NO");
  printf( "========== FONT SANITIZER END ==========\n\n");

  return result;
}
