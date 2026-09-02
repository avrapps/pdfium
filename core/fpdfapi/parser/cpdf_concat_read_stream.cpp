// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "core/fpdfapi/parser/cpdf_concat_read_stream.h"

#include <algorithm>
#include <utility>

#include "core/fxcrt/fx_safe_types.h"
#include "core/fxcrt/numerics/safe_conversions.h"

CPDF_ConcatReadStream::CPDF_ConcatReadStream(
    RetainPtr<IFX_SeekableReadStream> first,
    RetainPtr<IFX_SeekableReadStream> second)
    : first_(std::move(first)),
      second_(std::move(second)),
      first_size_(first_ ? first_->GetSize() : 0),
      second_size_(second_ ? second_->GetSize() : 0) {}

CPDF_ConcatReadStream::~CPDF_ConcatReadStream() = default;

FX_FILESIZE CPDF_ConcatReadStream::GetSize() {
  FX_SAFE_FILESIZE size = first_size_;
  size += second_size_;
  return size.ValueOrDefault(0);
}

bool CPDF_ConcatReadStream::ReadBlockAtOffset(pdfium::span<uint8_t> buffer,
                                              FX_FILESIZE offset) {
  if (offset < 0) {
    return false;
  }
  if (buffer.empty()) {
    return offset <= GetSize();
  }

  FX_SAFE_FILESIZE safe_end = offset;
  safe_end += buffer.size();
  if (!safe_end.IsValid() || safe_end.ValueOrDie() > GetSize()) {
    return false;
  }

  if (offset < first_size_) {
    const FX_FILESIZE remaining_first_size = first_size_ - offset;
    const size_t first_read_size =
        pdfium::IsValueInRangeForNumericType<size_t>(remaining_first_size)
            ? std::min(buffer.size(),
                       pdfium::checked_cast<size_t>(remaining_first_size))
            : buffer.size();
    if (!first_ ||
        !first_->ReadBlockAtOffset(buffer.first(first_read_size), offset)) {
      return false;
    }
    buffer = buffer.subspan(first_read_size);
    offset = 0;
  } else {
    offset -= first_size_;
  }

  return buffer.empty() ||
         (second_ && second_->ReadBlockAtOffset(buffer, offset));
}
