// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef PUBLIC_EPDF_TEXT_H_
#define PUBLIC_EPDF_TEXT_H_

#include <stdint.h>

// NOLINTNEXTLINE(build/include)
#include "fpdf_text.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// EPDF_CHAR_GEOMETRY::flags values. Geometry validity is explicit: callers
// must not infer quad validity from EPDF_CHARGEO_UPRIGHT or from non-zero data.
#define EPDF_CHARGEO_HAS_TIGHT_BOX (1u << 0)
#define EPDF_CHARGEO_HAS_LOOSE_QUAD (1u << 1)
#define EPDF_CHARGEO_HAS_TIGHT_QUAD (1u << 2)
#define EPDF_CHARGEO_UPRIGHT (1u << 3)
#define EPDF_CHARGEO_SPACE (1u << 4)
#define EPDF_CHARGEO_EMPTY (1u << 5)
#define EPDF_CHARGEO_SYNTHESIZED (1u << 6)

// Experimental EmbedPDF Extension API.
// Complete geometry for one text-page character. All coordinates are in PDF
// user space. Quad slots have frame-geometric semantics in the glyph's local
// upright frame:
//   (x1,y1) upper-start, (x2,y2) upper-end,
//   (x3,y3) lower-start, (x4,y4) lower-end.
// "Start" is local minimum-x and is deliberately not a bidi/reading-order
// statement. Quads are populated only when their corresponding HAS_* flag is
// present. Upright valid quads are populated and coincide with box corners.
typedef struct EPDF_CHAR_GEOMETRY_ {
  FS_RECTF loose_box;
  FS_RECTF tight_box;
  FS_QUADPOINTSF loose_quad;
  FS_QUADPOINTSF tight_quad;
  FS_MATRIX matrix;
  uint32_t flags;
} EPDF_CHAR_GEOMETRY;

// Experimental EmbedPDF Extension API.
// Read the boxes, oriented cells, effective matrix, and selection flags for a
// character in a single call. Returns FALSE for a null output pointer, invalid
// text page, or out-of-range index; on failure |geometry| is unmodified.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFText_GetCharGeometry(FPDF_TEXTPAGE text_page,
                         int index,
                         EPDF_CHAR_GEOMETRY* geometry);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_EPDF_TEXT_H_
