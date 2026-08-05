// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "fpdfsdk/epdf_page_content_helpers.h"

#include <memory>

#include "core/fpdfapi/page/cpdf_form.h"
#include "core/fpdfapi/page/cpdf_formobject.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/page/cpdf_pageobject.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_stream.h"

void EpdfAppendFormXObjectToPage(CPDF_Page* page,
                                 RetainPtr<const CPDF_Stream> form_stream,
                                 const CFX_FloatRect& target_rect) {
  if (!page || !form_stream) {
    return;
  }

  CPDF_Document* doc = page->GetDocument();
  if (!doc) {
    return;
  }

  RetainPtr<const CPDF_Dictionary> form_dict = form_stream->GetDict();
  if (!form_dict) {
    return;
  }

  CFX_FloatRect form_bbox = form_dict->GetRectFor("BBox");
  form_bbox.Normalize();
  if (form_bbox.IsEmpty()) {
    form_bbox = target_rect;
  }

  float scale_x = 1.0f;
  float scale_y = 1.0f;
  if (form_bbox.Width() > 0) {
    scale_x = target_rect.Width() / form_bbox.Width();
  }
  if (form_bbox.Height() > 0) {
    scale_y = target_rect.Height() / form_bbox.Height();
  }

  CFX_Matrix form_matrix;
  form_matrix.a = scale_x;
  form_matrix.d = scale_y;
  form_matrix.e = target_rect.left - form_bbox.left * scale_x;
  form_matrix.f = target_rect.bottom - form_bbox.bottom * scale_y;

  auto form = std::make_unique<CPDF_Form>(
      doc, page->GetMutableResources(),
      pdfium::WrapRetain(const_cast<CPDF_Stream*>(form_stream.Get())));
  form->ParseContent();

  auto form_obj = std::make_unique<CPDF_FormObject>(
      CPDF_PageObject::kNoContentStream, std::move(form), form_matrix);

  form_obj->CalcBoundingBox();
  form_obj->SetDirty(true);
  page->AppendPageObject(std::move(form_obj));
}
