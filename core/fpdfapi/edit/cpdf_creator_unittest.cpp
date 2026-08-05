// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "core/fpdfapi/edit/cpdf_creator.h"

#include "testing/gtest/include/gtest/gtest.h"

TEST(CPDFCreatorTest, FormatXrefOffset10Is64BitClean) {
  ByteString small = CPDF_Creator::FormatXrefOffset10ForTesting(42);
  EXPECT_EQ("0000000042", small);
  EXPECT_EQ(10u, small.GetLength());

  ByteString mid =
      CPDF_Creator::FormatXrefOffset10ForTesting(2500000000LL);
  EXPECT_EQ("2500000000", mid);
  EXPECT_EQ(10u, mid.GetLength());

  ByteString max =
      CPDF_Creator::FormatXrefOffset10ForTesting(0xffffffff);
  EXPECT_EQ("4294967295", max);
  EXPECT_EQ(10u, max.GetLength());
}
