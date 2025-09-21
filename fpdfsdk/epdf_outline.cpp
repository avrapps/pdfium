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
  
  // Returns the document root's /Outlines dict, creating one if needed.
  RetainPtr<CPDF_Dictionary> GetOrCreateOutlines(CPDF_Document* doc) {
    CPDF_Dictionary* root = doc->GetMutableRoot();
    if (!root)
      return nullptr;
  
    // We plan to write under /Outlines, so use the mutable getter.
    RetainPtr<CPDF_Dictionary> outlines = root->GetMutableDictFor("Outlines");
    if (outlines)
      return outlines;
  
    outlines = doc->NewIndirect<CPDF_Dictionary>();
    root->SetNewFor<CPDF_Reference>("Outlines", doc, outlines->GetObjNum());
    return outlines;
  }
  
  // Does |obj| belong to |doc| as an indirect object?
  bool BelongsTo(const CPDF_Document* doc, const CPDF_Object* obj) {
    if (!doc || !obj)
      return false;
    const uint32_t num = obj->GetObjNum();
    return num != 0 && doc->GetIndirectObject(num) == obj;
  }
  
  // Create a new indirect bookmark dictionary with a UTF-16 title.
  RetainPtr<CPDF_Dictionary> CreateBookmarkDict(CPDF_Document* doc,
                                                const WideString& title) {
    RetainPtr<CPDF_Dictionary> node = doc->NewIndirect<CPDF_Dictionary>();
    if (!title.IsEmpty())
      node->SetNewFor<CPDF_String>("Title", title.AsStringView());
    return node;
  }
  
  // Return true if |candidate| is in the subtree rooted at |root|. (read-only)
  bool IsDescendant(const CPDF_Dictionary* root,
                    const CPDF_Dictionary* candidate) {
    if (!root || !candidate)
      return false;
    if (root == candidate)
      return true;
    for (RetainPtr<const CPDF_Dictionary> cur = root->GetDictFor("First");
         cur; cur = cur->GetDictFor("Next")) {
      if (cur.Get() == candidate)
        return true;
      if (IsDescendant(cur.Get(), candidate))
        return true;
    }
    return false;
  }
  
  // Unlink |node| from its current parent/sibling list. Clears node's /Prev,/Next.
  // Leaves /Parent in place (caller will overwrite on insert or may inspect it).
  void UnlinkFromParent(CPDF_Document* doc, CPDF_Dictionary* node) {
    if (!node)
      return;
  
    RetainPtr<CPDF_Dictionary> parent = node->GetMutableDictFor("Parent");
    RetainPtr<CPDF_Dictionary> prev   = node->GetMutableDictFor("Prev");
    RetainPtr<CPDF_Dictionary> next   = node->GetMutableDictFor("Next");
  
    // Fix sibling pointers.
    if (prev) {
      if (next)
        prev->SetNewFor<CPDF_Reference>("Next", doc, next->GetObjNum());
      else
        prev->RemoveFor("Next");
    }
    if (next) {
      if (prev)
        next->SetNewFor<CPDF_Reference>("Prev", doc, prev->GetObjNum());
      else
        next->RemoveFor("Prev");
    }
  
    // Fix parent's /First and /Last.
    if (parent) {
      RetainPtr<CPDF_Dictionary> first = parent->GetMutableDictFor("First");
      RetainPtr<CPDF_Dictionary> last  = parent->GetMutableDictFor("Last");
      if (first && first.Get() == node) {
        if (next)
          parent->SetNewFor<CPDF_Reference>("First", doc, next->GetObjNum());
        else
          parent->RemoveFor("First");
      }
      if (last && last.Get() == node) {
        if (prev)
          parent->SetNewFor<CPDF_Reference>("Last", doc, prev->GetObjNum());
        else
          parent->RemoveFor("Last");
      }
    }
  
    node->RemoveFor("Prev");
    node->RemoveFor("Next");
  }
  
  // Insert |child| as the last child of |parent_like|.
  void AppendChild(CPDF_Document* doc,
                   CPDF_Dictionary* parent_like,
                   CPDF_Dictionary* child) {
    child->SetNewFor<CPDF_Reference>("Parent", doc, parent_like->GetObjNum());
  
    RetainPtr<CPDF_Dictionary> last = parent_like->GetMutableDictFor("Last");
    if (!last) {
      // First child.
      parent_like->SetNewFor<CPDF_Reference>("First", doc, child->GetObjNum());
      parent_like->SetNewFor<CPDF_Reference>("Last",  doc, child->GetObjNum());
      child->RemoveFor("Prev");
      child->RemoveFor("Next");
      return;
    }
  
    last->SetNewFor<CPDF_Reference>("Next", doc, child->GetObjNum());
    child->SetNewFor<CPDF_Reference>("Prev", doc, last->GetObjNum());
    child->RemoveFor("Next");
    parent_like->SetNewFor<CPDF_Reference>("Last", doc, child->GetObjNum());
  }
  
  // Insert |child| under |parent_like| right after |after_sibling|.
  // If |after_sibling| is null, insert as the first child.
  bool InsertAfter(CPDF_Document* doc,
                   CPDF_Dictionary* parent_like,
                   CPDF_Dictionary* after_sibling,  // may be null
                   CPDF_Dictionary* child) {
    child->SetNewFor<CPDF_Reference>("Parent", doc, parent_like->GetObjNum());
  
    if (!after_sibling) {
      // Insert as first child.
      RetainPtr<CPDF_Dictionary> first = parent_like->GetMutableDictFor("First");
      if (first) {
        first->SetNewFor<CPDF_Reference>("Prev", doc, child->GetObjNum());
        child->SetNewFor<CPDF_Reference>("Next", doc, first->GetObjNum());
      } else {
        // No children previously.
        parent_like->SetNewFor<CPDF_Reference>("Last", doc, child->GetObjNum());
        child->RemoveFor("Next");
      }
      parent_like->SetNewFor<CPDF_Reference>("First", doc, child->GetObjNum());
      child->RemoveFor("Prev");
      return true;
    }
  
    // Validate parent of after_sibling.
    RetainPtr<const CPDF_Dictionary> sib_parent =
        after_sibling->GetDictFor("Parent");
    if (!sib_parent || sib_parent.Get() != parent_like)
      return false;
  
    RetainPtr<CPDF_Dictionary> next = after_sibling->GetMutableDictFor("Next");
  
    after_sibling->SetNewFor<CPDF_Reference>("Next", doc, child->GetObjNum());
    child->SetNewFor<CPDF_Reference>("Prev", doc, after_sibling->GetObjNum());
  
    if (next) {
      next->SetNewFor<CPDF_Reference>("Prev", doc, child->GetObjNum());
      child->SetNewFor<CPDF_Reference>("Next", doc, next->GetObjNum());
    } else {
      parent_like->SetNewFor<CPDF_Reference>("Last", doc, child->GetObjNum());
      child->RemoveFor("Next");
    }
    return true;
  }
  
  // Recursively delete the subtree rooted at |node|.
  void DeleteSubtree(CPDF_Document* doc, CPDF_Dictionary* node) {
    // Delete children (depth-first).
    for (RetainPtr<CPDF_Dictionary> cur = node->GetMutableDictFor("First"); cur; ) {
      RetainPtr<CPDF_Dictionary> next = cur->GetMutableDictFor("Next");
      DeleteSubtree(doc, cur.Get());
      cur = std::move(next);
    }
  
    // Unlink self and delete indirect object.
    UnlinkFromParent(doc, node);
    node->RemoveFor("First");
    node->RemoveFor("Last");
    node->RemoveFor("Parent");
  
    const uint32_t num = node->GetObjNum();
    if (num)
      doc->DeleteIndirectObject(num);
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

FPDF_EXPORT FPDF_BOOKMARK FPDF_CALLCONV
EPDFBookmark_Create(FPDF_DOCUMENT doc, FPDF_WIDESTRING title) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(doc);
  if (!pDoc)
    return nullptr;

  RetainPtr<CPDF_Dictionary> outlines = GetOrCreateOutlines(pDoc);
  if (!outlines)
    return nullptr;

  WideString wtitle =
      title ? UNSAFE_BUFFERS(WideStringFromFPDFWideString(title)) : WideString();

  RetainPtr<CPDF_Dictionary> node = CreateBookmarkDict(pDoc, wtitle);
  AppendChild(pDoc, outlines.Get(), node.Get());
  return FPDFBookmarkFromCPDFDictionary(node.Get());
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFBookmark_Delete(FPDF_DOCUMENT doc, FPDF_BOOKMARK bookmark) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(doc);
  RetainPtr<CPDF_Dictionary> bm(
      pdfium::WrapRetain(CPDFDictionaryFromFPDFBookmark(bookmark)));
  if (!pDoc || !bm || !BelongsTo(pDoc, bm.Get()))
    return false;

  DeleteSubtree(pDoc, bm.Get());
  return true;
}

FPDF_EXPORT FPDF_BOOKMARK FPDF_CALLCONV
EPDFBookmark_AppendChild(FPDF_DOCUMENT doc,
                         FPDF_BOOKMARK parent,
                         FPDF_WIDESTRING title) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(doc);
  if (!pDoc)
    return nullptr;

  RetainPtr<CPDF_Dictionary> parent_like =
      parent ? pdfium::WrapRetain(CPDFDictionaryFromFPDFBookmark(parent))
             : GetOrCreateOutlines(pDoc);
  if (!parent_like || !BelongsTo(pDoc, parent_like.Get()))
    return nullptr;

  WideString wtitle =
      title ? UNSAFE_BUFFERS(WideStringFromFPDFWideString(title)) : WideString();
  RetainPtr<CPDF_Dictionary> child = CreateBookmarkDict(pDoc, wtitle);
  AppendChild(pDoc, parent_like.Get(), child.Get());
  return FPDFBookmarkFromCPDFDictionary(child.Get());
}

FPDF_EXPORT FPDF_BOOKMARK FPDF_CALLCONV
EPDFBookmark_InsertAfter(FPDF_DOCUMENT doc,
                         FPDF_BOOKMARK parent,
                         FPDF_BOOKMARK after_sibling,
                         FPDF_WIDESTRING title) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(doc);
  if (!pDoc)
    return nullptr;

  RetainPtr<CPDF_Dictionary> parent_like =
      parent ? pdfium::WrapRetain(CPDFDictionaryFromFPDFBookmark(parent))
             : GetOrCreateOutlines(pDoc);
  if (!parent_like || !BelongsTo(pDoc, parent_like.Get()))
    return nullptr;

  CPDF_Dictionary* after_dict =
      after_sibling ? CPDFDictionaryFromFPDFBookmark(after_sibling) : nullptr;
  if (after_dict && !BelongsTo(pDoc, after_dict))
    return nullptr;

  WideString wtitle =
      title ? UNSAFE_BUFFERS(WideStringFromFPDFWideString(title)) : WideString();
  RetainPtr<CPDF_Dictionary> child = CreateBookmarkDict(pDoc, wtitle);

  if (!InsertAfter(pDoc, parent_like.Get(), after_dict, child.Get()))
    return nullptr;

  return FPDFBookmarkFromCPDFDictionary(child.Get());
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFBookmark_Move(FPDF_DOCUMENT doc,
                  FPDF_BOOKMARK bookmark,
                  FPDF_BOOKMARK new_parent,
                  FPDF_BOOKMARK after_sibling) {
  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(doc);
  RetainPtr<CPDF_Dictionary> bm(
      pdfium::WrapRetain(CPDFDictionaryFromFPDFBookmark(bookmark)));
  if (!pDoc || !bm || !BelongsTo(pDoc, bm.Get()))
    return false;

  RetainPtr<CPDF_Dictionary> parent_like =
      new_parent ? pdfium::WrapRetain(CPDFDictionaryFromFPDFBookmark(new_parent))
                 : GetOrCreateOutlines(pDoc);
  if (!parent_like || !BelongsTo(pDoc, parent_like.Get()))
    return false;

  CPDF_Dictionary* after_dict =
      after_sibling ? CPDFDictionaryFromFPDFBookmark(after_sibling) : nullptr;
  if (after_dict && !BelongsTo(pDoc, after_dict))
    return false;

  // Prevent cycles if parent_like is a bookmark node.
  RetainPtr<CPDF_Dictionary> outlines = GetOrCreateOutlines(pDoc);
  const bool parent_is_outlines = (parent_like.Get() == outlines.Get());
  if (!parent_is_outlines && IsDescendant(bm.Get(), parent_like.Get()))
    return false;

  // Validate after_sibling belongs under parent_like if provided.
  if (after_dict) {
    RetainPtr<const CPDF_Dictionary> par = after_dict->GetDictFor("Parent");
    if (!par || par.Get() != parent_like.Get())
      return false;
    if (after_dict == bm.Get())
      return false;
  }

  UnlinkFromParent(pDoc, bm.Get());
  return InsertAfter(pDoc, parent_like.Get(), after_dict, bm.Get());
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFBookmark_Clear(FPDF_DOCUMENT fdoc) {
  CPDF_Document* doc = CPDFDocumentFromFPDFDocument(fdoc);
  if (!doc)
    return false;

  CPDF_Dictionary* root = doc->GetMutableRoot();
  if (!root)
    return false;

  RetainPtr<CPDF_Dictionary> outlines = root->GetMutableDictFor("Outlines");
  if (!outlines)
    return true;  // nothing to clear

  // Delete all top-level nodes.
  for (RetainPtr<CPDF_Dictionary> cur = outlines->GetMutableDictFor("First"); cur; ) {
    RetainPtr<CPDF_Dictionary> next = cur->GetMutableDictFor("Next");
    DeleteSubtree(doc, cur.Get());
    cur = std::move(next);
  }

  // Remove /Outlines from root and delete the dict object.
  root->RemoveFor("Outlines");
  const uint32_t num = outlines->GetObjNum();
  if (num)
    doc->DeleteIndirectObject(num);

  return true;
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