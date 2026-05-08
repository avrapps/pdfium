// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/parser/cpdf_read_only_graph_guard.h"

#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "testing/gtest/include/gtest/gtest.h"

TEST(CPDFReadOnlyGraphGuardTest, ActiveStateStacks) {
  EXPECT_FALSE(CPDF_ReadOnlyGraphGuard::IsActive());
  {
    CPDF_ReadOnlyGraphGuard guard;
    EXPECT_TRUE(CPDF_ReadOnlyGraphGuard::IsActive());
    {
      CPDF_ReadOnlyGraphGuard nested_guard;
      EXPECT_TRUE(CPDF_ReadOnlyGraphGuard::IsActive());
    }
    EXPECT_TRUE(CPDF_ReadOnlyGraphGuard::IsActive());
  }
  EXPECT_FALSE(CPDF_ReadOnlyGraphGuard::IsActive());
}

TEST(CPDFReadOnlyGraphGuardTest, AllowsInlineObjects) {
  auto dict = pdfium::MakeRetain<CPDF_Dictionary>();
  ASSERT_EQ(0u, dict->GetObjNum());

  CPDF_ReadOnlyGraphGuard guard;
  DCHECK_PDF_GRAPH_MUTABLE_FOR(dict.Get());
}

TEST(CPDFReadOnlyGraphGuardTest, InlineRewriteStateStacks) {
  EXPECT_FALSE(CPDF_ReadOnlyGraphGuard::IsInlineRewriteActive());
  {
    CPDF_ScopedInlineRewrite rewrite;
    EXPECT_TRUE(CPDF_ReadOnlyGraphGuard::IsInlineRewriteActive());
    {
      CPDF_ScopedInlineRewrite nested_rewrite;
      EXPECT_TRUE(CPDF_ReadOnlyGraphGuard::IsInlineRewriteActive());
    }
    EXPECT_TRUE(CPDF_ReadOnlyGraphGuard::IsInlineRewriteActive());
  }
  EXPECT_FALSE(CPDF_ReadOnlyGraphGuard::IsInlineRewriteActive());
}
