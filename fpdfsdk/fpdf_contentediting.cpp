// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "public/fpdf_contentediting.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/page/cpdf_pageobject.h"
#include "core/fpdfapi/render/cpdf_pagerendercontext.h"
#include "core/fpdfapi/render/cpdf_progressiverenderer.h"
#include "core/fpdfapi/render/cpdf_rendercontext.h"
#include "core/fpdfapi/render/cpdf_renderobjectfilter.h"
#include "core/fpdfapi/render/cpdf_renderoptions.h"
#include "core/fpdfapi/render/cpdf_renderstatus.h"
#include "core/fpdftext/cpdf_textblock_detector.h"
#include "core/fpdftext/cpdf_textpage.h"
#include "core/fxge/cfx_defaultrenderdevice.h"
#include "core/fxge/dib/cfx_dibitmap.h"
#include "fpdfsdk/cpdfsdk_helpers.h"
#include "fpdfsdk/cpdfsdk_renderpage.h"

namespace {

// FIX #7: Global storage for text block snapshots, keyed by page pointer.
// WARNING: This is a Phase-1 simplification. Known issues:
// - Page pointer reuse across documents (rare but possible)
// - Lifetime: snapshots are not auto-erased on page destroy
// - Thread safety (WASM is usually single-thread but still risky)
// TODO: Store as extension data on CPDF_Page, keyed by (document, page_index)
std::map<CPDF_Page*, std::unique_ptr<pdfium::textblock::TextBlockSnapshot>>
    g_page_snapshots;

pdfium::textblock::TextBlockSnapshot* GetSnapshot(FPDF_PAGE page) {
  CPDF_Page* pPage = CPDFPageFromFPDFPage(page);
  if (!pPage) {
    return nullptr;
  }
  auto it = g_page_snapshots.find(pPage);
  if (it == g_page_snapshots.end()) {
    return nullptr;
  }
  return it->second.get();
}

void SetSnapshot(
    FPDF_PAGE page,
    std::unique_ptr<pdfium::textblock::TextBlockSnapshot> snapshot) {
  CPDF_Page* pPage = CPDFPageFromFPDFPage(page);
  if (!pPage) {
    return;
  }
  g_page_snapshots[pPage] = std::move(snapshot);
}

void ClearSnapshot(FPDF_PAGE page) {
  CPDF_Page* pPage = CPDFPageFromFPDFPage(page);
  if (!pPage) {
    return;
  }
  g_page_snapshots.erase(pPage);
}

}  // namespace

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_DetectTextBlocks(FPDF_PAGE page, int flags) {
  CPDF_Page* pPage = CPDFPageFromFPDFPage(page);
  if (!pPage) {
    return false;
  }

  // Check if we already have a snapshot with the same flags
  auto* existing = GetSnapshot(page);
  if (existing && existing->flags_used == flags) {
    return true;  // Already detected with same flags
  }

  // Parse page content if not already done
  pPage->ParseContent();

  // Create text page for character extraction
  auto text_page = std::make_unique<CPDF_TextPage>(pPage, false);

  // Run detection
  pdfium::textblock::CPDF_TextBlockDetector detector;
  auto snapshot = detector.Detect(pPage, text_page.get(), flags);

  if (!snapshot) {
    return false;
  }

  SetSnapshot(page, std::move(snapshot));
  return true;
}

FPDF_EXPORT void FPDF_CALLCONV
EPDFPage_InvalidateTextBlocks(FPDF_PAGE page) {
  ClearSnapshot(page);
}

FPDF_EXPORT int FPDF_CALLCONV EPDFPage_GetTextBlockCount(FPDF_PAGE page) {
  auto* snapshot = GetSnapshot(page);
  if (!snapshot) {
    return -1;
  }
  return static_cast<int>(snapshot->blocks.size());
}

FPDF_EXPORT int FPDF_CALLCONV EPDFPage_GetTextBlockType(FPDF_PAGE page,
                                                        int index) {
  auto* snapshot = GetSnapshot(page);
  if (!snapshot || index < 0 ||
      static_cast<size_t>(index) >= snapshot->blocks.size()) {
    return -1;
  }
  return static_cast<int>(snapshot->blocks[index].type);
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_GetTextBlockInkBounds(FPDF_PAGE page, int index, FS_RECTF* out_rect) {
  if (!out_rect) {
    return false;
  }

  auto* snapshot = GetSnapshot(page);
  if (!snapshot || index < 0 ||
      static_cast<size_t>(index) >= snapshot->blocks.size()) {
    return false;
  }

  const auto& bounds = snapshot->blocks[index].ink_bounds;
  out_rect->left = bounds.left;
  out_rect->bottom = bounds.bottom;
  out_rect->right = bounds.right;
  out_rect->top = bounds.top;
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_GetTextBlockLayoutBounds(FPDF_PAGE page,
                                  int index,
                                  FS_RECTF* out_rect) {
  if (!out_rect) {
    return false;
  }

  auto* snapshot = GetSnapshot(page);
  if (!snapshot || index < 0 ||
      static_cast<size_t>(index) >= snapshot->blocks.size()) {
    return false;
  }

  const auto& bounds = snapshot->blocks[index].layout_bounds;
  out_rect->left = bounds.left;
  out_rect->bottom = bounds.bottom;
  out_rect->right = bounds.right;
  out_rect->top = bounds.top;
  return true;
}

FPDF_EXPORT int FPDF_CALLCONV EPDFPage_GetTextBlockText(FPDF_PAGE page,
                                                        int index,
                                                        unsigned short* buffer,
                                                        int buflen) {
  auto* snapshot = GetSnapshot(page);
  if (!snapshot || index < 0 ||
      static_cast<size_t>(index) >= snapshot->blocks.size()) {
    return 0;
  }

  // FIX #10: Use cached text instead of rebuilding CPDF_TextPage
  const auto& block = snapshot->blocks[index];
  const WideString& text = block.cached_text;

  // Add terminating null
  int required_size = static_cast<int>(text.GetLength()) + 1;

  if (!buffer) {
    return required_size;
  }

  if (buflen < required_size) {
    return required_size;
  }

  // Copy to buffer
  for (size_t i = 0; i < text.GetLength(); ++i) {
    buffer[i] = static_cast<unsigned short>(text[i]);
  }
  buffer[text.GetLength()] = 0;

  return required_size;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_GetTextBlockBitmapSize(FPDF_PAGE page,
                                int index,
                                float scale,
                                int* out_width,
                                int* out_height) {
  if (!out_width || !out_height || scale <= 0.0f) {
    return false;
  }

  auto* snapshot = GetSnapshot(page);
  if (!snapshot || index < 0 ||
      static_cast<size_t>(index) >= snapshot->blocks.size()) {
    return false;
  }

  const auto& bounds = snapshot->blocks[index].ink_bounds;
  float width = bounds.right - bounds.left;
  float height = bounds.top - bounds.bottom;

  *out_width = static_cast<int>(std::ceil(width * scale));
  *out_height = static_cast<int>(std::ceil(height * scale));

  // Ensure minimum size
  if (*out_width < 1) {
    *out_width = 1;
  }
  if (*out_height < 1) {
    *out_height = 1;
  }

  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_RenderBackgroundExcludingTextBlocks(FPDF_BITMAP bitmap,
                                             FPDF_PAGE page,
                                             int start_x,
                                             int start_y,
                                             int size_x,
                                             int size_y,
                                             int rotate,
                                             int flags) {
  auto* snapshot = GetSnapshot(page);
  if (!snapshot) {
    return false;
  }

  CPDF_Page* pPage = CPDFPageFromFPDFPage(page);
  if (!pPage) {
    return false;
  }

  RetainPtr<CFX_DIBitmap> pBitmap(CFXDIBitmapFromFPDFBitmap(bitmap));
  if (!pBitmap) {
    return false;
  }

  pPage->ParseContent();

  // Set up render device, respecting FPDF_REVERSE_BYTE_ORDER flag for RGBA output
  auto pDevice = std::make_unique<CFX_DefaultRenderDevice>();
  pDevice->AttachWithRgbByteOrder(pBitmap, !!(flags & FPDF_REVERSE_BYTE_ORDER));

  const FX_RECT rect(start_x, start_y, start_x + size_x, start_y + size_y);
  CFX_Matrix matrix = pPage->GetDisplayMatrixForRect(rect, rotate);

  // Create exclusion filter using the snapshot's all_block_object_ids set
  pdfium::render::ExcludeIdsFilter filter(snapshot->all_block_object_ids);

  // Set up render context with layers
  auto pContext = std::make_unique<CPDF_RenderContext>(
      pPage->GetDocument(), pPage->GetMutablePageResources(),
      pPage->GetPageImageCache());
  pContext->AppendLayer(pPage, matrix);

  // Render with filter and object table
  pContext->Render(pDevice.get(), nullptr, nullptr, nullptr, &filter,
                   &snapshot->object_table);

  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_RenderTextBlockBitmap(FPDF_BITMAP bitmap,
                               FPDF_PAGE page,
                               int block_index,
                               int rotate,
                               int flags) {
  auto* snapshot = GetSnapshot(page);
  if (!snapshot || block_index < 0 ||
      static_cast<size_t>(block_index) >= snapshot->blocks.size()) {
    return false;
  }

  CPDF_Page* pPage = CPDFPageFromFPDFPage(page);
  if (!pPage) {
    return false;
  }

  RetainPtr<CFX_DIBitmap> pBitmap(CFXDIBitmapFromFPDFBitmap(bitmap));
  if (!pBitmap) {
    return false;
  }

  pPage->ParseContent();

  const auto& block = snapshot->blocks[block_index];
  const auto& block_ids = snapshot->block_to_object_ids[block_index];

  // Clear bitmap to transparent
  pBitmap->Clear(0);

  // Get bitmap dimensions
  int bitmap_width = pBitmap->GetWidth();
  int bitmap_height = pBitmap->GetHeight();

  // Calculate scale from block bounds to bitmap size
  float block_width = block.ink_bounds.right - block.ink_bounds.left;
  float block_height = block.ink_bounds.top - block.ink_bounds.bottom;

  if (block_width <= 0 || block_height <= 0) {
    return true;  // Empty block, nothing to render
  }

  float scale_x = static_cast<float>(bitmap_width) / block_width;
  float scale_y = static_cast<float>(bitmap_height) / block_height;
  float scale = std::min(scale_x, scale_y);

  // Build transform: translate block's bottom-left to origin, then scale
  // Page space is bottom-left origin, bitmap is top-left origin
  CFX_Matrix transform;
  transform.Translate(-block.ink_bounds.left, -block.ink_bounds.bottom);
  transform.Scale(scale, -scale);  // Negative Y to flip
  transform.Translate(0.0f, static_cast<float>(bitmap_height));

  // Handle rotation
  if (rotate != 0) {
    float center_x = static_cast<float>(bitmap_width) / 2.0f;
    float center_y = static_cast<float>(bitmap_height) / 2.0f;
    transform.Translate(-center_x, -center_y);
    float angle = static_cast<float>(rotate) * 90.0f * 3.14159265f / 180.0f;
    transform.Rotate(angle);
    transform.Translate(center_x, center_y);
  }

  // Set up render device, respecting FPDF_REVERSE_BYTE_ORDER flag for RGBA output
  auto pDevice = std::make_unique<CFX_DefaultRenderDevice>();
  pDevice->AttachWithRgbByteOrder(pBitmap, !!(flags & FPDF_REVERSE_BYTE_ORDER));

  const FX_RECT rect(0, 0, bitmap_width, bitmap_height);
  pDevice->SetClip_Rect(rect);

  // Set up render context and status for O(K) subset rendering
  auto pContext = std::make_unique<CPDF_RenderContext>(
      pPage->GetDocument(), pPage->GetMutablePageResources(),
      pPage->GetPageImageCache());
  pContext->AppendLayer(pPage, transform);

  // Create render status for subset rendering
  CPDF_RenderStatus status(pContext.get(), pDevice.get());
  status.SetTransparency(pPage->GetTransparency());
  status.Initialize(nullptr, nullptr);

  // Use O(K) subset rendering - only render the block's objects
  status.RenderObjectSubsetById(snapshot->object_table, block_ids, transform,
                                &rect);

  return true;
}
