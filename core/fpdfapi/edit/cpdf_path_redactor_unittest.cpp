// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "core/fpdfapi/edit/cpdf_path_redactor.h"
#include "core/fpdfapi/edit/cpdf_text_redactor.h"

#include <algorithm>
#include <array>

#include "core/fpdfapi/page/cpdf_pathobject.h"
#include "core/fpdfapi/page/cpdf_shadingobject.h"
#include "core/fpdfapi/page/cpdf_shadingpattern.h"
#include "core/fpdfapi/page/test_with_page_module.h"
#include "core/fxge/cfx_graphstatedata.h"
#include "core/fxge/dib/fx_dib.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

RedactRegion RectRegion(float left, float bottom, float right, float top) {
  RedactRegion region;
  region.bbox = CFX_FloatRect(left, bottom, right, top);
  return region;
}

size_t CountPointType(const CPDF_Path& path, CFX_Path::Point::Type type) {
  return std::ranges::count_if(
      path.GetPoints(),
      [type](const CFX_Path::Point& point) { return point.type_ == type; });
}

void InitializeFilledRectangle(CPDF_PathObject* path,
                               float left,
                               float bottom,
                               float right,
                               float top) {
  path->SetDefaultStates();
  path->path().AppendRect(left, bottom, right, top);
  path->set_winding_filltype();
  path->set_stroke(false);
  path->SetPathMatrix(CFX_Matrix());
}

}  // namespace

using CPDFPathRedactorTest = TestWithPageModule;

TEST_F(CPDFPathRedactorTest, SplitsFilledRectangleInsteadOfDroppingIt) {
  CPDF_PathObject path;
  InitializeFilledRectangle(&path, 0.0f, 0.0f, 100.0f, 20.0f);
  const std::array<RedactRegion, 1> regions = {
      RectRegion(40.0f, -10.0f, 60.0f, 30.0f)};
  const CPDF_PathRedactor redactor(regions);
  ASSERT_TRUE(redactor.IsValid());

  PathRedactionResult result = redactor.Redact(&path, CFX_Matrix());

  EXPECT_TRUE(result.succeeded);
  EXPECT_TRUE(result.changed);
  EXPECT_FALSE(result.remove_original);
  EXPECT_FALSE(result.trailing_object);
  EXPECT_TRUE(path.has_alternate_filltype());
  EXPECT_FALSE(path.stroke());
  EXPECT_EQ(2u, CountPointType(path.path(), CFX_Path::Point::Type::kMove));
}

TEST_F(CPDFPathRedactorTest, PreservesEvenOddRuleForEnclosedHole) {
  CPDF_PathObject path;
  InitializeFilledRectangle(&path, 0.0f, 0.0f, 100.0f, 20.0f);
  const std::array<RedactRegion, 1> regions = {
      RectRegion(40.0f, 5.0f, 60.0f, 15.0f)};
  const CPDF_PathRedactor redactor(regions);

  PathRedactionResult result = redactor.Redact(&path, CFX_Matrix());

  ASSERT_TRUE(result.succeeded);
  ASSERT_TRUE(result.changed);
  EXPECT_TRUE(path.has_alternate_filltype());
  EXPECT_EQ(2u, CountPointType(path.path(), CFX_Path::Point::Type::kMove));
}

TEST_F(CPDFPathRedactorTest, PreservesCurvesOnBothSidesOfCut) {
  CPDF_PathObject path;
  path.SetDefaultStates();
  path.path().AppendPoint(CFX_PointF(0.0f, 0.0f), CFX_Path::Point::Type::kMove);
  path.path().AppendPoint(CFX_PointF(30.0f, 80.0f),
                          CFX_Path::Point::Type::kBezier);
  path.path().AppendPoint(CFX_PointF(70.0f, 80.0f),
                          CFX_Path::Point::Type::kBezier);
  path.path().AppendPoint(CFX_PointF(100.0f, 0.0f),
                          CFX_Path::Point::Type::kBezier);
  path.path().AppendPoint(CFX_PointF(100.0f, -20.0f),
                          CFX_Path::Point::Type::kLine);
  path.path().AppendPoint(CFX_PointF(70.0f, 40.0f),
                          CFX_Path::Point::Type::kBezier);
  path.path().AppendPoint(CFX_PointF(30.0f, 40.0f),
                          CFX_Path::Point::Type::kBezier);
  path.path().AppendPointAndClose(CFX_PointF(0.0f, -20.0f),
                                  CFX_Path::Point::Type::kBezier);
  path.set_winding_filltype();
  path.SetPathMatrix(CFX_Matrix());

  const std::array<RedactRegion, 1> regions = {
      RectRegion(45.0f, -100.0f, 55.0f, 100.0f)};
  const CPDF_PathRedactor redactor(regions);
  PathRedactionResult result = redactor.Redact(&path, CFX_Matrix());

  ASSERT_TRUE(result.succeeded);
  ASSERT_TRUE(result.changed);
  EXPECT_EQ(2u, CountPointType(path.path(), CFX_Path::Point::Type::kMove));
  EXPECT_GT(CountPointType(path.path(), CFX_Path::Point::Type::kBezier), 0u);
}

TEST_F(CPDFPathRedactorTest, ExpandsAndSplitsDashedStroke) {
  CPDF_PathObject path;
  path.SetDefaultStates();
  path.path().AppendPoint(CFX_PointF(0.0f, 0.0f), CFX_Path::Point::Type::kMove);
  path.path().AppendPoint(CFX_PointF(100.0f, 0.0f),
                          CFX_Path::Point::Type::kLine);
  path.set_no_filltype();
  path.set_stroke(true);
  path.mutable_graph_state().SetLineWidth(10.0f);
  path.mutable_graph_state().SetLineCap(CFX_GraphStateData::LineCap::kRound);
  path.mutable_graph_state().SetLineDash({12.0f, 4.0f}, 1.0f);
  path.SetPathMatrix(CFX_Matrix());

  const std::array<RedactRegion, 1> regions = {
      RectRegion(40.0f, -20.0f, 60.0f, 20.0f)};
  const CPDF_PathRedactor redactor(regions);
  PathRedactionResult result = redactor.Redact(&path, CFX_Matrix());

  ASSERT_TRUE(result.succeeded);
  ASSERT_TRUE(result.changed);
  EXPECT_FALSE(result.remove_original);
  EXPECT_FALSE(result.trailing_object);
  EXPECT_FALSE(path.stroke());
  EXPECT_TRUE(path.has_alternate_filltype());
  EXPECT_GE(CountPointType(path.path(), CFX_Path::Point::Type::kMove), 2u);
}

TEST_F(CPDFPathRedactorTest, ExpandsHairlineInPageSpace) {
  CPDF_PathObject path;
  path.SetDefaultStates();
  path.path().AppendPoint(CFX_PointF(0.0f, 0.0f), CFX_Path::Point::Type::kMove);
  path.path().AppendPoint(CFX_PointF(100.0f, 0.0f),
                          CFX_Path::Point::Type::kLine);
  path.set_no_filltype();
  path.set_stroke(true);
  path.mutable_graph_state().SetLineWidth(0.0f);
  path.SetPathMatrix(CFX_Matrix(1.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f));

  const std::array<RedactRegion, 1> regions = {
      RectRegion(40.0f, -20.0f, 60.0f, 20.0f)};
  const CPDF_PathRedactor redactor(regions);
  PathRedactionResult result = redactor.Redact(&path, CFX_Matrix());

  ASSERT_TRUE(result.succeeded);
  ASSERT_TRUE(result.changed);
  EXPECT_FALSE(path.stroke());
  EXPECT_TRUE(path.has_alternate_filltype());
  EXPECT_NEAR(1.0f, path.GetRect().Height(), 0.001f);
}

TEST_F(CPDFPathRedactorTest, SplitsFillAndStrokePaintSemantics) {
  CPDF_PathObject path;
  InitializeFilledRectangle(&path, 0.0f, 0.0f, 100.0f, 20.0f);
  path.set_stroke(true);
  path.mutable_graph_state().SetLineWidth(6.0f);
  path.mutable_color_state().SetFillColorRef(FXSYS_BGR(255, 0, 0));
  path.mutable_color_state().SetStrokeColorRef(FXSYS_BGR(0, 0, 255));
  path.mutable_general_state().SetFillAlpha(0.25f);
  path.mutable_general_state().SetStrokeAlpha(0.75f);
  path.CalcBoundingBox();

  const std::array<RedactRegion, 1> regions = {
      RectRegion(40.0f, -10.0f, 60.0f, 30.0f)};
  const CPDF_PathRedactor redactor(regions);
  PathRedactionResult result = redactor.Redact(&path, CFX_Matrix());

  ASSERT_TRUE(result.succeeded);
  ASSERT_TRUE(result.changed);
  ASSERT_TRUE(result.trailing_object);
  EXPECT_FALSE(path.stroke());
  EXPECT_FLOAT_EQ(0.25f, path.general_state().GetFillAlpha());
  EXPECT_FALSE(result.trailing_object->stroke());
  EXPECT_TRUE(result.trailing_object->has_alternate_filltype());
  EXPECT_EQ(FXSYS_BGR(0, 0, 255),
            result.trailing_object->color_state().GetFillColorRef());
  EXPECT_FLOAT_EQ(0.75f,
                  result.trailing_object->general_state().GetFillAlpha());
}

TEST_F(CPDFPathRedactorTest, UsesExactOrientedRegionInsteadOfBoundingBox) {
  CPDF_PathObject path;
  InitializeFilledRectangle(&path, 0.0f, 0.0f, 3.0f, 3.0f);
  RedactRegion region;
  region.bbox = CFX_FloatRect(0.0f, 0.0f, 20.0f, 20.0f);
  region.has_quad = true;
  region.quad = {CFX_PointF(0.0f, 10.0f), CFX_PointF(10.0f, 20.0f),
                 CFX_PointF(10.0f, 0.0f), CFX_PointF(20.0f, 10.0f)};
  const std::array<RedactRegion, 1> regions = {region};
  const CPDF_PathRedactor redactor(regions);

  PathRedactionResult result = redactor.Redact(&path, CFX_Matrix());

  EXPECT_TRUE(result.succeeded);
  EXPECT_FALSE(result.changed);
  EXPECT_FALSE(result.remove_original);
}

TEST_F(CPDFPathRedactorTest, MalformedIntersectingPathFailsWithoutMutation) {
  CPDF_PathObject path;
  path.SetDefaultStates();
  path.path().AppendPoint(CFX_PointF(0.0f, 0.0f), CFX_Path::Point::Type::kMove);
  path.path().AppendPoint(CFX_PointF(100.0f, 100.0f),
                          CFX_Path::Point::Type::kBezier);
  path.set_winding_filltype();
  path.SetPathMatrix(CFX_Matrix());
  const size_t original_point_count = path.path().GetPoints().size();

  const std::array<RedactRegion, 1> regions = {
      RectRegion(40.0f, 40.0f, 60.0f, 60.0f)};
  const CPDF_PathRedactor redactor(regions);
  PathRedactionResult result = redactor.Redact(&path, CFX_Matrix());

  EXPECT_FALSE(result.succeeded);
  EXPECT_FALSE(result.changed);
  EXPECT_EQ(original_point_count, path.path().GetPoints().size());
}

TEST_F(CPDFPathRedactorTest, ClipsPartiallyRedactedShading) {
  CPDF_ShadingObject shading(CPDF_PageObject::kNoContentStream, nullptr,
                             CFX_Matrix());
  shading.SetRect(CFX_FloatRect(0.0f, 0.0f, 100.0f, 20.0f));
  const std::array<RedactRegion, 1> regions = {
      RectRegion(40.0f, -10.0f, 60.0f, 30.0f)};
  const CPDF_PathRedactor redactor(regions);

  PathRedactionResult result = redactor.RedactShading(&shading, CFX_Matrix());

  ASSERT_TRUE(result.succeeded);
  ASSERT_TRUE(result.changed);
  EXPECT_FALSE(result.remove_original);
  ASSERT_EQ(1u, shading.clip_path().GetPathCount());
  EXPECT_EQ(CFX_FillRenderOptions::FillType::kEvenOdd,
            shading.clip_path().GetClipType(0));
  EXPECT_EQ(2u, CountPointType(shading.clip_path().GetPath(0),
                               CFX_Path::Point::Type::kMove));
}
