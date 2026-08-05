// Copyright 2014 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef PUBLIC_FPDF_FLATTEN_H_
#define PUBLIC_FPDF_FLATTEN_H_

// NOLINTNEXTLINE(build/include)
#include "fpdfview.h"

// Flatten operation failed.
#define FLATTEN_FAIL 0
// Flatten operation succeed.
#define FLATTEN_SUCCESS 1
// Nothing to be flattened.
#define FLATTEN_NOTHINGTODO 2

// Flatten for normal display.
#define FLAT_NORMALDISPLAY 0
// Flatten for print.
#define FLAT_PRINT 1

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Flatten annotations and form fields into the page contents.
//
//   page  - handle to the page.
//   nFlag - One of the |FLAT_*| values denoting the page usage.
//
// Returns one of the |FLATTEN_*| values.
//
// Currently, all failures return |FLATTEN_FAIL| with no indication of the
// cause.
FPDF_EXPORT int FPDF_CALLCONV FPDFPage_Flatten(FPDF_PAGE page, int nFlag);

// Experimental EmbedPDF Extension API.
// Flatten every eligible annotation appearance on a page. This is the
// layer-safe counterpart to FPDFPage_Flatten(): the page dictionary is
// promoted before any mutation.
//
// Only annotations whose normal appearance was successfully added to page
// content are removed. Hidden, usage-ineligible, popup, malformed, and
// appearance-less annotations remain in /Annots. Flattened widgets are also
// detached from the AcroForm field tree; a separate logical field remains as
// an unplaced field.
//
//   page  - handle to the page.
//   usage - exactly one of the |FLAT_*| values.
//
// Returns FLATTEN_SUCCESS if at least one annotation was flattened,
// FLATTEN_NOTHINGTODO if the page has no eligible usable appearances, or
// FLATTEN_FAIL for invalid arguments.
FPDF_EXPORT int FPDF_CALLCONV EPDFPage_Flatten(FPDF_PAGE page, int usage);

// Experimental EmbedPDF Extension API.
// Flatten one annotation from a page. The annotation may have been loaded by
// index, /NM, object number, or any other API returning an FPDF_ANNOTATION
// handle. Eligibility, appearance resolution, widget cleanup, and layer
// promotion are identical to EPDFPage_Flatten().
//
// The caller must close |annot| with FPDFPage_CloseAnnot() after this call.
// A successful call removes the annotation from the page, so the handle must
// not be used again before it is closed.
//
//   page  - handle to the page containing |annot|.
//   annot - handle to the annotation to flatten.
//   usage - exactly one of the |FLAT_*| values.
//
// Returns FLATTEN_SUCCESS if the annotation was flattened,
// FLATTEN_NOTHINGTODO if it belongs to the page but is ineligible or has no
// usable normal appearance, or FLATTEN_FAIL for invalid arguments or when
// |annot| does not belong to |page|.
FPDF_EXPORT int FPDF_CALLCONV EPDFAnnot_Flatten(FPDF_PAGE page,
                                                FPDF_ANNOTATION annot,
                                                int usage);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_FPDF_FLATTEN_H_
