// Copyright 2025 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFAPI_EDIT_CPDF_TOUNICODE_BUILDER_H_
#define CORE_FPDFAPI_EDIT_CPDF_TOUNICODE_BUILDER_H_

#include <map>
#include <set>

#include "core/fxcrt/bytestring.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/widestring.h"

class CPDF_Document;
class CPDF_Font;
class CPDF_Stream;

// Rebuilds ToUnicode CMaps to only include mappings for used character codes.
// This is crucial for secure redaction - removing unused mappings prevents
// attackers from recovering redacted text by inspecting the ToUnicode stream.
class CPDF_ToUnicodeBuilder {
 public:
  CPDF_ToUnicodeBuilder();
  ~CPDF_ToUnicodeBuilder();

  // Rebuilds the ToUnicode CMap for a font, keeping only mappings for the
  // specified character codes.
  // |doc| - The PDF document (for creating new stream objects).
  // |font| - The font whose ToUnicode to rebuild.
  // |used_char_codes| - Character codes that should be retained.
  // Returns true if the ToUnicode was successfully rebuilt.
  bool RebuildToUnicode(CPDF_Document* doc,
                        CPDF_Font* font,
                        const std::set<uint32_t>& used_char_codes);

  // Generates a ToUnicode CMap stream from a mapping.
  // |char_to_unicode| - Map from character codes to Unicode strings.
  // Returns the CMap content as a ByteString.
  static ByteString GenerateCMapContent(
      const std::map<uint32_t, WideString>& char_to_unicode);

 private:
  // Replaces the ToUnicode stream in the font dictionary.
  bool ReplaceToUnicodeStream(CPDF_Document* doc,
                              CPDF_Font* font,
                              const ByteString& cmap_content);
};

#endif  // CORE_FPDFAPI_EDIT_CPDF_TOUNICODE_BUILDER_H_
