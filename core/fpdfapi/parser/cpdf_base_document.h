// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_FPDFAPI_PARSER_CPDF_BASE_DOCUMENT_H_
#define CORE_FPDFAPI_PARSER_CPDF_BASE_DOCUMENT_H_

#include <stdint.h>

#include <array>
#include <vector>

#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fxcrt/check.h"
#include "core/fxcrt/fx_types.h"
#include "core/fxcrt/retain_ptr.h"

class IFX_SeekableReadStream;
class CPDF_LayerDocument;

class CPDF_BaseDocument final : public CPDF_Document, public Retainable {
 public:
  CONSTRUCT_VIA_MAKE_RETAIN;

  CPDF_Parser::Error LoadBaseDoc(RetainPtr<IFX_SeekableReadStream> file_access,
                                 const ByteString& password);
  bool EagerlyParseAllReachable();

  RetainPtr<const CPDF_Object> GetFrozenObjectForLayer(uint32_t objnum) const;
  FX_FILESIZE GetRawBaseSize() const { return raw_base_size_; }
  FX_FILESIZE GetLayerAppendBaseOffset() const override {
    return layer_append_base_offset_;
  }
  const CPDF_BaseDocument* GetBaseDocumentForViewScope() const override {
    return this;
  }
  const std::array<uint8_t, 32>& GetRawBaseSha256() const {
    return raw_base_sha256_;
  }

#if DCHECK_IS_ON()
  void RegisterLiveLayer(const CPDF_LayerDocument* layer);
  void UnregisterLiveLayer(const CPDF_LayerDocument* layer);
#endif

 private:
  CPDF_BaseDocument();
  ~CPDF_BaseDocument() override;

  bool CacheBaseIdentity();

  // CPDF_IndirectObjectHolder:
  // This override is intentionally limited to the non-const reference
  // resolution path. Const base lookups must remain frozen so layer promotion
  // always clones from the shared base rather than resolving back into the
  // layer overlay.
  CPDF_Object* GetOrParseIndirectObjectInternal(uint32_t objnum) override;

#if DCHECK_IS_ON()
  bool IsObjectPromotedInAnyLiveLayer(uint32_t objnum) const;
  std::vector<const CPDF_LayerDocument*> live_layers_;
#endif

  FX_FILESIZE raw_base_size_ = 0;
  // PDFium parser offsets are logical PDF offsets after the syntax parser's
  // header offset has been subtracted. Layer append-only xref offsets must use
  // the same coordinate system, not the raw stream byte size.
  FX_FILESIZE layer_append_base_offset_ = 0;
  std::array<uint8_t, 32> raw_base_sha256_ = {};
};

#endif  // CORE_FPDFAPI_PARSER_CPDF_BASE_DOCUMENT_H_
