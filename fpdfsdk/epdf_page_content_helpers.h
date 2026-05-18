// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FPDFSDK_EPDF_PAGE_CONTENT_HELPERS_H_
#define FPDFSDK_EPDF_PAGE_CONTENT_HELPERS_H_

#include "core/fxcrt/fx_coordinates.h"
#include "core/fxcrt/retain_ptr.h"

class CPDF_Page;
class CPDF_Stream;

void EpdfAppendFormXObjectToPage(CPDF_Page* page,
                                 RetainPtr<const CPDF_Stream> form_stream,
                                 const CFX_FloatRect& target_rect);

#endif  // FPDFSDK_EPDF_PAGE_CONTENT_HELPERS_H_
