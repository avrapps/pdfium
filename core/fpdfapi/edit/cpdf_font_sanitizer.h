// Copyright 2025 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFAPI_EDIT_CPDF_FONT_SANITIZER_H_
#define CORE_FPDFAPI_EDIT_CPDF_FONT_SANITIZER_H_

#include <functional>
#include <memory>

#include "core/fxcrt/retain_ptr.h"

class CPDF_Document;
class CPDF_FontSubsetter;
class CPDF_FontUsageCollector;
class CPDF_ToUnicodeBuilder;
class CPDF_Type3Pruner;

// Options for font sanitization.
struct FontSanitizerOptions {
  // Whether to subset font programs (remove unused glyphs).
  // This reduces file size and removes potentially sensitive glyph data.
  bool subset_fonts = true;
  
  // Whether to rebuild ToUnicode CMaps (remove unused mappings).
  // This is critical for security - prevents character leakage.
  bool rebuild_tounicode = true;
  
  // Whether to prune Type3 font CharProcs.
  // Type3 glyphs can contain arbitrary content that might leak data.
  bool prune_type3 = true;
  
  // Progress callback: called with (current_font, total_fonts).
  // Return false to cancel the operation.
  std::function<bool(int, int)> progress_callback;
};

// Result of font sanitization.
struct FontSanitizerResult {
  // Whether sanitization completed successfully.
  bool success = false;
  
  // Number of fonts that were processed.
  int fonts_processed = 0;
  
  // Number of fonts that were modified.
  int fonts_modified = 0;
  
  // Error message if success is false.
  const char* error_message = nullptr;
};

// Main orchestrator for font sanitization.
// This class coordinates the collection of used glyphs, font subsetting,
// ToUnicode rebuilding, and Type3 CharProcs pruning.
//
// Usage:
//   CPDF_FontSanitizer sanitizer(document);
//   FontSanitizerResult result = sanitizer.Sanitize();
//
// This should be called as a finalization step before saving a document,
// especially after redaction operations or when embedding new fonts.
class CPDF_FontSanitizer {
 public:
  explicit CPDF_FontSanitizer(CPDF_Document* doc);
  ~CPDF_FontSanitizer();

  // Performs font sanitization with default options.
  FontSanitizerResult Sanitize();
  
  // Performs font sanitization with custom options.
  FontSanitizerResult SanitizeWithOptions(const FontSanitizerOptions& options);

 private:
  CPDF_Document* document_;
  std::unique_ptr<CPDF_FontUsageCollector> usage_collector_;
  std::unique_ptr<CPDF_FontSubsetter> subsetter_;
  std::unique_ptr<CPDF_ToUnicodeBuilder> tounicode_builder_;
  std::unique_ptr<CPDF_Type3Pruner> type3_pruner_;
};

#endif  // CORE_FPDFAPI_EDIT_CPDF_FONT_SANITIZER_H_
