// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "public/epdf_font.h"

#include <stddef.h>
#include <stdint.h>

#include "core/fxcrt/bytestring.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/span.h"
#include "core/fxge/cfx_fontregistry.h"
#include "fpdfsdk/cpdfsdk_customaccess.h"

FPDF_EXPORT EPDF_FONT_ID FPDF_CALLCONV
EPDFFont_RegisterFont(FPDF_BYTESTRING family_name,
                      int weight,
                      int italic,
                      FPDF_FILEACCESS* file_access) {
  if (!file_access) {
    return CFX_FontRegistry::kInvalidFontId;
  }

  ByteString font_family_name(family_name ? family_name : "");
  return CFX_FontRegistry::RegisterFont(
      font_family_name, weight, italic,
      pdfium::MakeRetain<CPDFSDK_CustomAccess>(file_access));
}

FPDF_EXPORT EPDF_FONT_ID FPDF_CALLCONV
EPDFFont_RegisterMemFont(FPDF_BYTESTRING family_name,
                         int weight,
                         int italic,
                         const void* data_buf,
                         int size) {
  if (size < 0) {
    return CFX_FontRegistry::kInvalidFontId;
  }
  return EPDFFont_RegisterMemFont64(family_name, weight, italic, data_buf,
                                    static_cast<size_t>(size));
}

FPDF_EXPORT EPDF_FONT_ID FPDF_CALLCONV
EPDFFont_RegisterMemFont64(FPDF_BYTESTRING family_name,
                           int weight,
                           int italic,
                           const void* data_buf,
                           size_t size) {
  if (!data_buf || size == 0) {
    return CFX_FontRegistry::kInvalidFontId;
  }

  ByteString font_family_name(family_name ? family_name : "");
  // SAFETY: required from caller.
  auto font_data =
      UNSAFE_BUFFERS(pdfium::span(static_cast<const uint8_t*>(data_buf), size));
  return CFX_FontRegistry::RegisterMemoryFont(font_family_name, weight, italic,
                                              font_data);
}

FPDF_EXPORT void FPDF_CALLCONV EPDFFont_ClearRegisteredFonts(void) {
  CFX_FontRegistry::ClearRegisteredFonts();
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFFont_AddFallbackFont(EPDF_FONT_ID font_id) {
  return CFX_FontRegistry::AddFallbackFont(font_id);
}

FPDF_EXPORT void FPDF_CALLCONV EPDFFont_ClearFallbackFonts(void) {
  CFX_FontRegistry::ClearFallbackFonts();
}
