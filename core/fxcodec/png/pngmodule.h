// Copyright 2025 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FXCODEC_PNG_PNGMODULE_H_
#define CORE_FXCODEC_PNG_PNGMODULE_H_

#include <stdint.h>

#include <optional>

#include "core/fxcrt/data_vector.h"
#include "core/fxcrt/span.h"

namespace fxcodec {

class PngModule {
 public:
  struct DecodedImage {
    uint32_t width;
    uint32_t height;
    DataVector<uint8_t> rgb;    // width * height * 3 bytes (DeviceRGB)
    DataVector<uint8_t> alpha;  // width * height bytes (empty if no alpha)
  };

  static std::optional<DecodedImage> Decode(
      pdfium::span<const uint8_t> png_data);

  PngModule() = delete;
  PngModule(const PngModule&) = delete;
  PngModule& operator=(const PngModule&) = delete;
};

}  // namespace fxcodec

using PngModule = fxcodec::PngModule;

#endif  // CORE_FXCODEC_PNG_PNGMODULE_H_
