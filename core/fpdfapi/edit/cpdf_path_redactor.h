// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_FPDFAPI_EDIT_CPDF_PATH_REDACTOR_H_
#define CORE_FPDFAPI_EDIT_CPDF_PATH_REDACTOR_H_

#include <memory>

#include "core/fxcrt/fx_coordinates.h"
#include "core/fxcrt/span.h"

class CPDF_PathObject;
class CPDF_ShadingObject;
struct RedactRegion;

// Result of sanitizing one path object. The original object is mutated only
// after all geometry operations have succeeded.
struct PathRedactionResult {
  bool succeeded = true;
  bool changed = false;
  bool remove_original = false;

  // A fill+stroke PDF path has two independently painted components. Once
  // clipped, the stroke is represented by a filled outline immediately after
  // the surviving fill so colors, alpha, and PDF paint order remain intact.
  std::unique_ptr<CPDF_PathObject> trailing_object;
};

// Curve-preserving PDF path sanitizer backed by Skia PathOps. Redaction
// regions are supplied in page user space and unioned once for reuse across
// every path on the page and in nested forms.
class CPDF_PathRedactor {
 public:
  explicit CPDF_PathRedactor(pdfium::span<const RedactRegion> regions);
  ~CPDF_PathRedactor();

  CPDF_PathRedactor(const CPDF_PathRedactor&) = delete;
  CPDF_PathRedactor& operator=(const CPDF_PathRedactor&) = delete;

  bool IsValid() const;

  // `parent_to_page` maps the containing page/form's local coordinates into
  // page user space. The path object's own matrix is applied internally.
  PathRedactionResult Redact(CPDF_PathObject* path,
                             const CFX_Matrix& parent_to_page) const;

  // Shadings have no finite source path to split. Preserve the paint server
  // and append a permanent vector clip containing only the area outside the
  // redaction region.
  PathRedactionResult RedactShading(CPDF_ShadingObject* shading,
                                    const CFX_Matrix& parent_to_page) const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

#endif  // CORE_FPDFAPI_EDIT_CPDF_PATH_REDACTOR_H_
