// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "core/fpdfapi/edit/cpdf_path_redactor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "core/fpdfapi/edit/cpdf_text_redactor.h"
#include "core/fpdfapi/page/cpdf_color.h"
#include "core/fpdfapi/page/cpdf_path.h"
#include "core/fpdfapi/page/cpdf_pathobject.h"
#include "core/fpdfapi/page/cpdf_shadingobject.h"
#include "core/fxge/cfx_fillrenderoptions.h"
#include "core/fxge/cfx_graphstatedata.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/core/SkStrokeRec.h"
#include "third_party/skia/include/pathops/SkPathOps.h"
#include "third_party/skia/src/utils/SkDashPathPriv.h"

namespace {

constexpr float kMatrixDeterminantTolerance = 1e-12f;
constexpr float kRedactionBoundaryGuard = 1e-3f;
constexpr float kHairlineWidth = 1.0f;
constexpr int kConicToQuadPow2 = 2;

bool Intersects(const CFX_FloatRect& a, const CFX_FloatRect& b) {
  return a.right > b.left && a.left < b.right && a.top > b.bottom &&
         a.bottom < b.top;
}

bool IsFiniteRect(const CFX_FloatRect& rect) {
  return std::isfinite(rect.left) && std::isfinite(rect.bottom) &&
         std::isfinite(rect.right) && std::isfinite(rect.top);
}

bool IsFinitePoint(const CFX_PointF& point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

bool IsFinitePoint(const SkPoint& point) {
  return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool IsFiniteMatrix(const CFX_Matrix& matrix) {
  return std::isfinite(matrix.a) && std::isfinite(matrix.b) &&
         std::isfinite(matrix.c) && std::isfinite(matrix.d) &&
         std::isfinite(matrix.e) && std::isfinite(matrix.f);
}

bool IsInvertibleMatrix(const CFX_Matrix& matrix) {
  return IsFiniteMatrix(matrix) &&
         std::fabs(matrix.a * matrix.d - matrix.b * matrix.c) >
             kMatrixDeterminantTolerance;
}

SkMatrix ToSkMatrix(const CFX_Matrix& matrix) {
  SkMatrix result;
  result.setAll(matrix.a, matrix.c, matrix.e, matrix.b, matrix.d, matrix.f,
                0.0f, 0.0f, 1.0f);
  return result;
}

SkPathFillType ToSkFillType(const CPDF_PathObject& path) {
  return path.has_alternate_filltype() ? SkPathFillType::kEvenOdd
                                       : SkPathFillType::kWinding;
}

std::optional<SkPath> ToSkPath(const CPDF_Path& source,
                               SkPathFillType fill_type) {
  SkPathBuilder builder;
  builder.setFillType(fill_type);

  const std::vector<CFX_Path::Point>& points = source.GetPoints();
  bool has_current_point = false;
  for (size_t i = 0; i < points.size(); ++i) {
    const CFX_Path::Point& point = points[i];
    if (!IsFinitePoint(point.point_)) {
      return std::nullopt;
    }

    switch (point.type_) {
      case CFX_Path::Point::Type::kMove:
        builder.moveTo(point.point_.x, point.point_.y);
        has_current_point = true;
        if (point.close_figure_) {
          builder.close();
        }
        break;

      case CFX_Path::Point::Type::kLine:
        if (!has_current_point) {
          return std::nullopt;
        }
        builder.lineTo(point.point_.x, point.point_.y);
        if (point.close_figure_) {
          builder.close();
        }
        break;

      case CFX_Path::Point::Type::kBezier:
        if (!has_current_point || i + 2 >= points.size() ||
            points[i + 1].type_ != CFX_Path::Point::Type::kBezier ||
            points[i + 2].type_ != CFX_Path::Point::Type::kBezier ||
            point.close_figure_ || points[i + 1].close_figure_ ||
            !IsFinitePoint(points[i + 1].point_) ||
            !IsFinitePoint(points[i + 2].point_)) {
          return std::nullopt;
        }
        builder.cubicTo(point.point_.x, point.point_.y, points[i + 1].point_.x,
                        points[i + 1].point_.y, points[i + 2].point_.x,
                        points[i + 2].point_.y);
        if (points[i + 2].close_figure_) {
          builder.close();
        }
        i += 2;
        break;
    }
  }

  SkPath result = builder.detach();
  return result.isFinite() ? std::optional<SkPath>(std::move(result))
                           : std::nullopt;
}

void AppendQuadraticAsCubic(CPDF_Path* output,
                            const SkPoint& start,
                            const SkPoint& control,
                            const SkPoint& end) {
  const CFX_PointF control1(
      start.x() + (control.x() - start.x()) * (2.0f / 3.0f),
      start.y() + (control.y() - start.y()) * (2.0f / 3.0f));
  const CFX_PointF control2(end.x() + (control.x() - end.x()) * (2.0f / 3.0f),
                            end.y() + (control.y() - end.y()) * (2.0f / 3.0f));
  output->AppendPoint(control1, CFX_Path::Point::Type::kBezier);
  output->AppendPoint(control2, CFX_Path::Point::Type::kBezier);
  output->AppendPoint(CFX_PointF(end.x(), end.y()),
                      CFX_Path::Point::Type::kBezier);
}

std::optional<CPDF_Path> ToPDFPath(const SkPath& source) {
  if (!source.isFinite()) {
    return std::nullopt;
  }

  CPDF_Path output;
  output.Emplace();
  SkPath::Iter iterator(source, /*forceClose=*/false);
  SkPoint points[4];
  bool has_current_point = false;

  while (true) {
    const SkPath::Verb verb = iterator.next(points);
    switch (verb) {
      case SkPath::kMove_Verb:
        if (!IsFinitePoint(points[0])) {
          return std::nullopt;
        }
        output.AppendPoint(CFX_PointF(points[0].x(), points[0].y()),
                           CFX_Path::Point::Type::kMove);
        has_current_point = true;
        break;

      case SkPath::kLine_Verb:
        if (!has_current_point || !IsFinitePoint(points[1])) {
          return std::nullopt;
        }
        output.AppendPoint(CFX_PointF(points[1].x(), points[1].y()),
                           CFX_Path::Point::Type::kLine);
        break;

      case SkPath::kQuad_Verb:
        if (!has_current_point || !IsFinitePoint(points[0]) ||
            !IsFinitePoint(points[1]) || !IsFinitePoint(points[2])) {
          return std::nullopt;
        }
        AppendQuadraticAsCubic(&output, points[0], points[1], points[2]);
        break;

      case SkPath::kConic_Verb: {
        if (!has_current_point || !IsFinitePoint(points[0]) ||
            !IsFinitePoint(points[1]) || !IsFinitePoint(points[2]) ||
            !std::isfinite(iterator.conicWeight())) {
          return std::nullopt;
        }
        std::array<SkPoint, 1 + 2 * (1 << kConicToQuadPow2)> quads;
        const int count = SkPath::ConvertConicToQuads(
            points[0], points[1], points[2], iterator.conicWeight(),
            quads.data(), kConicToQuadPow2);
        if (count <= 0) {
          return std::nullopt;
        }
        for (int i = 0; i < count; ++i) {
          const SkPoint& start = quads[2 * i];
          const SkPoint& control = quads[2 * i + 1];
          const SkPoint& end = quads[2 * i + 2];
          if (!IsFinitePoint(start) || !IsFinitePoint(control) ||
              !IsFinitePoint(end)) {
            return std::nullopt;
          }
          AppendQuadraticAsCubic(&output, start, control, end);
        }
        break;
      }

      case SkPath::kCubic_Verb:
        if (!has_current_point || !IsFinitePoint(points[1]) ||
            !IsFinitePoint(points[2]) || !IsFinitePoint(points[3])) {
          return std::nullopt;
        }
        output.AppendPoint(CFX_PointF(points[1].x(), points[1].y()),
                           CFX_Path::Point::Type::kBezier);
        output.AppendPoint(CFX_PointF(points[2].x(), points[2].y()),
                           CFX_Path::Point::Type::kBezier);
        output.AppendPoint(CFX_PointF(points[3].x(), points[3].y()),
                           CFX_Path::Point::Type::kBezier);
        break;

      case SkPath::kClose_Verb:
        if (!has_current_point) {
          return std::nullopt;
        }
        output.ClosePath();
        break;

      case SkPath::kDone_Verb:
        return output;
    }
  }
}

std::optional<CFX_FillRenderOptions::FillType> ToPDFFillType(
    SkPathFillType fill_type) {
  switch (fill_type) {
    case SkPathFillType::kWinding:
      return CFX_FillRenderOptions::FillType::kWinding;
    case SkPathFillType::kEvenOdd:
      return CFX_FillRenderOptions::FillType::kEvenOdd;
    case SkPathFillType::kInverseWinding:
    case SkPathFillType::kInverseEvenOdd:
      return std::nullopt;
  }
}

std::optional<SkPath> BuildRedactionPath(const RedactRegion& region) {
  SkPathBuilder builder;
  builder.setFillType(SkPathFillType::kWinding);
  if (region.has_quad) {
    for (const CFX_PointF& point : region.quad) {
      if (!IsFinitePoint(point)) {
        return std::nullopt;
      }
    }
    builder.moveTo(region.quad[0].x, region.quad[0].y);
    builder.lineTo(region.quad[1].x, region.quad[1].y);
    builder.lineTo(region.quad[3].x, region.quad[3].y);
    builder.lineTo(region.quad[2].x, region.quad[2].y);
    builder.close();
  } else {
    CFX_FloatRect rect = region.bbox;
    rect.Normalize();
    if (!std::isfinite(rect.left) || !std::isfinite(rect.bottom) ||
        !std::isfinite(rect.right) || !std::isfinite(rect.top) ||
        rect.right <= rect.left || rect.top <= rect.bottom) {
      return std::nullopt;
    }
    builder.addRect(
        SkRect::MakeLTRB(rect.left, rect.bottom, rect.right, rect.top));
  }

  SkStrokeRec guard(SkStrokeRec::kHairline_InitStyle);
  guard.setStrokeStyle(2.0f * kRedactionBoundaryGuard,
                       /*strokeAndFill=*/true);
  guard.setStrokeParams(SkPaint::kButt_Cap, SkPaint::kMiter_Join, 4.0f);
  SkPathBuilder expanded_builder;
  if (!guard.applyToPath(&expanded_builder, builder.detach())) {
    return std::nullopt;
  }
  SkPath expanded = expanded_builder.detach();
  if (!expanded.isFinite() || expanded.isEmpty()) {
    return std::nullopt;
  }
  return expanded;
}

std::optional<SkPath> BuildRedactionUnion(
    pdfium::span<const RedactRegion> regions) {
  std::optional<SkPath> result;
  for (const RedactRegion& region : regions) {
    std::optional<SkPath> next = BuildRedactionPath(region);
    if (!next) {
      return std::nullopt;
    }
    if (!result) {
      result = std::move(*next);
      continue;
    }
    result = Op(*result, *next, kUnion_SkPathOp);
    if (!result) {
      return std::nullopt;
    }
  }
  return result;
}

std::optional<SkPath> BuildStrokeOutline(const CPDF_PathObject& source,
                                         const SkPath& local_path,
                                         const SkMatrix& local_to_page) {
  const CFX_GraphState& graph_state = source.graph_state();
  float width = graph_state.GetLineWidth();
  const float miter = graph_state.GetMiterLimit();
  const float phase = graph_state.GetLineDashPhase();
  if (!std::isfinite(width) || !std::isfinite(miter) || !std::isfinite(phase)) {
    return std::nullopt;
  }
  width = std::fabs(width);
  const bool is_hairline = width == 0.0f;

  SkPaint::Cap cap = SkPaint::kButt_Cap;
  switch (graph_state.GetLineCap()) {
    case CFX_GraphStateData::LineCap::kButt:
      cap = SkPaint::kButt_Cap;
      break;
    case CFX_GraphStateData::LineCap::kRound:
      cap = SkPaint::kRound_Cap;
      break;
    case CFX_GraphStateData::LineCap::kSquare:
      cap = SkPaint::kSquare_Cap;
      break;
  }
  SkPaint::Join join = SkPaint::kMiter_Join;
  switch (graph_state.GetLineJoin()) {
    case CFX_GraphStateData::LineJoin::kMiter:
      join = SkPaint::kMiter_Join;
      break;
    case CFX_GraphStateData::LineJoin::kRound:
      join = SkPaint::kRound_Join;
      break;
    case CFX_GraphStateData::LineJoin::kBevel:
      join = SkPaint::kBevel_Join;
      break;
  }

  SkStrokeRec stroke_record(SkStrokeRec::kHairline_InitStyle);
  stroke_record.setStrokeStyle(is_hairline ? kHairlineWidth : width);
  stroke_record.setStrokeParams(cap, join, std::max(1.0f, miter));

  std::vector<float> dashes = graph_state.GetLineDashArray();
  if (!dashes.empty()) {
    bool has_positive_interval = false;
    for (float dash : dashes) {
      if (!std::isfinite(dash) || dash < 0.0f) {
        return std::nullopt;
      }
      has_positive_interval |= dash > 0.0f;
    }
    if (!has_positive_interval) {
      return std::nullopt;
    }
    if (dashes.size() % 2 != 0) {
      const std::vector<float> copy = dashes;
      dashes.insert(dashes.end(), copy.begin(), copy.end());
    }
    if (!SkDashPath::ValidDashPath(phase, dashes)) {
      return std::nullopt;
    }
  }

  SkPath path_to_outline = local_path;
  if (!dashes.empty()) {
    float initial_dash_length = 0.0f;
    size_t initial_dash_index = 0;
    float interval_length = 0.0f;
    float adjusted_phase = 0.0f;
    SkDashPath::CalcDashParameters(phase, dashes, &initial_dash_length,
                                   &initial_dash_index, &interval_length,
                                   &adjusted_phase);

    SkStrokeRec dash_stroke = stroke_record;
    if (is_hairline) {
      dash_stroke.setHairlineStyle();
    }
    SkPathBuilder dashed_builder;
    if (SkDashPath::InternalFilter(
            &dashed_builder, path_to_outline, &dash_stroke,
            /*cullRect=*/nullptr, dashes, initial_dash_length,
            static_cast<int32_t>(initial_dash_index), interval_length,
            adjusted_phase)) {
      path_to_outline = dashed_builder.detach();
      stroke_record = dash_stroke;
    } else if (is_hairline) {
      return std::nullopt;
    }
  }

  if (is_hairline) {
    // A PDF zero-width stroke is a device-space hairline. Apply dashing in
    // the original user space, then transform the centerline and widen it in
    // page space. This avoids turning a scaled or sheared hairline into an
    // arbitrarily thick local-space stroke when it is persisted as a fill.
    path_to_outline = path_to_outline.makeTransform(local_to_page);
    if (!path_to_outline.isFinite()) {
      return std::nullopt;
    }
    stroke_record.setStrokeStyle(kHairlineWidth);
  }

  SkPathBuilder outline_builder;
  if (stroke_record.applyToPath(&outline_builder, path_to_outline)) {
    path_to_outline = outline_builder.detach();
  } else if (!stroke_record.isFillStyle()) {
    return std::nullopt;
  }
  SkPath outline = std::move(path_to_outline);
  if (!outline.isFinite()) {
    return std::nullopt;
  }
  if (!is_hairline) {
    outline = outline.makeTransform(local_to_page);
  }
  return outline.isFinite() ? std::optional<SkPath>(std::move(outline))
                            : std::nullopt;
}

struct DifferenceResult {
  bool succeeded = true;
  bool changed = false;
  SkPath surviving_path;
};

DifferenceResult SubtractRedaction(const SkPath& painted_path,
                                   const SkPath& redaction_path) {
  DifferenceResult result;
  result.surviving_path = painted_path;
  if (painted_path.isEmpty()) {
    return result;
  }

  std::optional<SkPath> intersection =
      Op(painted_path, redaction_path, kIntersect_SkPathOp);
  if (!intersection) {
    return {.succeeded = false};
  }
  if (intersection->isEmpty()) {
    return result;
  }

  std::optional<SkPath> difference =
      Op(painted_path, redaction_path, kDifference_SkPathOp);
  if (!difference || !difference->isFinite()) {
    return {.succeeded = false};
  }
  result.changed = true;
  result.surviving_path = std::move(*difference);
  return result;
}

void CopyStrokePaintToFill(CPDF_PathObject* object) {
  CPDF_ColorState& colors = object->mutable_color_state();
  colors.SetFillColorRef(colors.GetStrokeColorRef());
  const CPDF_Color* stroke_color = colors.GetStrokeColor();
  if (stroke_color) {
    CPDF_Color* fill_color = colors.GetMutableFillColor();
    if (fill_color) {
      *fill_color = *stroke_color;
    }
    colors.SetFillColorSpaceResName(colors.GetStrokeColorSpaceResName());
    colors.SetFillPatternResName(colors.GetStrokePatternResName());
  }

  CPDF_GeneralState& general = object->mutable_general_state();
  general.SetFillAlpha(general.GetStrokeAlpha());
  general.SetFillOP(general.GetStrokeOP());
}

std::unique_ptr<CPDF_PathObject> MakeStrokeFillObject(
    const CPDF_PathObject& source,
    const CPDF_Path& geometry,
    CFX_FillRenderOptions::FillType fill_type) {
  auto object = std::make_unique<CPDF_PathObject>(source.GetContentStream());
  object->mutable_clip_path() = source.clip_path();
  object->mutable_graph_state() = source.graph_state();
  object->mutable_color_state() = source.color_state();
  object->mutable_text_state() = source.text_state();
  object->mutable_general_state() = source.general_state();
  object->SetContentMarks(*source.GetContentMarks());
  object->SetResourceName(source.GetResourceName());
  object->path() = geometry;
  object->set_filltype(fill_type);
  object->set_stroke(false);
  CopyStrokePaintToFill(object.get());
  object->SetPathMatrix(source.matrix());
  object->CalcBoundingBox();
  object->SetDirty(true);
  return object;
}

}  // namespace

class CPDF_PathRedactor::Impl {
 public:
  explicit Impl(pdfium::span<const RedactRegion> regions)
      : redaction_path(BuildRedactionUnion(regions)) {
    region_bboxes.reserve(regions.size());
    for (const RedactRegion& region : regions) {
      CFX_FloatRect bbox = region.bbox;
      bbox.Normalize();
      region_bboxes.push_back(bbox);
    }
  }

  std::optional<SkPath> redaction_path;
  std::vector<CFX_FloatRect> region_bboxes;
};

CPDF_PathRedactor::CPDF_PathRedactor(pdfium::span<const RedactRegion> regions)
    : impl_(std::make_unique<Impl>(regions)) {}

CPDF_PathRedactor::~CPDF_PathRedactor() = default;

bool CPDF_PathRedactor::IsValid() const {
  return impl_ && impl_->redaction_path.has_value();
}

PathRedactionResult CPDF_PathRedactor::Redact(
    CPDF_PathObject* path,
    const CFX_Matrix& parent_to_page) const {
  if (!path || !IsValid()) {
    return {.succeeded = false};
  }
  const bool has_fill = !path->has_no_filltype();
  const bool has_stroke = path->stroke();
  if (!has_fill && !has_stroke) {
    return {};
  }

  CFX_FloatRect page_bbox = parent_to_page.TransformRect(path->GetRect());
  page_bbox.Normalize();
  if (!IsFiniteRect(page_bbox)) {
    return {.succeeded = false};
  }
  bool might_intersect = false;
  for (const CFX_FloatRect& region_bbox : impl_->region_bboxes) {
    if (Intersects(page_bbox, region_bbox)) {
      might_intersect = true;
      break;
    }
  }
  if (!might_intersect) {
    return {};
  }

  const CFX_Matrix local_to_page = path->matrix() * parent_to_page;
  if (!IsInvertibleMatrix(local_to_page)) {
    return {.succeeded = false};
  }
  std::optional<SkPath> local_path =
      ToSkPath(path->path(), ToSkFillType(*path));
  if (!local_path) {
    return {.succeeded = false};
  }

  const SkMatrix sk_local_to_page = ToSkMatrix(local_to_page);
  DifferenceResult fill_result;
  if (has_fill) {
    const SkPath page_fill = local_path->makeTransform(sk_local_to_page);
    if (!page_fill.isFinite()) {
      return {.succeeded = false};
    }
    fill_result = SubtractRedaction(page_fill, *impl_->redaction_path);
    if (!fill_result.succeeded) {
      return {.succeeded = false};
    }
  }

  DifferenceResult stroke_result;
  if (has_stroke) {
    std::optional<SkPath> page_outline =
        BuildStrokeOutline(*path, *local_path, sk_local_to_page);
    if (!page_outline) {
      return {.succeeded = false};
    }
    stroke_result = SubtractRedaction(*page_outline, *impl_->redaction_path);
    if (!stroke_result.succeeded) {
      return {.succeeded = false};
    }
  }

  if (!fill_result.changed && !stroke_result.changed) {
    return {};
  }

  const CFX_Matrix page_to_local = local_to_page.GetInverse();
  const SkMatrix sk_page_to_local = ToSkMatrix(page_to_local);
  std::optional<CPDF_Path> surviving_fill;
  CFX_FillRenderOptions::FillType surviving_fill_type = path->filltype();
  if (has_fill && !fill_result.surviving_path.isEmpty()) {
    if (fill_result.changed) {
      std::optional<CFX_FillRenderOptions::FillType> fill_type =
          ToPDFFillType(fill_result.surviving_path.getFillType());
      if (!fill_type) {
        return {.succeeded = false};
      }
      surviving_fill_type = *fill_type;
      surviving_fill =
          ToPDFPath(fill_result.surviving_path.makeTransform(sk_page_to_local));
      if (!surviving_fill) {
        return {.succeeded = false};
      }
    } else {
      surviving_fill = path->path();
    }
  }

  std::optional<CPDF_Path> surviving_stroke;
  CFX_FillRenderOptions::FillType surviving_stroke_type =
      CFX_FillRenderOptions::FillType::kWinding;
  if (has_stroke && !stroke_result.surviving_path.isEmpty()) {
    std::optional<CFX_FillRenderOptions::FillType> fill_type =
        ToPDFFillType(stroke_result.surviving_path.getFillType());
    if (!fill_type) {
      return {.succeeded = false};
    }
    surviving_stroke_type = *fill_type;
    surviving_stroke =
        ToPDFPath(stroke_result.surviving_path.makeTransform(sk_page_to_local));
    if (!surviving_stroke) {
      return {.succeeded = false};
    }
  }

  PathRedactionResult result;
  result.changed = true;
  if (!surviving_fill && !surviving_stroke) {
    result.remove_original = true;
    return result;
  }

  std::unique_ptr<CPDF_PathObject> stroke_object;
  if (surviving_fill && surviving_stroke) {
    stroke_object =
        MakeStrokeFillObject(*path, *surviving_stroke, surviving_stroke_type);
  }

  if (surviving_fill) {
    path->path() = *surviving_fill;
    if (fill_result.changed) {
      path->set_filltype(surviving_fill_type);
    }
    path->set_stroke(false);
  } else {
    path->path() = *surviving_stroke;
    path->set_filltype(surviving_stroke_type);
    path->set_stroke(false);
    CopyStrokePaintToFill(path);
  }
  path->CalcBoundingBox();
  path->SetDirty(true);
  result.trailing_object = std::move(stroke_object);
  return result;
}

PathRedactionResult CPDF_PathRedactor::RedactShading(
    CPDF_ShadingObject* shading,
    const CFX_Matrix& parent_to_page) const {
  if (!shading || !IsValid()) {
    return {.succeeded = false};
  }

  CFX_FloatRect local_bbox = shading->GetRect();
  local_bbox.Normalize();
  CFX_FloatRect page_bbox = parent_to_page.TransformRect(local_bbox);
  page_bbox.Normalize();
  if (!IsFiniteRect(local_bbox) || !IsFiniteRect(page_bbox)) {
    return {.succeeded = false};
  }

  bool might_intersect = false;
  for (const CFX_FloatRect& region_bbox : impl_->region_bboxes) {
    if (Intersects(page_bbox, region_bbox)) {
      might_intersect = true;
      break;
    }
  }
  if (!might_intersect) {
    return {};
  }
  if (!IsInvertibleMatrix(parent_to_page)) {
    return {.succeeded = false};
  }

  SkPathBuilder bounds_builder;
  bounds_builder.addRect(SkRect::MakeLTRB(local_bbox.left, local_bbox.bottom,
                                          local_bbox.right, local_bbox.top));
  const SkPath page_shape =
      bounds_builder.detach().makeTransform(ToSkMatrix(parent_to_page));
  if (!page_shape.isFinite()) {
    return {.succeeded = false};
  }
  DifferenceResult difference =
      SubtractRedaction(page_shape, *impl_->redaction_path);
  if (!difference.succeeded) {
    return {.succeeded = false};
  }
  if (!difference.changed) {
    return {};
  }
  if (difference.surviving_path.isEmpty()) {
    return {.changed = true, .remove_original = true};
  }

  const CFX_Matrix page_to_local = parent_to_page.GetInverse();
  std::optional<CFX_FillRenderOptions::FillType> clip_fill_type =
      ToPDFFillType(difference.surviving_path.getFillType());
  if (!clip_fill_type) {
    return {.succeeded = false};
  }
  std::optional<CPDF_Path> surviving_clip = ToPDFPath(
      difference.surviving_path.makeTransform(ToSkMatrix(page_to_local)));
  if (!surviving_clip) {
    return {.succeeded = false};
  }
  shading->mutable_clip_path().AppendPath(std::move(*surviving_clip),
                                          *clip_fill_type);
  shading->SetDirty(true);
  return {.changed = true};
}
