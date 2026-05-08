// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/parser/cpdf_read_only_graph_guard.h"

namespace {

thread_local bool g_read_only_graph_guard_active = false;
thread_local int g_inline_rewrite_depth = 0;

}  // namespace

CPDF_ReadOnlyGraphGuard::CPDF_ReadOnlyGraphGuard()
    : previous_(g_read_only_graph_guard_active) {
  g_read_only_graph_guard_active = true;
}

CPDF_ReadOnlyGraphGuard::~CPDF_ReadOnlyGraphGuard() {
  g_read_only_graph_guard_active = previous_;
}

// static
bool CPDF_ReadOnlyGraphGuard::IsActive() {
  return g_read_only_graph_guard_active;
}

// static
bool CPDF_ReadOnlyGraphGuard::IsInlineRewriteActive() {
  return g_inline_rewrite_depth > 0;
}

CPDF_ScopedInlineRewrite::CPDF_ScopedInlineRewrite() {
  ++g_inline_rewrite_depth;
}

CPDF_ScopedInlineRewrite::~CPDF_ScopedInlineRewrite() {
  --g_inline_rewrite_depth;
}
