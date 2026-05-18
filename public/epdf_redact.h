// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PUBLIC_EPDF_REDACT_H_
#define PUBLIC_EPDF_REDACT_H_

#include <stdint.h>

// NOLINTNEXTLINE(build/include)
#include "fpdfview.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Experimental EmbedPDF Extension API.
// Report entry produced by redaction APIs that delete annotations.
//
// `object_number` is 0 when the removed annotation was a direct object.
// `nm_utf8_len` is 0 when no /NM was present. It is
// EPDF_REMOVED_ANNOT_NM_UTF8_OVERFLOW when /NM existed but the caller's
// UTF-8 byte pool had no room for it.
#define EPDF_REMOVED_ANNOT_NM_UTF8_OVERFLOW 0xFFFFFFFFu

typedef struct {
  uint32_t object_number;
  uint32_t index_at_removal;
  uint32_t nm_utf8_offset;
  uint32_t nm_utf8_len;
} EPDF_RemovedAnnotInfo;

// Experimental EmbedPDF Extension API.
// Apply a redact annotation, permanently removing content underneath.
// If the annotation has an RO (Redact Overlay) stream, it will be flattened
// as page content (filled rectangles with overlay text).
// If no RO stream exists, content is simply removed with no overlay.
// The annotation is automatically removed from the page after applying.
//
// The caller is responsible for:
//   1. Closing the annotation handle with FPDFPage_CloseAnnot after this call
//   2. Calling FPDFPage_GenerateContent to persist changes
//
//   page  - handle to the page containing the annotation
//   annot - handle to a REDACT annotation
//
// Returns TRUE on success, FALSE if not a REDACT annotation or on error.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFAnnot_ApplyRedaction(FPDF_PAGE page, FPDF_ANNOTATION annot);

// Experimental EmbedPDF Extension API.
// Same as EPDFAnnot_ApplyRedaction(), but also reports every annotation that
// was removed. This includes annotations whose /Rect intersects the redaction
// area, popup annotations cascaded from removed parents, and the originating
// REDACT annotation itself. Sibling REDACT annotations are preserved.
//
// The caller owns both output buffers. `out_written_count` is the number of
// records safely written to `out_removed`; `out_total_count` is the total
// number of annotations removed. If total > written, the report was truncated.
// /NM values are normalized to UTF-8 and written into `nm_utf8_pool`.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFAnnot_ApplyRedactionWithReport(
    FPDF_PAGE page,
    FPDF_ANNOTATION annot,
    EPDF_RemovedAnnotInfo* out_removed,
    uint32_t out_removed_capacity,
    char* nm_utf8_pool,
    uint32_t nm_utf8_pool_capacity,
    uint32_t* out_written_count,
    uint32_t* out_total_count,
    uint32_t* out_nm_utf8_bytes_used);

// Experimental EmbedPDF Extension API.
// Apply all redact annotations on a page, permanently removing content
// underneath each one. For each annotation with an RO stream, the overlay
// is flattened as page content. Annotations without RO simply have content
// removed with no overlay.
// All REDACT annotations are automatically removed from the page after applying.
//
// The caller is responsible for:
//   1. Calling FPDFPage_GenerateContent to persist changes
//
//   page - handle to a page
//
// Returns TRUE if any redactions were applied, FALSE otherwise.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_ApplyRedactions(FPDF_PAGE page);

// Experimental EmbedPDF Extension API.
// Same as EPDFPage_ApplyRedactions(), but reports removed annotations using
// the same buffer contract as EPDFAnnot_ApplyRedactionWithReport().
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_ApplyRedactionsWithReport(
    FPDF_PAGE page,
    EPDF_RemovedAnnotInfo* out_removed,
    uint32_t out_removed_capacity,
    char* nm_utf8_pool,
    uint32_t nm_utf8_pool_capacity,
    uint32_t* out_written_count,
    uint32_t* out_total_count,
    uint32_t* out_nm_utf8_bytes_used);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_EPDF_REDACT_H_
