// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFAPI_PARSER_CPDF_LAYER_DOCUMENT_H_
#define CORE_FPDFAPI_PARSER_CPDF_LAYER_DOCUMENT_H_

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fxcrt/retain_ptr.h"

class CPDF_BaseDocument;
class IFX_SeekableReadStream;

class CPDF_LayerDocument final : public CPDF_Document {
 public:
  enum class OpenStatus {
    kSuccess,
    kMalformedDelta,
    kBaseLayerMismatch,
    kOpenFailed,
  };

  CPDF_LayerDocument(RetainPtr<CPDF_BaseDocument> base,
                     RetainPtr<IFX_SeekableReadStream> file_access);
  ~CPDF_LayerDocument() override;

  static CPDF_LayerDocument* FromDocument(CPDF_Document* document);
  static const CPDF_LayerDocument* FromDocument(const CPDF_Document* document);

  OpenStatus ingest_status() const { return ingest_status_; }
  size_t GetPromotedObjectCount() const;
  CPDF_BaseDocument* GetBaseDocument() const { return base_.Get(); }

  // CPDF_Document:
  CPDF_Parser* GetParser() const override;
  RetainPtr<CPDF_Dictionary> GetMutableRoot() override;
  RetainPtr<CPDF_Dictionary> GetMutableInfo() override;
  uint32_t GetUserPermissions(bool get_owner_perms) const override;
  RetainPtr<CPDF_Object> FindPromotedObject(uint32_t objnum) const override;
  bool IsLayerDocument() const override;

  // CPDF_Parser::ParsedObjectsHolder:
  RetainPtr<CPDF_Object> ParseIndirectObject(uint32_t objnum) override;
  RetainPtr<CPDF_Object> GetMutableIndirectObject(uint32_t objnum) override;
  void DeleteIndirectObject(uint32_t objnum) override;

 protected:
  // CPDF_IndirectObjectHolder:
  const CPDF_Object* GetIndirectObjectInternal(uint32_t objnum) const override;
  CPDF_Object* GetOrParseIndirectObjectInternal(uint32_t objnum) override;

  // CPDF_Document page-list storage:
  uint32_t GetPageObjNumAt(size_t index) const override;
  void SetPageObjNumAt(size_t index, uint32_t objnum) override;
  void InsertPageObjNum(size_t index, uint32_t objnum) override;
  void ErasePageObjNum(size_t index) override;
  void ResizePageList(size_t size) override;
  size_t GetPageListSize() const override;

 private:
  void InitializeFromBase();
  void IngestCurrentDelta();

  RetainPtr<CPDF_BaseDocument> const base_;
  RetainPtr<IFX_SeekableReadStream> const file_access_;
  std::vector<uint32_t> layer_page_list_;
  OpenStatus ingest_status_ = OpenStatus::kSuccess;
};

#endif  // CORE_FPDFAPI_PARSER_CPDF_LAYER_DOCUMENT_H_
