// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/parser/cpdf_layer_document.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "core/fpdfapi/page/cpdf_docpagedata.h"
#include "core/fpdfapi/parser/cpdf_base_document.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_object.h"
#include "core/fpdfapi/parser/cpdf_parser.h"
#include "core/fpdfapi/render/cpdf_docrenderdata.h"
#include "core/fxcrt/check.h"
#include "core/fxcrt/fx_stream.h"
#include "core/fxcrt/notreached.h"

CPDF_LayerDocument::CPDF_LayerDocument(
    RetainPtr<CPDF_BaseDocument> base,
    RetainPtr<IFX_SeekableReadStream> file_access)
    : CPDF_Document(std::make_unique<CPDF_DocRenderData>(
                        CPDF_DocRenderData::FromDocument(base.Get())),
                    std::make_unique<CPDF_DocPageData>(
                        CPDF_DocPageData::FromDocument(base.Get()))),
      base_(std::move(base)),
      file_access_(std::move(file_access)) {
  CHECK(base_);
  SetLastObjNum(base_->GetLastObjNum());
  InitializeFromBase();
  IngestCurrentDelta();
}

CPDF_LayerDocument::~CPDF_LayerDocument() = default;

// static
CPDF_LayerDocument* CPDF_LayerDocument::FromDocument(CPDF_Document* document) {
  return document && document->IsLayerDocument()
             ? static_cast<CPDF_LayerDocument*>(document)
             : nullptr;
}

// static
const CPDF_LayerDocument* CPDF_LayerDocument::FromDocument(
    const CPDF_Document* document) {
  return document && document->IsLayerDocument()
             ? static_cast<const CPDF_LayerDocument*>(document)
             : nullptr;
}

size_t CPDF_LayerDocument::GetPromotedObjectCount() const {
  return static_cast<size_t>(std::distance(begin(), end()));
}

CPDF_Parser* CPDF_LayerDocument::GetParser() const {
  return base_->GetParser();
}

RetainPtr<CPDF_Dictionary> CPDF_LayerDocument::GetMutableRoot() {
  const uint32_t root_objnum = base_->GetParser()->GetRootObjNum();
  RetainPtr<CPDF_Object> live = GetMutableIndirectObject(root_objnum);
  RetainPtr<CPDF_Dictionary> root =
      live ? pdfium::WrapRetain(live->AsMutableDictionary()) : nullptr;
  SetCachedRootDict(root);
  return root;
}

RetainPtr<CPDF_Dictionary> CPDF_LayerDocument::GetMutableInfo() {
  CPDF_Parser* parser = base_->GetParser();
  const uint32_t info_objnum = parser ? parser->GetInfoObjNum() : 0;
  if (!info_objnum || info_objnum == CPDF_Object::kInvalidObjNum) {
    return nullptr;
  }
  RetainPtr<CPDF_Object> live = GetMutableIndirectObject(info_objnum);
  RetainPtr<CPDF_Dictionary> info =
      live ? pdfium::WrapRetain(live->AsMutableDictionary()) : nullptr;
  SetCachedInfoDict(info);
  return info;
}

uint32_t CPDF_LayerDocument::GetUserPermissions(bool get_owner_perms) const {
  return base_->GetUserPermissions(get_owner_perms);
}

RetainPtr<CPDF_Object> CPDF_LayerDocument::FindPromotedObject(
    uint32_t objnum) const {
  return FindLocalIndirectObject(objnum);
}

bool CPDF_LayerDocument::IsLayerDocument() const {
  return true;
}

RetainPtr<CPDF_Object> CPDF_LayerDocument::ParseIndirectObject(
    uint32_t objnum) {
  NOTREACHED();
  return nullptr;
}

RetainPtr<CPDF_Object> CPDF_LayerDocument::GetMutableIndirectObject(
    uint32_t objnum) {
  if (RetainPtr<CPDF_Object> local = FindLocalIndirectObject(objnum)) {
    return local;
  }
  return PromoteFromBase(objnum);
}

void CPDF_LayerDocument::DeleteIndirectObject(uint32_t objnum) {
  if (FindLocalIndirectObject(objnum)) {
    CPDF_Document::DeleteIndirectObject(objnum);
  }
}

const CPDF_Object* CPDF_LayerDocument::GetIndirectObjectInternal(
    uint32_t objnum) const {
  if (RetainPtr<CPDF_Object> local = FindLocalIndirectObject(objnum)) {
    return local.Get();
  }
  return base_->GetFrozenObjectForLayer(objnum).Get();
}

CPDF_Object* CPDF_LayerDocument::GetOrParseIndirectObjectInternal(
    uint32_t objnum) {
  return const_cast<CPDF_Object*>(GetIndirectObjectInternal(objnum));
}

uint32_t CPDF_LayerDocument::GetPageObjNumAt(size_t index) const {
  CHECK_LT(index, layer_page_list_.size());
  return layer_page_list_[index];
}

void CPDF_LayerDocument::SetPageObjNumAt(size_t index, uint32_t objnum) {
  CHECK_LT(index, layer_page_list_.size());
  layer_page_list_[index] = objnum;
}

void CPDF_LayerDocument::InsertPageObjNum(size_t index, uint32_t objnum) {
  CHECK_LE(index, layer_page_list_.size());
  layer_page_list_.insert(layer_page_list_.begin() + index, objnum);
}

void CPDF_LayerDocument::ErasePageObjNum(size_t index) {
  CHECK_LT(index, layer_page_list_.size());
  layer_page_list_.erase(layer_page_list_.begin() + index);
}

void CPDF_LayerDocument::ResizePageList(size_t size) {
  layer_page_list_.resize(size);
}

size_t CPDF_LayerDocument::GetPageListSize() const {
  return layer_page_list_.size();
}

void CPDF_LayerDocument::InitializeFromBase() {
  SetCachedRootDict(
      pdfium::WrapRetain(const_cast<CPDF_Dictionary*>(base_->GetRoot())));
  SetCachedInfoDict(base_->GetInfo());

  const int page_count = base_->GetPageCount();
  if (page_count < 0) {
    ingest_status_ = OpenStatus::kOpenFailed;
    return;
  }

  layer_page_list_.reserve(static_cast<size_t>(page_count));
  for (int i = 0; i < page_count; ++i) {
    RetainPtr<const CPDF_Dictionary> page = base_->GetPageDictionary(i);
    layer_page_list_.push_back(page ? page->GetObjNum() : 0);
  }
}

void CPDF_LayerDocument::IngestCurrentDelta() {
  if (ingest_status_ != OpenStatus::kSuccess) {
    return;
  }

  CPDF_Parser* base_parser = base_->GetParser();
  if (!base_parser || !file_access_) {
    ingest_status_ = OpenStatus::kOpenFailed;
    return;
  }

  const FX_FILESIZE base_end = base_parser->GetDocumentSize();
  const FX_FILESIZE layer_size = file_access_->GetSize();
  if (layer_size < base_end) {
    ingest_status_ = OpenStatus::kBaseLayerMismatch;
    return;
  }
  if (layer_size > base_end) {
    // Full appended-xref ingest lands with the delta parser. Until then, fail
    // closed instead of silently ignoring a caller-provided delta.
    ingest_status_ = OpenStatus::kMalformedDelta;
  }
}

RetainPtr<CPDF_Object> CPDF_LayerDocument::PromoteFromBase(uint32_t objnum) {
  if (!objnum || objnum == CPDF_Object::kInvalidObjNum) {
    return nullptr;
  }
  if (RetainPtr<CPDF_Object> local = FindLocalIndirectObject(objnum)) {
    return local;
  }

  RetainPtr<const CPDF_Object> base_object =
      base_->GetFrozenObjectForLayer(objnum);
  if (!base_object) {
    return nullptr;
  }

  RetainPtr<CPDF_Object> clone = base_object->CloneForHolder(this);
  if (!clone) {
    return nullptr;
  }
  clone->SetGenNum(base_object->GetGenNum());
  AddPromotedObject(objnum, clone);

  CPDF_Parser* parser = base_->GetParser();
  if (parser) {
    if (parser->GetRootObjNum() == objnum) {
      InvalidateCachedRootDict();
    }
    if (parser->GetInfoObjNum() == objnum) {
      InvalidateCachedInfoDict();
    }
  }

  return clone;
}
