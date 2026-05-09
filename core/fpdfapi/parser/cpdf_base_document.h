// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFAPI_PARSER_CPDF_BASE_DOCUMENT_H_
#define CORE_FPDFAPI_PARSER_CPDF_BASE_DOCUMENT_H_

#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fxcrt/retain_ptr.h"

class IFX_SeekableReadStream;

class CPDF_BaseDocument final : public CPDF_Document, public Retainable {
 public:
  CONSTRUCT_VIA_MAKE_RETAIN;

  CPDF_Parser::Error LoadBaseDoc(RetainPtr<IFX_SeekableReadStream> file_access,
                                 const ByteString& password);
  bool EagerlyParseAllReachable();

  RetainPtr<const CPDF_Object> GetFrozenObjectForLayer(uint32_t objnum) const;

 private:
  CPDF_BaseDocument();
  ~CPDF_BaseDocument() override;
};

#endif  // CORE_FPDFAPI_PARSER_CPDF_BASE_DOCUMENT_H_
