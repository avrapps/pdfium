// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PUBLIC_FPDF_CONTENTEDITING_H_
#define PUBLIC_FPDF_CONTENTEDITING_H_

// clang-format off
// NOLINTNEXTLINE(build/include)
#include "fpdfview.h"

// Text block types
#define EPDF_TEXTBLOCK_TYPE_PARAGRAPH 0
#define EPDF_TEXTBLOCK_TYPE_TABLECELL 1  // Reserved for Phase 1C

// Detection flags
#define EPDF_TEXTBLOCK_DEFAULT           0x00
#define EPDF_TEXTBLOCK_DETECT_TABLES     0x01  // Reserved for Phase 1C
#define EPDF_TEXTBLOCK_STRICT_EXCLUSIONS 0x02  // More aggressive RTL/complex filtering

#ifdef __cplusplus
extern "C" {
#endif

// Experimental API.
// Function: EPDFPage_DetectTextBlocks
//          Run text block detection on a page and cache the results.
// Parameters:
//          page    -   Handle to a page. Returned by FPDF_LoadPage.
//          flags   -   Detection flags. A bit-wise OR of the
//                      EPDF_TEXTBLOCK_* values above.
// Return value:
//          TRUE on success, FALSE on failure.
// Comments:
//          Detection results are cached on the page. Call
//          EPDFPage_InvalidateTextBlocks() after modifying page content
//          to clear the cache.
//          If called with different flags than previously cached, detection
//          will re-run.
//
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_DetectTextBlocks(FPDF_PAGE page, int flags);

// Experimental API.
// Function: EPDFPage_InvalidateTextBlocks
//          Invalidate the cached text block detection results.
// Parameters:
//          page    -   Handle to a page.
// Return value:
//          None.
// Comments:
//          Call this after modifying page content to ensure the next
//          detection or query returns fresh results.
//
FPDF_EXPORT void FPDF_CALLCONV
EPDFPage_InvalidateTextBlocks(FPDF_PAGE page);

// Experimental API.
// Function: EPDFPage_GetTextBlockCount
//          Get the number of detected text blocks on a page.
// Parameters:
//          page    -   Handle to a page.
// Return value:
//          The number of text blocks, or -1 if detection has not been run
//          or an error occurred.
// Comments:
//          EPDFPage_DetectTextBlocks() must be called first.
//
FPDF_EXPORT int FPDF_CALLCONV
EPDFPage_GetTextBlockCount(FPDF_PAGE page);

// Experimental API.
// Function: EPDFPage_GetTextBlockType
//          Get the type of a text block.
// Parameters:
//          page    -   Handle to a page.
//          index   -   Zero-based index of the text block.
// Return value:
//          The text block type (EPDF_TEXTBLOCK_TYPE_*), or -1 if detection
//          has not been run or index is invalid.
//
FPDF_EXPORT int FPDF_CALLCONV
EPDFPage_GetTextBlockType(FPDF_PAGE page, int index);

// Experimental API.
// Function: EPDFPage_GetTextBlockInkBounds
//          Get the tight bounding box (ink bounds) of a text block.
// Parameters:
//          page    -   Handle to a page.
//          index   -   Zero-based index of the text block.
//          out_rect -  Pointer to a FS_RECTF to receive the bounds.
//                      In page coordinates (left, bottom, right, top).
// Return value:
//          TRUE on success, FALSE if detection has not been run or
//          index is invalid.
// Comments:
//          Ink bounds are the tight bounding box around actual glyph pixels.
//
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_GetTextBlockInkBounds(FPDF_PAGE page, int index, FS_RECTF* out_rect);

// Experimental API.
// Function: EPDFPage_GetTextBlockLayoutBounds
//          Get the layout bounds of a text block.
// Parameters:
//          page    -   Handle to a page.
//          index   -   Zero-based index of the text block.
//          out_rect -  Pointer to a FS_RECTF to receive the bounds.
//                      In page coordinates (left, bottom, right, top).
// Return value:
//          TRUE on success, FALSE if detection has not been run or
//          index is invalid.
// Comments:
//          Layout bounds are expanded by line metrics (ascent/descent).
//          In Phase 1, this may return the same as ink bounds.
//
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_GetTextBlockLayoutBounds(FPDF_PAGE page, int index, FS_RECTF* out_rect);

// Experimental API.
// Function: EPDFPage_GetTextBlockText
//          Get the text content of a text block.
// Parameters:
//          page    -   Handle to a page.
//          index   -   Zero-based index of the text block.
//          buffer  -   A buffer to receive the text in UTF-16LE encoding.
//                      May be NULL to query required size.
//          buflen  -   Number of unsigned shorts the buffer can hold.
// Return value:
//          Number of unsigned shorts written (including terminating NUL),
//          or required buffer size if buffer is NULL.
//          Returns 0 if detection has not been run or index is invalid.
//
FPDF_EXPORT int FPDF_CALLCONV
EPDFPage_GetTextBlockText(FPDF_PAGE page,
                          int index,
                          unsigned short* buffer,
                          int buflen);

// Experimental API.
// Function: EPDFPage_GetTextBlockBitmapSize
//          Get the recommended bitmap size for rendering a text block.
// Parameters:
//          page    -   Handle to a page.
//          index   -   Zero-based index of the text block.
//          scale   -   Scale factor (1.0 = 72 DPI, 2.0 = 144 DPI, etc.)
//          out_width  - Pointer to receive the recommended width in pixels.
//          out_height - Pointer to receive the recommended height in pixels.
// Return value:
//          TRUE on success, FALSE if detection has not been run or
//          index is invalid.
//
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_GetTextBlockBitmapSize(FPDF_PAGE page,
                                int index,
                                float scale,
                                int* out_width,
                                int* out_height);

// Experimental API.
// Function: EPDFPage_RenderBackgroundExcludingTextBlocks
//          Render a page excluding detected text blocks.
// Parameters:
//          bitmap  -   Handle to the device-independent bitmap (created by
//                      FPDFBitmap_Create) for rendering.
//          page    -   Handle to the page.
//          start_x -   Left pixel position of the display area in bitmap
//                      coordinates.
//          start_y -   Top pixel position of the display area in bitmap
//                      coordinates.
//          size_x  -   Horizontal size (in pixels) for displaying the page.
//          size_y  -   Vertical size (in pixels) for displaying the page.
//          rotate  -   Page orientation: 0 (normal), 1 (rotated 90 degrees
//                      clockwise), 2 (180 degrees), 3 (270 degrees clockwise).
//          flags   -   0 for normal display, or combination of FPDF_* render
//                      flags defined in fpdfview.h.
// Return value:
//          TRUE on success, FALSE if detection has not been run.
// Comments:
//          EPDFPage_DetectTextBlocks() must be called first.
//          This renders all page content EXCEPT the detected text blocks,
//          suitable for use as a background layer.
//
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_RenderBackgroundExcludingTextBlocks(FPDF_BITMAP bitmap,
                                             FPDF_PAGE page,
                                             int start_x,
                                             int start_y,
                                             int size_x,
                                             int size_y,
                                             int rotate,
                                             int flags);

// Experimental API.
// Function: EPDFPage_RenderTextBlockBitmap
//          Render a single text block to a bitmap with transparent background.
// Parameters:
//          bitmap  -   Handle to the device-independent bitmap (created by
//                      FPDFBitmap_CreateEx with BGRA format).
//          page    -   Handle to the page.
//          block_index - Zero-based index of the text block to render.
//          rotate  -   Page orientation: 0 (normal), 1 (rotated 90 degrees
//                      clockwise), 2 (180 degrees), 3 (270 degrees clockwise).
//          flags   -   0 for normal display, or combination of FPDF_* render
//                      flags defined in fpdfview.h.
// Return value:
//          TRUE on success, FALSE if detection has not been run or
//          block_index is invalid.
// Comments:
//          EPDFPage_DetectTextBlocks() must be called first.
//          The bitmap should be sized using EPDFPage_GetTextBlockBitmapSize().
//          The bitmap will be cleared to transparent and only the text block's
//          glyphs will be rendered.
//          Use FPDF_ALPHA flag if alpha channel is needed.
//
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_RenderTextBlockBitmap(FPDF_BITMAP bitmap,
                               FPDF_PAGE page,
                               int block_index,
                               int rotate,
                               int flags);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // PUBLIC_FPDF_CONTENTEDITING_H_
