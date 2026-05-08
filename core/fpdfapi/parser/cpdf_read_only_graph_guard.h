// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFAPI_PARSER_CPDF_READ_ONLY_GRAPH_GUARD_H_
#define CORE_FPDFAPI_PARSER_CPDF_READ_ONLY_GRAPH_GUARD_H_

#include "core/fxcrt/check.h"

class CPDF_ReadOnlyGraphGuard {
 public:
  CPDF_ReadOnlyGraphGuard();
  ~CPDF_ReadOnlyGraphGuard();

  static bool IsActive();

 private:
  const bool previous_;
};

#if DCHECK_IS_ON()
#define DCHECK_PDF_GRAPH_MUTABLE_FOR(obj) \
  DCHECK(!CPDF_ReadOnlyGraphGuard::IsActive() || (obj)->GetObjNum() == 0)
#define DCHECK_PDF_HOLDER_MUTABLE() DCHECK(!CPDF_ReadOnlyGraphGuard::IsActive())
#else
#define DCHECK_PDF_GRAPH_MUTABLE_FOR(obj) ((void)0)
#define DCHECK_PDF_HOLDER_MUTABLE() ((void)0)
#endif

#endif  // CORE_FPDFAPI_PARSER_CPDF_READ_ONLY_GRAPH_GUARD_H_
