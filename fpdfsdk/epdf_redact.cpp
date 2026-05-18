// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "public/epdf_redact.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "constants/annotation_common.h"
#include "core/fpdfapi/edit/cpdf_text_redactor.h"
#include "core/fpdfapi/page/cpdf_annotcontext.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/fpdf_parser_utility.h"
#include "core/fpdfdoc/cpdf_annot.h"
#include "core/fpdfdoc/cpdf_interactiveform.h"
#include "core/fxcrt/bytestring.h"
#include "core/fxcrt/containers/contains.h"
#include "core/fxcrt/numerics/safe_conversions.h"
#include "fpdfsdk/cpdfsdk_helpers.h"
#include "fpdfsdk/epdf_page_content_helpers.h"

namespace {

const CPDF_Dictionary* GetAnnotDictFromFPDFAnnotation(
    const FPDF_ANNOTATION annot) {
  CPDF_AnnotContext* context = CPDFAnnotContextFromFPDFAnnotation(annot);
  return context ? context->GetAnnotDict() : nullptr;
}

std::vector<CFX_FloatRect> GetRedactRectsFromAnnotDict(
    const CPDF_Dictionary* annot_dict) {
  std::vector<CFX_FloatRect> rects;
  if (!annot_dict) {
    return rects;
  }

  RetainPtr<const CPDF_Array> quad_points_array =
      annot_dict->GetArrayFor("QuadPoints");
  if (quad_points_array && quad_points_array->size() >= 8) {
    size_t quad_count = CPDF_Annot::QuadPointCount(quad_points_array.Get());
    for (size_t i = 0; i < quad_count; ++i) {
      CFX_FloatRect rect = CPDF_Annot::RectFromQuadPoints(annot_dict, i);
      rect.Normalize();
      if (!rect.IsEmpty()) {
        rects.push_back(rect);
      }
    }
    if (!rects.empty()) {
      return rects;
    }
  }

  CFX_FloatRect rect = annot_dict->GetRectFor(pdfium::annotation::kRect);
  rect.Normalize();
  if (!rect.IsEmpty()) {
    rects.push_back(rect);
  }

  return rects;
}

struct RemovedAnnotCandidate {
  size_t index = 0;
  uint32_t object_number = 0;
  ByteString nm_utf8;
  RetainPtr<CPDF_Dictionary> dict;
};

struct RedactionReportBuffers {
  EPDF_RemovedAnnotInfo* removed = nullptr;
  uint32_t removed_capacity = 0;
  char* nm_utf8_pool = nullptr;
  uint32_t nm_utf8_pool_capacity = 0;
  uint32_t* written_count = nullptr;
  uint32_t* total_count = nullptr;
  uint32_t* nm_utf8_bytes_used = nullptr;
};

uint32_t GetAnnotObjectNumber(const CPDF_Object* entry,
                              const CPDF_Dictionary* dict) {
  if (entry && entry->IsReference()) {
    return entry->AsReference()->GetRefObjNum();
  }
  return dict ? dict->GetObjNum() : 0;
}

ByteString GetAnnotNMUtf8(const CPDF_Dictionary* dict) {
  if (!dict || !dict->KeyExist("NM")) {
    return ByteString();
  }
  return dict->GetUnicodeTextFor("NM").ToUTF8();
}

bool RectsIntersectWithPositiveArea(CFX_FloatRect a, CFX_FloatRect b) {
  a.Normalize();
  b.Normalize();
  a.Intersect(b);
  return !a.IsEmpty();
}

bool AnnotIntersectsAny(const CPDF_Dictionary* annot_dict,
                        pdfium::span<const CFX_FloatRect> redact_rects) {
  if (!annot_dict) {
    return false;
  }
  CFX_FloatRect annot_rect =
      annot_dict->GetRectFor(pdfium::annotation::kRect);
  annot_rect.Normalize();
  if (annot_rect.IsEmpty()) {
    return false;
  }
  for (const CFX_FloatRect& redact_rect : redact_rects) {
    if (RectsIntersectWithPositiveArea(annot_rect, redact_rect)) {
      return true;
    }
  }
  return false;
}

bool CandidateExistsAtIndex(
    const std::vector<RemovedAnnotCandidate>& candidates,
    size_t index) {
  return pdfium::Contains(candidates, index, &RemovedAnnotCandidate::index);
}

bool AddRemovalCandidate(CPDF_Page* page,
                         size_t index,
                         std::vector<RemovedAnnotCandidate>* candidates) {
  if (!page || !candidates || CandidateExistsAtIndex(*candidates, index)) {
    return false;
  }
  RetainPtr<CPDF_Array> annots = page->GetMutableAnnotsArray();
  if (!annots || index >= annots->size()) {
    return false;
  }
  RetainPtr<CPDF_Object> entry = annots->GetMutableObjectAt(index);
  RetainPtr<CPDF_Dictionary> dict =
      ToDictionary(entry ? entry->GetMutableDirect() : nullptr);
  if (!dict) {
    return false;
  }

  RemovedAnnotCandidate candidate;
  candidate.index = index;
  candidate.object_number = GetAnnotObjectNumber(entry.Get(), dict.Get());
  candidate.nm_utf8 = GetAnnotNMUtf8(dict.Get());
  candidate.dict = std::move(dict);
  candidates->push_back(std::move(candidate));
  return true;
}

int FindAnnotIndexOnPageByObjNumOrDict(const CPDF_Page* page,
                                       const CPDF_Dictionary* annot_dict) {
  if (!page || !annot_dict) {
    return -1;
  }
  RetainPtr<const CPDF_Array> annots = page->GetAnnotsArray();
  if (!annots) {
    return -1;
  }
  const uint32_t target_objnum = annot_dict->GetObjNum();
  for (size_t i = 0; i < annots->size(); ++i) {
    RetainPtr<const CPDF_Dictionary> current = annots->GetDictAt(i);
    if (!current) {
      continue;
    }
    if (current.Get() == annot_dict ||
        (target_objnum != 0 && current->GetObjNum() == target_objnum)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void AddPopupCascade(CPDF_Page* page,
                     std::vector<RemovedAnnotCandidate>* candidates) {
  if (!page || !candidates) {
    return;
  }
  for (size_t i = 0; i < candidates->size(); ++i) {
    RetainPtr<const CPDF_Dictionary> popup =
        candidates->at(i).dict ? candidates->at(i).dict->GetDictFor("Popup")
                               : nullptr;
    if (!popup) {
      continue;
    }
    const int popup_index =
        FindAnnotIndexOnPageByObjNumOrDict(page, popup.Get());
    if (popup_index >= 0) {
      AddRemovalCandidate(page, static_cast<size_t>(popup_index), candidates);
    }
  }
}

bool RemoveDictFromArray(CPDF_Array* array, const CPDF_Dictionary* dict) {
  if (!array || !dict) {
    return false;
  }
  bool removed = false;
  const uint32_t objnum = dict->GetObjNum();
  for (size_t i = array->size(); i > 0; --i) {
    RetainPtr<const CPDF_Dictionary> item = array->GetDictAt(i - 1);
    if (item &&
        (item.Get() == dict || (objnum != 0 && item->GetObjNum() == objnum))) {
      array->RemoveAt(i - 1);
      removed = true;
    }
  }
  return removed;
}

RetainPtr<CPDF_Array> GetAcroFormFields(CPDF_Document* doc) {
  if (!doc) {
    return nullptr;
  }
  RetainPtr<CPDF_Dictionary> root = doc->GetMutableRoot();
  if (!root) {
    return nullptr;
  }
  RetainPtr<CPDF_Dictionary> acro_form = root->GetMutableDictFor("AcroForm");
  return acro_form ? acro_form->GetMutableArrayFor("Fields") : nullptr;
}

bool DetachWidgetFromAcroForm(CPDF_Document* doc,
                              CPDF_Dictionary* widget_dict) {
  if (!doc || !widget_dict ||
      widget_dict->GetNameFor(pdfium::annotation::kSubtype) != "Widget") {
    return false;
  }

  bool changed = false;
  RetainPtr<CPDF_Dictionary> parent = widget_dict->GetMutableDictFor("Parent");
  if (!parent) {
    RetainPtr<CPDF_Array> fields = GetAcroFormFields(doc);
    return fields ? RemoveDictFromArray(fields.Get(), widget_dict) : false;
  }

  RetainPtr<CPDF_Array> kids = parent->GetMutableArrayFor("Kids");
  if (kids) {
    changed |= RemoveDictFromArray(kids.Get(), widget_dict);
  }

  RetainPtr<CPDF_Dictionary> current = parent;
  while (current) {
    RetainPtr<CPDF_Array> current_kids = current->GetMutableArrayFor("Kids");
    if (current_kids && !current_kids->IsEmpty()) {
      break;
    }

    RetainPtr<CPDF_Dictionary> next_parent =
        current->GetMutableDictFor("Parent");
    if (next_parent) {
      RetainPtr<CPDF_Array> parent_kids =
          next_parent->GetMutableArrayFor("Kids");
      if (parent_kids) {
        changed |= RemoveDictFromArray(parent_kids.Get(), current.Get());
      }
      current = std::move(next_parent);
      continue;
    }

    RetainPtr<CPDF_Array> fields = GetAcroFormFields(doc);
    if (fields) {
      changed |= RemoveDictFromArray(fields.Get(), current.Get());
    }
    break;
  }

  return changed;
}

void DetachWidgetsFromAcroForm(
    CPDF_Page* page,
    const std::vector<RemovedAnnotCandidate>& candidates) {
  if (!page) {
    return;
  }
  CPDF_Document* doc = page->GetDocument();
  if (!doc) {
    return;
  }
  bool changed = false;
  for (const RemovedAnnotCandidate& candidate : candidates) {
    if (candidate.dict &&
        candidate.dict->GetNameFor(pdfium::annotation::kSubtype) == "Widget") {
      changed |= DetachWidgetFromAcroForm(doc, candidate.dict.Get());
    }
  }
  if (changed) {
    CPDF_InteractiveForm form(doc);
    form.FixPageFields(page);
  }
}

void WriteRemovalReport(const std::vector<RemovedAnnotCandidate>& candidates,
                        const RedactionReportBuffers* report) {
  if (!report) {
    return;
  }

  const uint32_t total =
      pdfium::checked_cast<uint32_t>(candidates.size());
  uint32_t written = 0;
  uint32_t nm_bytes_used = 0;
  const uint32_t capacity = report->removed ? report->removed_capacity : 0;
  const uint32_t limit = std::min(total, capacity);

  for (; written < limit; ++written) {
    const RemovedAnnotCandidate& candidate = candidates[written];
    EPDF_RemovedAnnotInfo& out = report->removed[written];
    out.object_number = candidate.object_number;
    out.index_at_removal =
        pdfium::checked_cast<uint32_t>(candidate.index);
    out.nm_utf8_offset = 0;
    out.nm_utf8_len = 0;

    const uint32_t nm_len =
        pdfium::checked_cast<uint32_t>(candidate.nm_utf8.GetLength());
    if (nm_len == 0) {
      continue;
    }
    if (!report->nm_utf8_pool ||
        nm_len > report->nm_utf8_pool_capacity - nm_bytes_used) {
      out.nm_utf8_len = EPDF_REMOVED_ANNOT_NM_UTF8_OVERFLOW;
      continue;
    }

    out.nm_utf8_offset = nm_bytes_used;
    out.nm_utf8_len = nm_len;
    memcpy(report->nm_utf8_pool + nm_bytes_used, candidate.nm_utf8.c_str(),
           nm_len);
    nm_bytes_used += nm_len;
  }

  if (report->written_count) {
    *report->written_count = written;
  }
  if (report->total_count) {
    *report->total_count = total;
  }
  if (report->nm_utf8_bytes_used) {
    *report->nm_utf8_bytes_used = nm_bytes_used;
  }
}

void RemoveCandidatesFromPage(
    CPDF_Page* page,
    const std::vector<RemovedAnnotCandidate>& candidates) {
  if (!page) {
    return;
  }
  RetainPtr<CPDF_Array> annots = page->GetMutableAnnotsArray();
  if (!annots) {
    return;
  }
  CPDF_Document* doc = page->GetDocument();
  for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
    if (it->index >= annots->size()) {
      continue;
    }
    annots->RemoveAt(it->index);
    if (doc && it->object_number) {
      doc->DeleteIndirectObject(it->object_number);
    }
  }
}

void SortCandidatesByOriginalIndex(
    std::vector<RemovedAnnotCandidate>* candidates) {
  std::sort(candidates->begin(), candidates->end(),
            [](const RemovedAnnotCandidate& a,
               const RemovedAnnotCandidate& b) { return a.index < b.index; });
}

bool ApplySingleRedactionCore(CPDF_Page* page,
                              const CPDF_Dictionary* redact_dict,
                              const RedactionReportBuffers* report) {
  if (!page || !redact_dict) {
    return false;
  }

  std::vector<CFX_FloatRect> rects = GetRedactRectsFromAnnotDict(redact_dict);
  if (rects.empty()) {
    return false;
  }

  std::vector<RemovedAnnotCandidate> removals;
  RetainPtr<CPDF_Array> annots = page->GetMutableAnnotsArray();
  if (annots) {
    for (size_t i = 0; i < annots->size(); ++i) {
      RetainPtr<CPDF_Object> entry = annots->GetMutableObjectAt(i);
      RetainPtr<CPDF_Dictionary> annot_dict =
          ToDictionary(entry ? entry->GetMutableDirect() : nullptr);
      if (!annot_dict) {
        continue;
      }
      if (annot_dict->GetNameFor(pdfium::annotation::kSubtype) == "Redact") {
        continue;
      }
      if (AnnotIntersectsAny(annot_dict.Get(), pdfium::span(rects))) {
        AddRemovalCandidate(page, i, &removals);
      }
    }

    AddPopupCascade(page, &removals);

    const int redact_index =
        FindAnnotIndexOnPageByObjNumOrDict(page, redact_dict);
    if (redact_index >= 0) {
      AddRemovalCandidate(page, static_cast<size_t>(redact_index), &removals);
    }
  }

  SortCandidatesByOriginalIndex(&removals);

  RedactTextInRects(page, pdfium::span(rects),
                    /*recurse_forms=*/true,
                    /*draw_black_boxes=*/false);

  RetainPtr<const CPDF_Stream> ro_stream = redact_dict->GetStreamFor("RO");
  if (ro_stream) {
    CFX_FloatRect annot_rect =
        redact_dict->GetRectFor(pdfium::annotation::kRect);
    annot_rect.Normalize();
    EpdfAppendFormXObjectToPage(page, ro_stream, annot_rect);
  }

  DetachWidgetsFromAcroForm(page, removals);
  WriteRemovalReport(removals, report);
  RemoveCandidatesFromPage(page, removals);
  return true;
}

bool ApplyAllRedactionsCore(CPDF_Page* page,
                            const RedactionReportBuffers* report) {
  if (!page) {
    return false;
  }

  RetainPtr<CPDF_Array> annots = page->GetMutableAnnotsArray();
  if (!annots || annots->IsEmpty()) {
    return false;
  }

  std::vector<CFX_FloatRect> all_rects;
  std::vector<std::pair<RetainPtr<const CPDF_Stream>, CFX_FloatRect>>
      ro_streams;
  std::vector<RemovedAnnotCandidate> removals;

  for (size_t i = 0; i < annots->size(); ++i) {
    RetainPtr<CPDF_Object> entry = annots->GetMutableObjectAt(i);
    RetainPtr<CPDF_Dictionary> annot_dict =
        ToDictionary(entry ? entry->GetMutableDirect() : nullptr);
    if (!annot_dict ||
        annot_dict->GetNameFor(pdfium::annotation::kSubtype) != "Redact") {
      continue;
    }

    AddRemovalCandidate(page, i, &removals);

    std::vector<CFX_FloatRect> rects =
        GetRedactRectsFromAnnotDict(annot_dict.Get());
    for (const CFX_FloatRect& rect : rects) {
      all_rects.push_back(rect);
    }

    RetainPtr<const CPDF_Stream> ro_stream = annot_dict->GetStreamFor("RO");
    if (ro_stream) {
      CFX_FloatRect annot_rect =
          annot_dict->GetRectFor(pdfium::annotation::kRect);
      annot_rect.Normalize();
      ro_streams.push_back({ro_stream, annot_rect});
    }
  }

  if (all_rects.empty()) {
    return false;
  }

  for (size_t i = 0; i < annots->size(); ++i) {
    RetainPtr<CPDF_Object> entry = annots->GetMutableObjectAt(i);
    RetainPtr<CPDF_Dictionary> annot_dict =
        ToDictionary(entry ? entry->GetMutableDirect() : nullptr);
    if (!annot_dict) {
      continue;
    }
    if (annot_dict->GetNameFor(pdfium::annotation::kSubtype) == "Redact") {
      continue;
    }
    if (AnnotIntersectsAny(annot_dict.Get(), pdfium::span(all_rects))) {
      AddRemovalCandidate(page, i, &removals);
    }
  }

  AddPopupCascade(page, &removals);
  SortCandidatesByOriginalIndex(&removals);

  RedactTextInRects(page, pdfium::span(all_rects),
                    /*recurse_forms=*/true,
                    /*draw_black_boxes=*/false);

  for (const auto& [ro_stream, annot_rect] : ro_streams) {
    EpdfAppendFormXObjectToPage(page, ro_stream, annot_rect);
  }

  DetachWidgetsFromAcroForm(page, removals);
  WriteRemovalReport(removals, report);
  RemoveCandidatesFromPage(page, removals);
  return true;
}

}  // namespace

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFAnnot_ApplyRedaction(FPDF_PAGE page, FPDF_ANNOTATION annot) {
  return EPDFAnnot_ApplyRedactionWithReport(
      page, annot, nullptr, 0, nullptr, 0, nullptr, nullptr, nullptr);
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFAnnot_ApplyRedactionWithReport(
    FPDF_PAGE page,
    FPDF_ANNOTATION annot,
    EPDF_RemovedAnnotInfo* out_removed,
    uint32_t out_removed_capacity,
    char* nm_utf8_pool,
    uint32_t nm_utf8_pool_capacity,
    uint32_t* out_written_count,
    uint32_t* out_total_count,
    uint32_t* out_nm_utf8_bytes_used) {
  if (out_written_count) {
    *out_written_count = 0;
  }
  if (out_total_count) {
    *out_total_count = 0;
  }
  if (out_nm_utf8_bytes_used) {
    *out_nm_utf8_bytes_used = 0;
  }

  CPDF_Page* pPage = CPDFPageFromFPDFPage(page);
  if (!pPage) {
    return false;
  }

  const CPDF_Dictionary* annot_dict = GetAnnotDictFromFPDFAnnotation(annot);
  if (!annot_dict ||
      annot_dict->GetNameFor(pdfium::annotation::kSubtype) != "Redact") {
    return false;
  }

  RedactionReportBuffers report = {
      out_removed,
      out_removed_capacity,
      nm_utf8_pool,
      nm_utf8_pool_capacity,
      out_written_count,
      out_total_count,
      out_nm_utf8_bytes_used,
  };
  return ApplySingleRedactionCore(pPage, annot_dict, &report);
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV EPDFPage_ApplyRedactions(FPDF_PAGE page) {
  return EPDFPage_ApplyRedactionsWithReport(page, nullptr, 0, nullptr, 0,
                                            nullptr, nullptr, nullptr);
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_ApplyRedactionsWithReport(
    FPDF_PAGE page,
    EPDF_RemovedAnnotInfo* out_removed,
    uint32_t out_removed_capacity,
    char* nm_utf8_pool,
    uint32_t nm_utf8_pool_capacity,
    uint32_t* out_written_count,
    uint32_t* out_total_count,
    uint32_t* out_nm_utf8_bytes_used) {
  if (out_written_count) {
    *out_written_count = 0;
  }
  if (out_total_count) {
    *out_total_count = 0;
  }
  if (out_nm_utf8_bytes_used) {
    *out_nm_utf8_bytes_used = 0;
  }

  CPDF_Page* pPage = CPDFPageFromFPDFPage(page);
  if (!pPage) {
    return false;
  }

  RedactionReportBuffers report = {
      out_removed,
      out_removed_capacity,
      nm_utf8_pool,
      nm_utf8_pool_capacity,
      out_written_count,
      out_total_count,
      out_nm_utf8_bytes_used,
  };
  return ApplyAllRedactionsCore(pPage, &report);
}
