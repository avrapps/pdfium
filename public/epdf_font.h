// Copyright 2026 The EmbedPDF Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PUBLIC_EPDF_FONT_H_
#define PUBLIC_EPDF_FONT_H_

#include <stddef.h>
#include <stdint.h>

// NOLINTNEXTLINE(build/include)
#include "fpdfview.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Experimental EmbedPDF Extension API.
typedef uint32_t EPDF_FONT_ID;

// Experimental EmbedPDF Extension API.
// Register a font for runtime fallback use and PDF authoring from file access.
//
//   family_name  - optional family/resource base name. Pass NULL or "" to
//                  infer from the font.
//   weight       - style weight for matching. Pass 0 to infer from the font.
//   italic       - style italic flag for matching. Pass -1 to infer from the
//                  font, 0 for non-italic, or 1 for italic.
//   file_access  - font bytes as FPDF_FILEACCESS. The underlying file resources
//                  must remain valid until EPDFFont_ClearRegisteredFonts() or
//                  PDFium shutdown. The FPDF_FILEACCESS struct itself may be
//                  stack-owned.
//
// Returns a non-zero font id on success, or 0 on failure.
FPDF_EXPORT EPDF_FONT_ID FPDF_CALLCONV
EPDFFont_RegisterFont(FPDF_BYTESTRING family_name,
                      int weight,
                      int italic,
                      FPDF_FILEACCESS* file_access);

// Experimental EmbedPDF Extension API.
// Register an in-memory font for runtime fallback use and PDF authoring.
//
//   family_name - optional family/resource base name. Pass NULL or "" to infer
//                 from the font.
//   weight      - style weight for matching. Pass 0 to infer from the font.
//   italic      - style italic flag for matching. Pass -1 to infer from the
//                 font, 0 for non-italic, or 1 for italic.
//   data_buf    - pointer to font bytes.
//   size        - size of |data_buf| in bytes.
//
// Returns a non-zero font id on success, or 0 on failure.
FPDF_EXPORT EPDF_FONT_ID FPDF_CALLCONV
EPDFFont_RegisterMemFont(FPDF_BYTESTRING family_name,
                         int weight,
                         int italic,
                         const void* data_buf,
                         int size);

// Experimental EmbedPDF Extension API.
// Same as EPDFFont_RegisterMemFont(), but supports size_t byte counts.
FPDF_EXPORT EPDF_FONT_ID FPDF_CALLCONV
EPDFFont_RegisterMemFont64(FPDF_BYTESTRING family_name,
                           int weight,
                           int italic,
                           const void* data_buf,
                           size_t size);

// Experimental EmbedPDF Extension API.
// Clear all registered fonts and the fallback font order.
FPDF_EXPORT void FPDF_CALLCONV EPDFFont_ClearRegisteredFonts(void);

// Experimental EmbedPDF Extension API.
// Add a registered font to the ordered fallback list used when the selected
// font does not contain a glyph or a PDF page needs a substitute font.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFFont_AddFallbackFont(EPDF_FONT_ID font_id);

// Experimental EmbedPDF Extension API.
// Clear the ordered fallback font list without unregistering fonts.
FPDF_EXPORT void FPDF_CALLCONV EPDFFont_ClearFallbackFonts(void);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_EPDF_FONT_H_
