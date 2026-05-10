// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFAPI_PARSER_CPDF_CONCAT_READ_STREAM_H_
#define CORE_FPDFAPI_PARSER_CPDF_CONCAT_READ_STREAM_H_

#include "core/fxcrt/fx_stream.h"
#include "core/fxcrt/retain_ptr.h"

class CPDF_ConcatReadStream final : public IFX_SeekableReadStream {
 public:
  CONSTRUCT_VIA_MAKE_RETAIN;

  // IFX_SeekableReadStream:
  FX_FILESIZE GetSize() override;
  bool ReadBlockAtOffset(pdfium::span<uint8_t> buffer,
                         FX_FILESIZE offset) override;

 private:
  CPDF_ConcatReadStream(RetainPtr<IFX_SeekableReadStream> first,
                        RetainPtr<IFX_SeekableReadStream> second);
  ~CPDF_ConcatReadStream() override;

  RetainPtr<IFX_SeekableReadStream> const first_;
  RetainPtr<IFX_SeekableReadStream> const second_;
  FX_FILESIZE const first_size_;
  FX_FILESIZE const second_size_;
};

#endif  // CORE_FPDFAPI_PARSER_CPDF_CONCAT_READ_STREAM_H_
