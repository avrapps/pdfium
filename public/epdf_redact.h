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
// Apply a redact annotation, permanently removing content underneath.
//
// Overlay precedence follows ISO 32000-2: if the annotation has an /RO
// (Redact Overlay) stream it is flattened as page content; otherwise an
// overlay is synthesized from the declarative entries (/IC fill and
// /OverlayText per /DA, /Q and /Repeat) and flattened the same way. An
// annotation with neither /RO nor declarative entries leaves the region
// transparent.
//
// Annotations whose /Rect intersects the redacted region with positive area
// are removed as well, including their popup cascade; removed widgets are
// detached from the AcroForm tree. Sibling REDACT annotations are preserved.
// The applied annotation itself is removed from the page.
//
// The caller is responsible for:
//   1. Closing the annotation handle with FPDFPage_CloseAnnot after this call
//   2. Calling FPDFPage_GenerateContent to persist changes
//
//   page                    - handle to the page containing the annotation
//   annot                   - handle to a REDACT annotation
//   out_removed_annot_count - optional, may be NULL. Receives the number of
//                             annotations removed as a side effect of this
//                             redaction, NOT counting REDACT annotations
//                             themselves. Zeroed on entry.
//
// Returns TRUE on success, FALSE if not a REDACT annotation or on error.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFAnnot_ApplyRedaction(FPDF_PAGE page,
                         FPDF_ANNOTATION annot,
                         uint32_t* out_removed_annot_count);

// Experimental EmbedPDF Extension API.
// Apply all redact annotations on a page, permanently removing content
// underneath each one. Overlay and removal semantics match
// EPDFAnnot_ApplyRedaction, as does the count contract: REDACT annotations
// (all of which are consumed by the apply) are never counted.
//
// The caller is responsible for:
//   1. Calling FPDFPage_GenerateContent to persist changes
//
//   page                    - handle to a page
//   out_removed_annot_count - optional, may be NULL. Receives the number of
//                             non-REDACT annotations removed. Zeroed on
//                             entry.
//
// Returns TRUE if any redactions were applied, FALSE otherwise.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_ApplyRedactions(FPDF_PAGE page, uint32_t* out_removed_annot_count);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_EPDF_REDACT_H_
