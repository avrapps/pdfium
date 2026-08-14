// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "core/fpdfapi/edit/cpdf_text_redactor.h"

#include <array>
#include <limits>

#include "testing/gtest/include/gtest/gtest.h"

TEST(CPDFTextRedactorTest, RejectsNonFiniteQuad) {
  const std::array<CFX_PointF, 4> corners = {
      CFX_PointF(0.0f, 10.0f), CFX_PointF(10.0f, 10.0f),
      CFX_PointF(0.0f, 0.0f),
      CFX_PointF(std::numeric_limits<float>::infinity(), 0.0f)};

  EXPECT_FALSE(RedactRegionFromQuadCorners(corners).has_value());
}

TEST(CPDFTextRedactorTest, FiniteMalformedQuadUsesAllCornerBounds) {
  // Ring order instead of PDF text-markup zigzag order. It is not safe to
  // interpret as an oriented cell, but its complete AABB is conservative.
  const std::array<CFX_PointF, 4> corners = {
      CFX_PointF(2.0f, 8.0f), CFX_PointF(12.0f, 10.0f),
      CFX_PointF(9.0f, -3.0f), CFX_PointF(-4.0f, 1.0f)};

  const std::optional<RedactRegion> region =
      RedactRegionFromQuadCorners(corners);
  ASSERT_TRUE(region.has_value());
  EXPECT_FALSE(region->has_quad);
  EXPECT_FLOAT_EQ(-4.0f, region->bbox.left);
  EXPECT_FLOAT_EQ(-3.0f, region->bbox.bottom);
  EXPECT_FLOAT_EQ(12.0f, region->bbox.right);
  EXPECT_FLOAT_EQ(10.0f, region->bbox.top);
}

TEST(CPDFTextRedactorTest, PreservesWellFormedOrientedQuad) {
  const std::array<CFX_PointF, 4> corners = {
      CFX_PointF(0.0f, 2.0f), CFX_PointF(2.0f, 4.0f),
      CFX_PointF(1.0f, 1.0f), CFX_PointF(3.0f, 3.0f)};

  const std::optional<RedactRegion> region =
      RedactRegionFromQuadCorners(corners);
  ASSERT_TRUE(region.has_value());
  EXPECT_TRUE(region->has_quad);
  EXPECT_EQ(corners, region->quad);
}
