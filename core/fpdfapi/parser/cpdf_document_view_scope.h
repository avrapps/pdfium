// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFAPI_PARSER_CPDF_DOCUMENT_VIEW_SCOPE_H_
#define CORE_FPDFAPI_PARSER_CPDF_DOCUMENT_VIEW_SCOPE_H_

class CPDF_BaseDocument;
class CPDF_Document;
class CPDF_LayerDocument;

// Establishes the effective document view used while resolving references
// inherited from a frozen base document. Scopes are nestable so operations
// involving more than one document can switch views explicitly and restore the
// caller's view on return.
class CPDF_DocumentViewScope final {
 public:
  enum class Mode {
    kNone,
    kEffective,
    kFrozen,
  };

  explicit CPDF_DocumentViewScope(CPDF_Document* document);
  ~CPDF_DocumentViewScope();

  CPDF_DocumentViewScope(const CPDF_DocumentViewScope&) = delete;
  CPDF_DocumentViewScope& operator=(const CPDF_DocumentViewScope&) = delete;

  static const CPDF_LayerDocument* GetCurrentLayerForBase(
      const CPDF_BaseDocument* base);
  static Mode GetCurrentMode();
  static bool IsFrozenForBase(const CPDF_BaseDocument* base);

 private:
  const Mode previous_mode_;
  const CPDF_LayerDocument* const previous_layer_;
  const CPDF_BaseDocument* const previous_frozen_base_;
  const Mode mode_;
  const CPDF_LayerDocument* const layer_;
  const CPDF_BaseDocument* const frozen_base_;
};

#endif  // CORE_FPDFAPI_PARSER_CPDF_DOCUMENT_VIEW_SCOPE_H_
