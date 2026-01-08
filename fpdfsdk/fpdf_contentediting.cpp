// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "public/fpdf_contentediting.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_EnableLayoutDebug(FPDF_PAGE page, FPDF_BOOL enable) {
  // Debug mode is set via detection flags (EPDF_TEXTBLOCK_ENABLE_DEBUG)
  // This function is kept for API compatibility
  (void)page;
  (void)enable;
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_RenderLayoutDebugOverlay(FPDF_BITMAP bitmap,
                                  FPDF_PAGE page,
                                  int start_x,
                                  int start_y,
                                  int size_x,
                                  int size_y,
                                  int rotate,
                                  int debug_flags) {
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

  const FX_RECT rect(start_x, start_y, start_x + size_x, start_y + size_y);
  CFX_Matrix matrix = pPage->GetDisplayMatrixForRect(rect, rotate);

  // Helper to transform page coords to bitmap coords
  auto page_to_bitmap = [&](float x, float y) -> CFX_PointF {
    return matrix.Transform(CFX_PointF(x, y));
  };

  // Helper to draw a rectangle
  auto draw_rect = [&](const CFX_FloatRect& r, uint32_t color) {
    CFX_PointF tl = page_to_bitmap(r.left, r.top);
    CFX_PointF br = page_to_bitmap(r.right, r.bottom);

    int x1 = static_cast<int>(std::min(tl.x, br.x));
    int y1 = static_cast<int>(std::min(tl.y, br.y));
    int x2 = static_cast<int>(std::max(tl.x, br.x));
    int y2 = static_cast<int>(std::max(tl.y, br.y));

    // Draw outline
    for (int x = x1; x <= x2; ++x) {
      if (y1 >= 0 && y1 < pBitmap->GetHeight() && x >= 0 && x < pBitmap->GetWidth()) {
        uint32_t* pixel = reinterpret_cast<uint32_t*>(
            pBitmap->GetWritableScanline(y1).data() + x * 4);
        *pixel = color;
      }
      if (y2 >= 0 && y2 < pBitmap->GetHeight() && x >= 0 && x < pBitmap->GetWidth()) {
        uint32_t* pixel = reinterpret_cast<uint32_t*>(
            pBitmap->GetWritableScanline(y2).data() + x * 4);
        *pixel = color;
      }
    }
    for (int y = y1; y <= y2; ++y) {
      if (y >= 0 && y < pBitmap->GetHeight()) {
        if (x1 >= 0 && x1 < pBitmap->GetWidth()) {
          uint32_t* pixel = reinterpret_cast<uint32_t*>(
              pBitmap->GetWritableScanline(y).data() + x1 * 4);
          *pixel = color;
        }
        if (x2 >= 0 && x2 < pBitmap->GetWidth()) {
          uint32_t* pixel = reinterpret_cast<uint32_t*>(
              pBitmap->GetWritableScanline(y).data() + x2 * 4);
          *pixel = color;
        }
      }
    }
  };

  // Helper to draw a small number label at a position
  auto draw_number = [&](int x, int y, int num, uint32_t color) {
    // Simple 3x5 digit patterns (0-9)
    static const uint8_t digits[10][5] = {
      {0b111, 0b101, 0b101, 0b101, 0b111},  // 0
      {0b010, 0b110, 0b010, 0b010, 0b111},  // 1
      {0b111, 0b001, 0b111, 0b100, 0b111},  // 2
      {0b111, 0b001, 0b111, 0b001, 0b111},  // 3
      {0b101, 0b101, 0b111, 0b001, 0b001},  // 4
      {0b111, 0b100, 0b111, 0b001, 0b111},  // 5
      {0b111, 0b100, 0b111, 0b101, 0b111},  // 6
      {0b111, 0b001, 0b001, 0b001, 0b001},  // 7
      {0b111, 0b101, 0b111, 0b101, 0b111},  // 8
      {0b111, 0b101, 0b111, 0b001, 0b111},  // 9
    };
    
    // Convert number to digits
    char buf[8];
    int len = snprintf(buf, sizeof(buf), "%d", num);
    
    int offset_x = 0;
    for (int d = 0; d < len; ++d) {
      int digit = buf[d] - '0';
      if (digit < 0 || digit > 9) continue;
      
      for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
          if (digits[digit][row] & (1 << (2 - col))) {
            int px = x + offset_x + col;
            int py = y + row;
            if (px >= 0 && px < pBitmap->GetWidth() &&
                py >= 0 && py < pBitmap->GetHeight()) {
              uint32_t* pixel = reinterpret_cast<uint32_t*>(
                  pBitmap->GetWritableScanline(py).data() + px * 4);
              *pixel = color;
            }
          }
        }
      }
      offset_x += 4;  // 3px width + 1px spacing
    }
  };

  // Draw words (cyan) with index labels
  if (debug_flags & EPDF_DEBUG_SHOW_WORDS) {
    for (size_t i = 0; i < snapshot->words.size(); ++i) {
      const auto& word = snapshot->words[i];
      if (!word.is_empty()) {
        draw_rect(word.bbox, 0xFF00FFFF);  // Cyan
        
        // Draw word index at top-left
        CFX_PointF pt = page_to_bitmap(word.bbox.left, word.bbox.top);
        draw_number(static_cast<int>(pt.x) + 1, static_cast<int>(pt.y) + 1,
                    static_cast<int>(i), 0xFF00FFFF);  // Cyan number
      }
    }
  }

  // Draw lines (magenta) with index labels
  if (debug_flags & EPDF_DEBUG_SHOW_LINES) {
    for (size_t i = 0; i < snapshot->lines.size(); ++i) {
      const auto& line = snapshot->lines[i];
      if (!line.is_empty()) {
        draw_rect(line.bbox, 0xFFFF00FF);  // Magenta
        
        // Draw line index at top-left of line bbox
        CFX_PointF pt = page_to_bitmap(line.bbox.left, line.bbox.top);
        draw_number(static_cast<int>(pt.x) + 2, static_cast<int>(pt.y) + 2, 
                    static_cast<int>(i), 0xFFFF00FF);  // Magenta number
      }
    }
  }

  // Draw columns (yellow) with index labels
  if (debug_flags & EPDF_DEBUG_SHOW_COLUMNS) {
    for (size_t i = 0; i < snapshot->columns.column_bounds.size(); ++i) {
      const auto& col = snapshot->columns.column_bounds[i];
      draw_rect(col, 0xFFFFFF00);  // Yellow
      
      // Draw column index at top-left
      CFX_PointF pt = page_to_bitmap(col.left, col.top);
      draw_number(static_cast<int>(pt.x) + 3, static_cast<int>(pt.y) + 3,
                  static_cast<int>(i), 0xFFFFFF00);  // Yellow number
    }
  }

  // Draw gutters (red vertical lines)
  if (debug_flags & EPDF_DEBUG_SHOW_GUTTERS) {
    float page_left = snapshot->stats.page_bounds.left;
    for (float g : snapshot->columns.gutter_positions) {
      float gutter_x = page_left + g;
      CFX_FloatRect gutter_rect;
      gutter_rect.left = gutter_x - 1;
      gutter_rect.right = gutter_x + 1;
      gutter_rect.top = snapshot->stats.page_bounds.top;
      gutter_rect.bottom = snapshot->stats.page_bounds.bottom;
      draw_rect(gutter_rect, 0xFFFF0000);  // Red
    }
  }

  // Draw tables (green) with index labels
  if (debug_flags & EPDF_DEBUG_SHOW_TABLES) {
    for (size_t i = 0; i < snapshot->tables.size(); ++i) {
      const auto& table = snapshot->tables[i];
      draw_rect(table.bbox, 0xFF00FF00);  // Green
      
      // Draw table index at top-left
      CFX_PointF pt = page_to_bitmap(table.bbox.left, table.bbox.top);
      draw_number(static_cast<int>(pt.x) + 5, static_cast<int>(pt.y) + 5,
                  static_cast<int>(i), 0xFF00FF00);  // Green number
    }
  }

  // Draw blocks (blue) with index labels
  if (debug_flags & EPDF_DEBUG_SHOW_BLOCKS) {
    for (size_t i = 0; i < snapshot->blocks.size(); ++i) {
      const auto& block = snapshot->blocks[i];
      draw_rect(block.ink_bounds, 0xFF0000FF);  // Blue
      
      // Draw block index at top-left
      CFX_PointF pt = page_to_bitmap(block.ink_bounds.left, block.ink_bounds.top);
      draw_number(static_cast<int>(pt.x) + 4, static_cast<int>(pt.y) + 4,
                  static_cast<int>(i), 0xFF0000FF);  // Blue number
    }
  }

  // Draw reading order (numbers)
  if (debug_flags & EPDF_DEBUG_SHOW_READING_ORDER) {
    // Simple: draw a marker at each block's top-left with order number
    for (size_t i = 0; i < snapshot->blocks.size(); ++i) {
      const auto& block = snapshot->blocks[i];
      CFX_PointF pt = page_to_bitmap(block.ink_bounds.left, block.ink_bounds.top);
      int x = static_cast<int>(pt.x);
      int y = static_cast<int>(pt.y);

      // Draw a small marker
      for (int dx = 0; dx < 8; ++dx) {
        for (int dy = 0; dy < 8; ++dy) {
          int px = x + dx;
          int py = y + dy;
          if (px >= 0 && px < pBitmap->GetWidth() &&
              py >= 0 && py < pBitmap->GetHeight()) {
            uint32_t* pixel = reinterpret_cast<uint32_t*>(
                pBitmap->GetWritableScanline(py).data() + px * 4);
            *pixel = 0xFF000000 | (static_cast<uint32_t>(i * 20) & 0xFF) << 8;
          }
        }
      }
    }
  }

  return true;
}

FPDF_EXPORT int FPDF_CALLCONV EPDFPage_GetWordCount(FPDF_PAGE page) {
  auto* snapshot = GetSnapshot(page);
  if (!snapshot) {
    return -1;
  }
  return static_cast<int>(snapshot->words.size());
}

FPDF_EXPORT int FPDF_CALLCONV EPDFPage_GetLineCount(FPDF_PAGE page) {
  auto* snapshot = GetSnapshot(page);
  if (!snapshot) {
    return -1;
  }
  return static_cast<int>(snapshot->lines.size());
}

FPDF_EXPORT int FPDF_CALLCONV EPDFPage_GetColumnCount(FPDF_PAGE page) {
  auto* snapshot = GetSnapshot(page);
  if (!snapshot) {
    return -1;
  }
  return static_cast<int>(snapshot->columns.column_bounds.size());
}

FPDF_EXPORT int FPDF_CALLCONV EPDFPage_GetTableCount(FPDF_PAGE page) {
  auto* snapshot = GetSnapshot(page);
  if (!snapshot) {
    return -1;
  }
  return static_cast<int>(snapshot->tables.size());
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_GetWordBounds(FPDF_PAGE page, int word_index, FS_RECTF* out_rect) {
  if (!out_rect) {
    return false;
  }

  auto* snapshot = GetSnapshot(page);
  if (!snapshot || word_index < 0 ||
      static_cast<size_t>(word_index) >= snapshot->words.size()) {
    return false;
  }

  const auto& bounds = snapshot->words[word_index].bbox;
  out_rect->left = bounds.left;
  out_rect->bottom = bounds.bottom;
  out_rect->right = bounds.right;
  out_rect->top = bounds.top;
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_GetLineBounds(FPDF_PAGE page, int line_index, FS_RECTF* out_rect) {
  if (!out_rect) {
    return false;
  }

  auto* snapshot = GetSnapshot(page);
  if (!snapshot || line_index < 0 ||
      static_cast<size_t>(line_index) >= snapshot->lines.size()) {
    return false;
  }

  const auto& bounds = snapshot->lines[line_index].bbox;
  out_rect->left = bounds.left;
  out_rect->bottom = bounds.bottom;
  out_rect->right = bounds.right;
  out_rect->top = bounds.top;
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_GetColumnBounds(FPDF_PAGE page, int column_index, FS_RECTF* out_rect) {
  if (!out_rect) {
    return false;
  }

  auto* snapshot = GetSnapshot(page);
  if (!snapshot || column_index < 0 ||
      static_cast<size_t>(column_index) >= snapshot->columns.column_bounds.size()) {
    return false;
  }

  const auto& bounds = snapshot->columns.column_bounds[column_index];
  out_rect->left = bounds.left;
  out_rect->bottom = bounds.bottom;
  out_rect->right = bounds.right;
  out_rect->top = bounds.top;
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_GetTableBounds(FPDF_PAGE page, int table_index, FS_RECTF* out_rect) {
  if (!out_rect) {
    return false;
  }

  auto* snapshot = GetSnapshot(page);
  if (!snapshot || table_index < 0 ||
      static_cast<size_t>(table_index) >= snapshot->tables.size()) {
    return false;
  }

  const auto& bounds = snapshot->tables[table_index].bbox;
  out_rect->left = bounds.left;
  out_rect->bottom = bounds.bottom;
  out_rect->right = bounds.right;
  out_rect->top = bounds.top;
  return true;
}

FPDF_EXPORT int FPDF_CALLCONV
EPDFPage_GetTableCellCount(FPDF_PAGE page, int table_index) {
  auto* snapshot = GetSnapshot(page);
  if (!snapshot || table_index < 0 ||
      static_cast<size_t>(table_index) >= snapshot->tables.size()) {
    return -1;
  }
  return static_cast<int>(snapshot->tables[table_index].cells.size());
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_GetAdaptiveParams(FPDF_PAGE page,
                           float* out_median_height,
                           float* out_median_width,
                           float* out_baseline_tol) {
  auto* snapshot = GetSnapshot(page);
  if (!snapshot) {
    return false;
  }

  if (out_median_height) {
    *out_median_height = snapshot->params.median_height;
  }
  if (out_median_width) {
    *out_median_width = snapshot->params.median_width;
  }
  if (out_baseline_tol) {
    *out_baseline_tol = snapshot->params.baseline_tol;
  }
  return true;
}
