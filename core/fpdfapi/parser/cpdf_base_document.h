// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFAPI_PARSER_CPDF_BASE_DOCUMENT_H_
#define CORE_FPDFAPI_PARSER_CPDF_BASE_DOCUMENT_H_

#include <stdint.h>

#include <array>

#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fxcrt/fx_types.h"
#include "core/fxcrt/retain_ptr.h"

class IFX_SeekableReadStream;

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
  const std::array<uint8_t, 32>& GetRawBaseSha256() const {
    return raw_base_sha256_;
  }

 private:
  CPDF_BaseDocument();
  ~CPDF_BaseDocument() override;

  bool CacheBaseIdentity();

  FX_FILESIZE raw_base_size_ = 0;
  // PDFium parser offsets are logical PDF offsets after the syntax parser's
  // header offset has been subtracted. Layer append-only xref offsets must use
  // the same coordinate system, not the raw stream byte size.
  FX_FILESIZE layer_append_base_offset_ = 0;
  std::array<uint8_t, 32> raw_base_sha256_ = {};
};

#endif  // CORE_FPDFAPI_PARSER_CPDF_BASE_DOCUMENT_H_
