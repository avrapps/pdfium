// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/parser/cpdf_document_view_scope.h"

#include "core/fpdfapi/parser/cpdf_layer_document.h"
#include "core/fxcrt/check_op.h"
#include "core/fxcrt/epdf_tls.h"

namespace {

EPDF_TLS CPDF_DocumentViewScope::Mode g_current_view_mode =
    CPDF_DocumentViewScope::Mode::kNone;
EPDF_TLS const CPDF_LayerDocument* g_current_layer_view = nullptr;
EPDF_TLS const CPDF_BaseDocument* g_current_frozen_base = nullptr;

CPDF_DocumentViewScope::Mode GetModeForDocument(CPDF_Document* document) {
  if (!document) {
    return g_current_view_mode;
  }
  return CPDF_LayerDocument::FromDocument(document)
             ? CPDF_DocumentViewScope::Mode::kEffective
             : CPDF_DocumentViewScope::Mode::kFrozen;
}

const CPDF_LayerDocument* GetLayerForDocument(CPDF_Document* document) {
  return document ? CPDF_LayerDocument::FromDocument(document)
                  : g_current_layer_view;
}

const CPDF_BaseDocument* GetFrozenBaseForDocument(CPDF_Document* document) {
  if (!document) {
    return g_current_frozen_base;
  }
  return !CPDF_LayerDocument::FromDocument(document)
             ? document->GetBaseDocumentForViewScope()
             : nullptr;
}

}  // namespace

CPDF_DocumentViewScope::CPDF_DocumentViewScope(CPDF_Document* document)
    : previous_mode_(g_current_view_mode),
      previous_layer_(g_current_layer_view),
      previous_frozen_base_(g_current_frozen_base),
      mode_(GetModeForDocument(document)),
      layer_(GetLayerForDocument(document)),
      frozen_base_(GetFrozenBaseForDocument(document)) {
  g_current_view_mode = mode_;
  g_current_layer_view = layer_;
  g_current_frozen_base = frozen_base_;
}

CPDF_DocumentViewScope::~CPDF_DocumentViewScope() {
  DCHECK_EQ(g_current_view_mode, mode_);
  DCHECK_EQ(g_current_layer_view, layer_);
  DCHECK_EQ(g_current_frozen_base, frozen_base_);
  g_current_view_mode = previous_mode_;
  g_current_layer_view = previous_layer_;
  g_current_frozen_base = previous_frozen_base_;
}

// static
const CPDF_LayerDocument* CPDF_DocumentViewScope::GetCurrentLayerForBase(
    const CPDF_BaseDocument* base) {
  return g_current_view_mode == Mode::kEffective && g_current_layer_view &&
                 g_current_layer_view->GetBaseDocument() == base
             ? g_current_layer_view
             : nullptr;
}

// static
CPDF_DocumentViewScope::Mode CPDF_DocumentViewScope::GetCurrentMode() {
  return g_current_view_mode;
}

// static
bool CPDF_DocumentViewScope::IsFrozenForBase(
    const CPDF_BaseDocument* base) {
  return g_current_view_mode == Mode::kFrozen &&
         g_current_frozen_base == base;
}
