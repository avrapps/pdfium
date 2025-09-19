// Copyright 2025 The EmbedPDF Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Experimental EmbedPDF outline/destination/action helpers and APIs.
// These functions are factored out of fpdf_doc.cpp to keep that file small.

#include "public/fpdf_doc.h"

#include <utility>

#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_name.h"
#include "core/fpdfapi/parser/cpdf_null.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_string.h"
#include "core/fxcrt/compiler_specific.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/widestring.h"
#include "fpdfsdk/cpdfsdk_helpers.h"

namespace {

// ------- Common helpers --------------------------------------------------

// Creates a new indirect array in |doc|.
RetainPtr<CPDF_Array> NewIndirectArray(CPDF_Document* doc) {
  return doc->NewIndirect<CPDF_Array>();
}

// Appends the page dictionary reference as the first element of |arr|.
bool AppendPageDict(CPDF_Array* arr, CPDF_Page* page) {
  if (!arr || !page)
    return false;
  CPDF_Document* doc = page->GetDocument();
  if (!doc)
    return false;
  RetainPtr<const CPDF_Dictionary> dict = page->GetDict();
  if (!dict)
    return false;
  arr->AppendNew<CPDF_Reference>(doc, dict->GetObjNum());
  return true;
}
}  // namespace

FPDF_EXPORT FPDF_DEST FPDF_CALLCONV
EPDFDest_CreateXYZ(FPDF_PAGE fpage,
                   FPDF_BOOL has_left, FS_FLOAT left,
                   FPDF_BOOL has_top,  FS_FLOAT top,
                   FPDF_BOOL has_zoom, FS_FLOAT zoom) {
  CPDF_Page* page = CPDFPageFromFPDFPage(fpage);
  if (!page)
    return nullptr;
  CPDF_Document* doc = page->GetDocument();
  if (!doc)
    return nullptr;

  RetainPtr<CPDF_Array> arr = NewIndirectArray(doc);
  if (!AppendPageDict(arr.Get(), page))
    return nullptr;

  arr->AppendNew<CPDF_Name>("XYZ");

  if (has_left)
    arr->AppendNew<CPDF_Number>(static_cast<float>(left));
  else
    arr->AppendNew<CPDF_Null>();

  if (has_top)
    arr->AppendNew<CPDF_Number>(static_cast<float>(top));
  else
    arr->AppendNew<CPDF_Null>();

  // Spec equivalence: zoom==0 means "unspecified"; emit null unless the caller set a non-zero zoom.
  if (has_zoom && zoom != 0.0f)
    arr->AppendNew<CPDF_Number>(static_cast<float>(zoom));
  else
    arr->AppendNew<CPDF_Null>();

  return FPDFDestFromCPDFArray(arr.Get());
}

FPDF_EXPORT FPDF_DEST FPDF_CALLCONV
EPDFDest_CreateFit(FPDF_PAGE fpage) {
  CPDF_Page* page = CPDFPageFromFPDFPage(fpage);
  if (!page)
    return nullptr;
  CPDF_Document* doc = page->GetDocument();
  if (!doc)
    return nullptr;

  RetainPtr<CPDF_Array> arr = NewIndirectArray(doc);
  if (!AppendPageDict(arr.Get(), page))
    return nullptr;

  arr->AppendNew<CPDF_Name>("Fit");
  return FPDFDestFromCPDFArray(arr.Get());
}

FPDF_EXPORT FPDF_DEST FPDF_CALLCONV
EPDFDest_CreateFitH(FPDF_PAGE fpage, FS_FLOAT top) {
  CPDF_Page* page = CPDFPageFromFPDFPage(fpage);
  if (!page)
    return nullptr;
  CPDF_Document* doc = page->GetDocument();
  if (!doc)
    return nullptr;

  RetainPtr<CPDF_Array> arr = NewIndirectArray(doc);
  if (!AppendPageDict(arr.Get(), page))
    return nullptr;

  arr->AppendNew<CPDF_Name>("FitH");
  arr->AppendNew<CPDF_Number>(static_cast<float>(top));
  return FPDFDestFromCPDFArray(arr.Get());
}

FPDF_EXPORT FPDF_DEST FPDF_CALLCONV
EPDFDest_CreateFitV(FPDF_PAGE fpage, FS_FLOAT left) {
  CPDF_Page* page = CPDFPageFromFPDFPage(fpage);
  if (!page)
    return nullptr;
  CPDF_Document* doc = page->GetDocument();
  if (!doc)
    return nullptr;

  RetainPtr<CPDF_Array> arr = NewIndirectArray(doc);
  if (!AppendPageDict(arr.Get(), page))
    return nullptr;

  arr->AppendNew<CPDF_Name>("FitV");
  arr->AppendNew<CPDF_Number>(static_cast<float>(left));
  return FPDFDestFromCPDFArray(arr.Get());
}

FPDF_EXPORT FPDF_DEST FPDF_CALLCONV
EPDFDest_CreateFitR(FPDF_PAGE fpage,
                    FS_FLOAT left, FS_FLOAT bottom, FS_FLOAT right, FS_FLOAT top) {
  CPDF_Page* page = CPDFPageFromFPDFPage(fpage);
  if (!page)
    return nullptr;
  CPDF_Document* doc = page->GetDocument();
  if (!doc)
    return nullptr;

  RetainPtr<CPDF_Array> arr = NewIndirectArray(doc);
  if (!AppendPageDict(arr.Get(), page))
    return nullptr;

  arr->AppendNew<CPDF_Name>("FitR");
  arr->AppendNew<CPDF_Number>(static_cast<float>(left));
  arr->AppendNew<CPDF_Number>(static_cast<float>(bottom));
  arr->AppendNew<CPDF_Number>(static_cast<float>(right));
  arr->AppendNew<CPDF_Number>(static_cast<float>(top));
  return FPDFDestFromCPDFArray(arr.Get());
}

FPDF_EXPORT FPDF_DEST FPDF_CALLCONV
EPDFDest_CreateFitB(FPDF_PAGE fpage) {
  CPDF_Page* page = CPDFPageFromFPDFPage(fpage);
  if (!page)
    return nullptr;
  CPDF_Document* doc = page->GetDocument();
  if (!doc)
    return nullptr;

  RetainPtr<CPDF_Array> arr = NewIndirectArray(doc);
  if (!AppendPageDict(arr.Get(), page))
    return nullptr;

  arr->AppendNew<CPDF_Name>("FitB");
  return FPDFDestFromCPDFArray(arr.Get());
}

FPDF_EXPORT FPDF_DEST FPDF_CALLCONV
EPDFDest_CreateFitBH(FPDF_PAGE fpage, FS_FLOAT top) {
  CPDF_Page* page = CPDFPageFromFPDFPage(fpage);
  if (!page)
    return nullptr;
  CPDF_Document* doc = page->GetDocument();
  if (!doc)
    return nullptr;

  RetainPtr<CPDF_Array> arr = NewIndirectArray(doc);
  if (!AppendPageDict(arr.Get(), page))
    return nullptr;

  arr->AppendNew<CPDF_Name>("FitBH");
  arr->AppendNew<CPDF_Number>(static_cast<float>(top));
  return FPDFDestFromCPDFArray(arr.Get());
}

FPDF_EXPORT FPDF_DEST FPDF_CALLCONV
EPDFDest_CreateFitBV(FPDF_PAGE fpage, FS_FLOAT left) {
  CPDF_Page* page = CPDFPageFromFPDFPage(fpage);
  if (!page)
    return nullptr;
  CPDF_Document* doc = page->GetDocument();
  if (!doc)
    return nullptr;

  RetainPtr<CPDF_Array> arr = NewIndirectArray(doc);
  if (!AppendPageDict(arr.Get(), page))
    return nullptr;

  arr->AppendNew<CPDF_Name>("FitBV");
  arr->AppendNew<CPDF_Number>(static_cast<float>(left));
  return FPDFDestFromCPDFArray(arr.Get());
}

FPDF_EXPORT FPDF_ACTION FPDF_CALLCONV
EPDFAction_CreateGoTo(FPDF_DOCUMENT document, FPDF_DEST dest) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(document);
  if (!pDoc || !dest)
    return nullptr;

  CPDF_Array* pDestArray = CPDFArrayFromFPDFDest(dest);
  if (!pDestArray)
    return nullptr;

  // We expect |dest| to belong to |document| and to be indirect (created by EPDFDest_*()).
  // Bail out if it's not indirect to avoid cross-doc or inline ownership issues.
  if (pDestArray->GetObjNum() == 0)
    return nullptr;

  RetainPtr<CPDF_Dictionary> action = pDoc->NewIndirect<CPDF_Dictionary>();
  action->SetNewFor<CPDF_Name>("S", "GoTo");
  action->SetNewFor<CPDF_Reference>("D", pDoc, pDestArray->GetObjNum());

  return FPDFActionFromCPDFDictionary(action.Get());
}

FPDF_EXPORT FPDF_ACTION FPDF_CALLCONV
EPDFAction_CreateURI(FPDF_DOCUMENT document, FPDF_BYTESTRING uri) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(document);
  if (!pDoc || !uri)
    return nullptr;

  RetainPtr<CPDF_Dictionary> action = pDoc->NewIndirect<CPDF_Dictionary>();
  action->SetNewFor<CPDF_Name>("S", "URI");
  // Store as a byte string (UTF-8 as provided by caller).
  action->SetNewFor<CPDF_String>("URI", ByteString(uri));

  return FPDFActionFromCPDFDictionary(action.Get());
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFBookmark_SetTitle(FPDF_BOOKMARK bookmark, FPDF_WIDESTRING title) {
  if (!bookmark)
    return false;

  RetainPtr<CPDF_Dictionary> bm_dict(
      pdfium::WrapRetain(CPDFDictionaryFromFPDFBookmark(bookmark)));
  if (!bm_dict)
    return false;

  WideString wide =
      title ? UNSAFE_BUFFERS(WideStringFromFPDFWideString(title)) : WideString();

  // Store as Unicode CPDF_String (matches existing patterns, e.g., Info keys).
  bm_dict->SetNewFor<CPDF_String>("Title", wide.AsStringView());
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFBookmark_SetDest(FPDF_DOCUMENT doc, FPDF_BOOKMARK bookmark, FPDF_DEST dest) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(doc);
  if (!pDoc || !bookmark || !dest)
    return false;

  RetainPtr<CPDF_Dictionary> bm_dict(
      pdfium::WrapRetain(CPDFDictionaryFromFPDFBookmark(bookmark)));
  if (!bm_dict)
    return false;

  CPDF_Array* dest_arr = CPDFArrayFromFPDFDest(dest);
  if (!dest_arr)
    return false;

  // Require |dest| to be indirect to avoid cross-doc or inline ownership issues.
  if (dest_arr->GetObjNum() == 0)
    return false;

  // Clear any action.
  bm_dict->RemoveFor("A");

  // Set /Dest as an indirect reference into this document.
  bm_dict->SetNewFor<CPDF_Reference>("Dest", pDoc, dest_arr->GetObjNum());
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFBookmark_SetAction(FPDF_DOCUMENT document,
                       FPDF_BOOKMARK bookmark,
                       FPDF_ACTION action) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(document);
  if (!pDoc || !bookmark || !action)
    return false;

  RetainPtr<CPDF_Dictionary> bm_dict(
      pdfium::WrapRetain(CPDFDictionaryFromFPDFBookmark(bookmark)));
  if (!bm_dict)
    return false;

  CPDF_Dictionary* act_dict = CPDFDictionaryFromFPDFAction(action);
  if (!act_dict)
    return false;

  // Require the action to be indirect so we can reference it.
  if (act_dict->GetObjNum() == 0)
    return false;

  // Clear any /Dest when setting /A, per PDF spec.
  bm_dict->RemoveFor("Dest");

  // Set /A as an indirect reference to |action|.
  bm_dict->SetNewFor<CPDF_Reference>("A", pDoc, act_dict->GetObjNum());
  return true;
}