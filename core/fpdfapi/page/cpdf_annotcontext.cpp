// Copyright 2018 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fpdfapi/page/cpdf_annotcontext.h"

#include <utility>

#include "core/fpdfapi/page/cpdf_form.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_object.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fxcrt/check.h"
#include "core/fxcrt/check_op.h"

CPDF_AnnotContext::CPDF_AnnotContext(RetainPtr<CPDF_Dictionary> pAnnotDict,
                                     IPDF_Page* pPage,
                                     int annot_index)
    : annot_dict_(std::move(pAnnotDict)),
      page_(pPage),
      annot_index_(annot_index) {
  DCHECK(annot_dict_);
  DCHECK(page_);
  DCHECK(page_->AsPDFPage());
}

CPDF_AnnotContext::~CPDF_AnnotContext() = default;

void CPDF_AnnotContext::SetForm(RetainPtr<CPDF_Stream> pStream) {
  CHECK(pStream);
  annot_form_ = std::make_unique<CPDF_Form>(
      page_->GetDocument(),
      pdfium::WrapRetain(const_cast<CPDF_Dictionary*>(
          page_->AsPDFPage()->GetResources().Get())),
      pStream);

  // The annotation expects the form content to be parsed with the identity
  // matrix (ignoring the matrix defined in the stream). To achieve this without
  // mutating the stream, pass the inverse of the stream's matrix as the parent
  // matrix during parsing. The parent matrix is applied to the stream's matrix,
  // effectively canceling out to the identity matrix.
  CFX_Matrix inverse_stream_matrix =
      pStream->GetDict()->GetMatrixFor("Matrix").GetInverse();
  annot_form_->ParseContent(nullptr, &inverse_stream_matrix, nullptr);
}

RetainPtr<CPDF_Dictionary> CPDF_AnnotContext::GetMutableAnnotDict() {
  CPDF_Page* page = page_ ? page_->AsPDFPage() : nullptr;
  CPDF_Document* doc = page ? page->GetDocument() : nullptr;
  if (!doc) {
    return annot_dict_;
  }

  const uint32_t objnum = annot_dict_->GetObjNum();
  if (objnum != 0) {
    RetainPtr<CPDF_Object> live = doc->GetMutableIndirectObject(objnum);
    if (live && live.Get() != annot_dict_.Get()) {
      annot_dict_ = pdfium::WrapRetain(live->AsMutableDictionary());
    }
    return annot_dict_;
  }

  if (annot_dict_->IsFrozen()) {
    EnsureMutableBackingForAnnotDict();
  }
  return annot_dict_;
}

void CPDF_AnnotContext::EnsureMutableBackingForAnnotDict() {
  CHECK_GE(annot_index_, 0);
  CPDF_Page* page = page_->AsPDFPage();
  RetainPtr<CPDF_Dictionary> page_dict = page->GetMutableDict();
  RetainPtr<CPDF_Array> annots = page_dict->GetMutableArrayFor("Annots");
  CHECK(annots);
  annot_dict_ = annots->GetMutableDictAt(annot_index_);
  CHECK(annot_dict_);
}
