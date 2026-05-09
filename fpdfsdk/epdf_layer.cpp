// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "public/fpdfview.h"

#include <limits>
#include <memory>
#include <utility>

#include "core/fpdfapi/parser/cpdf_base_document.h"
#include "core/fpdfapi/parser/cpdf_layer_document.h"
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

EPDFLayerOpenStatus ToPublicStatus(CPDF_LayerDocument::OpenStatus status) {
  switch (status) {
    case CPDF_LayerDocument::OpenStatus::kSuccess:
      return EPDFLayerOpenStatus_kSuccess;
    case CPDF_LayerDocument::OpenStatus::kMalformedDelta:
      return EPDFLayerOpenStatus_kMalformedDelta;
    case CPDF_LayerDocument::OpenStatus::kBaseLayerMismatch:
      return EPDFLayerOpenStatus_kBaseLayerMismatch;
    case CPDF_LayerDocument::OpenStatus::kOpenFailed:
      return EPDFLayerOpenStatus_kOpenFailed;
  }
}

}  // namespace

FPDF_EXPORT FPDF_DOCUMENT FPDF_CALLCONV
EPDFLayer_OpenLayer(EPDF_BASE_DOCUMENT base,
                    FPDF_FILEACCESS* pFileAccess,
                    FPDF_BYTESTRING password,
                    EPDFLayerOpenStatus* out_status) {
  if (out_status) {
    *out_status = EPDFLayerOpenStatus_kOpenFailed;
  }
  if (!base || !pFileAccess) {
    return nullptr;
  }

  // Slice 7.2 layers share the base parser/security state; password handling is
  // already complete when the base is loaded.
  (void)password;

  CPDF_BaseDocument* base_doc = CPDFBaseDocumentFromEPDFBaseDocument(base);
  RetainPtr<CPDF_BaseDocument> retained_base = pdfium::WrapRetain(base_doc);
  auto layer = std::make_unique<CPDF_LayerDocument>(
      std::move(retained_base),
      pdfium::MakeRetain<CPDFSDK_CustomAccess>(pFileAccess));

  const EPDFLayerOpenStatus status = ToPublicStatus(layer->ingest_status());
  if (out_status) {
    *out_status = status;
  }
  if (status != EPDFLayerOpenStatus_kSuccess) {
    return nullptr;
  }

  return FPDFDocumentFromCPDFDocument(layer.release());
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFLayer_IsObjectPromoted(FPDF_DOCUMENT layer, unsigned long obj_num) {
  if (obj_num > std::numeric_limits<uint32_t>::max()) {
    return false;
  }
  CPDF_Document* document = CPDFDocumentFromFPDFDocument(layer);
  CPDF_LayerDocument* layer_doc = CPDF_LayerDocument::FromDocument(document);
  return layer_doc &&
         layer_doc->IsObjectPromoted(static_cast<uint32_t>(obj_num));
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFLayer_GetPromotedObjectCount(FPDF_DOCUMENT layer) {
  CPDF_Document* document = CPDFDocumentFromFPDFDocument(layer);
  CPDF_LayerDocument* layer_doc = CPDF_LayerDocument::FromDocument(document);
  return layer_doc ? layer_doc->GetPromotedObjectCount() : 0;
}

FPDF_EXPORT EPDF_BASE_DOCUMENT FPDF_CALLCONV
EPDFLayer_GetBaseDocument(FPDF_DOCUMENT layer) {
  CPDF_Document* document = CPDFDocumentFromFPDFDocument(layer);
  CPDF_LayerDocument* layer_doc = CPDF_LayerDocument::FromDocument(document);
  return layer_doc ? EPDFBaseDocumentFromCPDFBaseDocument(
                         layer_doc->GetBaseDocument())
                   : nullptr;
}
