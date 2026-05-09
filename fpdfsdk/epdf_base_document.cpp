// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "public/fpdfview.h"

#include "core/fpdfapi/parser/cpdf_base_document.h"
#include "core/fpdfapi/parser/cpdf_parser.h"
#include "core/fxcrt/retain_ptr.h"
#include "fpdfsdk/cpdfsdk_customaccess.h"
#include "fpdfsdk/cpdfsdk_helpers.h"

namespace {

CPDF_BaseDocument* CPDFBaseDocumentFromEPDFBaseDocument(
    EPDF_BASE_DOCUMENT base) {
  return reinterpret_cast<CPDF_BaseDocument*>(base);
}

EPDF_BASE_DOCUMENT EPDFBaseDocumentFromCPDFBaseDocument(
    CPDF_BaseDocument* base) {
  return reinterpret_cast<EPDF_BASE_DOCUMENT>(base);
}

}  // namespace

FPDF_EXPORT EPDF_BASE_DOCUMENT FPDF_CALLCONV
EPDF_LoadBaseDocument(FPDF_FILEACCESS* pFileAccess, FPDF_BYTESTRING password) {
  if (!pFileAccess) {
    return nullptr;
  }

  RetainPtr<CPDF_BaseDocument> base = pdfium::MakeRetain<CPDF_BaseDocument>();
  CPDF_Parser::Error error = base->LoadBaseDoc(
      pdfium::MakeRetain<CPDFSDK_CustomAccess>(pFileAccess), password);
  if (error != CPDF_Parser::SUCCESS) {
    ProcessParseError(error);
    return nullptr;
  }

  return EPDFBaseDocumentFromCPDFBaseDocument(base.Leak());
}

FPDF_EXPORT void FPDF_CALLCONV
EPDF_ReleaseBaseDocument(EPDF_BASE_DOCUMENT base) {
  RetainPtr<CPDF_BaseDocument> retained;
  retained.Unleak(CPDFBaseDocumentFromEPDFBaseDocument(base));
}
