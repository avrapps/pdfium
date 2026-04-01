// Copyright 2025 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fxcodec/png/pngmodule.h"

#include <string.h>

#include <vector>

#include "png.h"

namespace {

struct PngReadCtx {
  const uint8_t* data;
  size_t size;
  size_t offset;
};

void PngReadFromMemory(png_structp png_ptr,
                       png_bytep out_bytes,
                       png_size_t byte_count) {
  auto* ctx = static_cast<PngReadCtx*>(png_get_io_ptr(png_ptr));
  if (ctx->offset + byte_count > ctx->size) {
    png_error(png_ptr, "PNG read past end of buffer");
    return;
  }
  memcpy(out_bytes, ctx->data + ctx->offset, byte_count);
  ctx->offset += byte_count;
}

}  // namespace

namespace fxcodec {

// static
std::optional<PngModule::DecodedImage> PngModule::Decode(
    pdfium::span<const uint8_t> png_data) {
  if (png_data.size() < 8)
    return std::nullopt;

  PngReadCtx ctx = {png_data.data(), png_data.size(), 0};

  png_structp png =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png)
    return std::nullopt;

  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_read_struct(&png, nullptr, nullptr);
    return std::nullopt;
  }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, nullptr);
    return std::nullopt;
  }

  png_set_read_fn(png, &ctx, PngReadFromMemory);
  png_read_info(png, info);

  png_uint_32 w = png_get_image_width(png, info);
  png_uint_32 h = png_get_image_height(png, info);
  png_byte color_type = png_get_color_type(png, info);
  png_byte bit_depth = png_get_bit_depth(png, info);

  if (bit_depth == 16)
    png_set_strip_16(png);
  if (color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb(png);
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    png_set_expand_gray_1_2_4_to_8(png);
  if (png_get_valid(png, info, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha(png);

  bool has_alpha = (color_type & PNG_COLOR_MASK_ALPHA) ||
                   png_get_valid(png, info, PNG_INFO_tRNS);

  if (!(color_type & PNG_COLOR_MASK_COLOR))
    png_set_gray_to_rgb(png);

  png_read_update_info(png, info);

  size_t row_bytes = png_get_rowbytes(png, info);
  int channels = png_get_channels(png, info);
  DataVector<uint8_t> raw(row_bytes * h);
  std::vector<png_bytep> rows(h);
  for (png_uint_32 y = 0; y < h; y++)
    rows[y] = raw.data() + y * row_bytes;

  png_read_image(png, rows.data());
  png_read_end(png, nullptr);
  png_destroy_read_struct(&png, &info, nullptr);

  size_t rgb_pitch = static_cast<size_t>(w) * 3;
  DecodedImage result;
  result.width = w;
  result.height = h;
  result.rgb.resize(rgb_pitch * h);

  if (has_alpha && channels == 4) {
    result.alpha.resize(static_cast<size_t>(w) * h);
    for (png_uint_32 y = 0; y < h; y++) {
      for (png_uint_32 x = 0; x < w; x++) {
        size_t src_idx = y * row_bytes + x * 4;
        size_t rgb_idx = y * rgb_pitch + x * 3;
        result.rgb[rgb_idx + 0] = raw[src_idx + 0];
        result.rgb[rgb_idx + 1] = raw[src_idx + 1];
        result.rgb[rgb_idx + 2] = raw[src_idx + 2];
        result.alpha[y * w + x] = raw[src_idx + 3];
      }
    }
  } else {
    for (png_uint_32 y = 0; y < h; y++) {
      memcpy(result.rgb.data() + y * rgb_pitch,
             raw.data() + y * row_bytes, rgb_pitch);
    }
  }

  return result;
}

}  // namespace fxcodec
