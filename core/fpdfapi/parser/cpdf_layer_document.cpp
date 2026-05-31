// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/parser/cpdf_layer_document.h"

#include <algorithm>
#include <utility>

#include "core/fpdfapi/page/cpdf_docpagedata.h"
#include "core/fpdfapi/parser/cpdf_base_document.h"
#include "core/fpdfapi/parser/cpdf_concat_read_stream.h"
#include "core/fpdfapi/parser/cpdf_cross_ref_table.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_object.h"
#include "core/fpdfapi/parser/cpdf_parser.h"
#include "core/fpdfapi/render/cpdf_docrenderdata.h"
#include "core/fxcrt/check.h"
#include "core/fxcrt/fx_stream.h"
#include "core/fxcrt/notreached.h"
#include "core/fxcrt/unowned_ptr.h"

namespace {

class DeltaParseObjectHolder final : public CPDF_Parser::ParsedObjectsHolder {
 public:
  DeltaParseObjectHolder() = default;
  ~DeltaParseObjectHolder() override = default;

  void SetParser(CPDF_Parser* parser) { parser_ = parser; }
  bool TryInit() override { return true; }

 protected:
  RetainPtr<CPDF_Object> ParseIndirectObject(uint32_t objnum) override {
    return parser_ ? parser_->ParseIndirectObject(objnum) : nullptr;
  }

 private:
  UnownedPtr<CPDF_Parser> parser_;
};

bool IsBaseObjectLive(const CPDF_Parser* base_parser, uint32_t objnum) {
  return objnum != 0 && base_parser->IsValidObjectNumber(objnum) &&
         !base_parser->IsObjectFree(objnum);
}

bool IsObjectOwnedByAppendedDelta(const CPDF_CrossRefTable* table,
                                  uint32_t objnum,
                                  const CPDF_CrossRefTable::ObjectInfo& info,
                                  FX_FILESIZE layer_append_base_offset) {
  if (objnum == table->trailer_object_number()) {
    return false;
  }

  switch (info.type) {
    case CPDF_CrossRefTable::ObjectType::kFree:
      return false;
    case CPDF_CrossRefTable::ObjectType::kNormal:
      return info.pos >= layer_append_base_offset;
    case CPDF_CrossRefTable::ObjectType::kCompressed: {
      const CPDF_CrossRefTable::ObjectInfo* archive_info =
          table->GetObjectInfo(info.archive.obj_num);
      return archive_info &&
             archive_info->type == CPDF_CrossRefTable::ObjectType::kNormal &&
             archive_info->pos >= layer_append_base_offset;
    }
  }
}

}  // namespace

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
  RetainPtr<CPDF_Dictionary> current_info = GetInfo();
  if (!current_info) {
    return nullptr;
  }
  const uint32_t info_objnum = current_info->GetObjNum();
  DCHECK_NE(CPDF_Object::kInvalidObjNum, info_objnum);
  DCHECK_NE(0u, info_objnum);
  RetainPtr<CPDF_Object> live = GetMutableIndirectObject(info_objnum);
  RetainPtr<CPDF_Dictionary> info =
      live ? pdfium::WrapRetain(live->AsMutableDictionary()) : nullptr;
  SetCachedInfoDict(info);
  return info;
}

RetainPtr<const CPDF_Dictionary> CPDF_LayerDocument::GetPageDictionary(
    int iPage) {
  if (iPage < 0 || static_cast<size_t>(iPage) >= GetPageListSize()) {
    return nullptr;
  }

  const uint32_t objnum = GetPageObjNumAt(iPage);
  if (!objnum) {
    return nullptr;
  }

  return ToDictionary(GetOrParseIndirectObject(objnum));
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

FX_FILESIZE CPDF_LayerDocument::GetLayerAppendBaseOffset() const {
  return base_->GetLayerAppendBaseOffset();
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
    if (!file_access_) {
      return;
    }
    FailDeltaIngest(OpenStatus::kOpenFailed);
    return;
  }

  const FX_FILESIZE delta_size = file_access_->GetSize();
  if (delta_size == 0) {
    file_access_.Reset();
    return;
  }

  RetainPtr<IFX_SeekableReadStream> base_file = base_parser->GetFileAccess();
  if (!base_file) {
    FailDeltaIngest(OpenStatus::kOpenFailed);
    return;
  }

  const FX_FILESIZE layer_append_base_offset =
      base_->GetLayerAppendBaseOffset();
  DeltaParseObjectHolder temp_holder;
  CPDF_Parser parser(&temp_holder);
  temp_holder.SetParser(&parser);
  CPDF_Parser::Error parse_error =
      parser.StartParse(pdfium::MakeRetain<CPDF_ConcatReadStream>(
                            std::move(base_file), file_access_),
                        base_parser->GetPassword());
  if (parse_error != CPDF_Parser::SUCCESS) {
    FailDeltaIngest(OpenStatus::kMalformedDelta);
    return;
  }
  if (parser.GetLastXRefOffset() < layer_append_base_offset) {
    FailDeltaIngest(OpenStatus::kMalformedDelta);
    return;
  }

  const CPDF_CrossRefTable* table = parser.GetCrossRefTable();
  if (!table) {
    FailDeltaIngest(OpenStatus::kMalformedDelta);
    return;
  }

  for (const auto& [objnum, info] : table->objects_info()) {
    if (info.type == CPDF_CrossRefTable::ObjectType::kFree &&
        IsBaseObjectLive(base_parser, objnum)) {
      FailDeltaIngest(OpenStatus::kMalformedDelta);
      return;
    }
  }

  size_t selected_delta_object_count = 0;
  for (const auto& [objnum, info] : table->objects_info()) {
    if (!IsObjectOwnedByAppendedDelta(table, objnum, info,
                                      layer_append_base_offset)) {
      continue;
    }

    RetainPtr<CPDF_Object> parsed = parser.ParseIndirectObject(objnum);
    if (!parsed) {
      FailDeltaIngest(OpenStatus::kMalformedDelta);
      return;
    }

    RetainPtr<CPDF_Object> clone = parsed->CloneForHolder(this);
    if (!clone) {
      FailDeltaIngest(OpenStatus::kMalformedDelta);
      return;
    }
    clone->SetGenNum(info.gennum);
    AddPromotedObject(objnum, std::move(clone));
    ++selected_delta_object_count;
  }

  if (FindLocalIndirectObject(base_parser->GetRootObjNum())) {
    InvalidateCachedRootDict();
  }
  if (FindLocalIndirectObject(base_parser->GetInfoObjNum())) {
    InvalidateCachedInfoDict();
  }
  const uint32_t delta_info_objnum = parser.GetInfoObjNum();
  if (delta_info_objnum && delta_info_objnum != CPDF_Object::kInvalidObjNum &&
      delta_info_objnum != base_parser->GetInfoObjNum()) {
    RetainPtr<CPDF_Object> local_info =
        FindLocalIndirectObject(delta_info_objnum);
    RetainPtr<CPDF_Dictionary> info =
        local_info ? pdfium::WrapRetain(local_info->AsMutableDictionary())
                   : nullptr;
    if (!info) {
      FailDeltaIngest(OpenStatus::kMalformedDelta);
      return;
    }
    SetCachedInfoDict(info);
  }
  if (selected_delta_object_count > 0 &&
      !RebuildPageListFromCurrentPageTree()) {
    FailDeltaIngest(OpenStatus::kMalformedDelta);
    return;
  }
  file_access_.Reset();
}

void CPDF_LayerDocument::FailDeltaIngest(OpenStatus status) {
  ingest_status_ = status;
  file_access_.Reset();
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
