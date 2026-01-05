// Copyright 2025 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFAPI_EDIT_CPDF_TYPE3_PRUNER_H_
#define CORE_FPDFAPI_EDIT_CPDF_TYPE3_PRUNER_H_

#include <set>

#include "core/fxcrt/retain_ptr.h"

class CPDF_Document;
class CPDF_Font;
class CPDF_Type3Font;

// Removes unused CharProcs from Type3 fonts.
// Type3 fonts store each glyph as a content stream in the CharProcs dictionary.
// These content streams can contain text, images, or vector graphics that
// reveal redacted content. Pruning removes all CharProcs not referenced by
// the used character codes.
class CPDF_Type3Pruner {
 public:
  CPDF_Type3Pruner();
  ~CPDF_Type3Pruner();

  // Prunes unused CharProcs from a Type3 font.
  // |font| - The Type3 font to prune.
  // |used_char_codes| - Character codes that should be retained.
  // Returns true if any CharProcs were removed.
  bool PruneUnusedCharProcs(CPDF_Font* font,
                            const std::set<uint32_t>& used_char_codes);

 private:
  // Gets the glyph name for a character code from the font's encoding.
  const char* GetGlyphName(CPDF_Type3Font* font, uint32_t char_code);
};

#endif  // CORE_FPDFAPI_EDIT_CPDF_TYPE3_PRUNER_H_
