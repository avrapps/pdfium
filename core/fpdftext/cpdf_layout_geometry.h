// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFTEXT_CPDF_LAYOUT_GEOMETRY_H_
#define CORE_FPDFTEXT_CPDF_LAYOUT_GEOMETRY_H_

#include <algorithm>
#include <cmath>

#include "core/fxcrt/fx_coordinates.h"

namespace pdfium {
namespace layout {

// =============================================================================
// PDF Coordinate System Convention
// =============================================================================
//
// PDF uses a coordinate system where Y increases upward, with origin at
// bottom-left. Therefore rect.top > rect.bottom.
//
// These helpers make intent explicit and prevent coordinate bugs.

// -----------------------------------------------------------------------------
// Basic Rect Accessors
// -----------------------------------------------------------------------------

inline float RectTop(const CFX_FloatRect& r) {
  return r.top;
}

inline float RectBottom(const CFX_FloatRect& r) {
  return r.bottom;
}

inline float RectLeft(const CFX_FloatRect& r) {
  return r.left;
}

inline float RectRight(const CFX_FloatRect& r) {
  return r.right;
}

inline float RectHeight(const CFX_FloatRect& r) {
  return r.top - r.bottom;
}

inline float RectWidth(const CFX_FloatRect& r) {
  return r.right - r.left;
}

// -----------------------------------------------------------------------------
// Center Points
// -----------------------------------------------------------------------------

inline float RectCenterY(const CFX_FloatRect& r) {
  return (r.top + r.bottom) / 2.0f;
}

inline float RectCenterX(const CFX_FloatRect& r) {
  return (r.left + r.right) / 2.0f;
}

// -----------------------------------------------------------------------------
// Positional Relationships (PDF coords: higher Y = above)
// -----------------------------------------------------------------------------

// Is r1 above r2? (bottom of r1 >= top of r2)
inline bool IsAbove(const CFX_FloatRect& r1, const CFX_FloatRect& r2) {
  return RectBottom(r1) >= RectTop(r2);
}

// Is r1 below r2? (top of r1 <= bottom of r2)
inline bool IsBelow(const CFX_FloatRect& r1, const CFX_FloatRect& r2) {
  return RectTop(r1) <= RectBottom(r2);
}

// Is r1 to the left of r2? (right of r1 <= left of r2)
inline bool IsLeftOf(const CFX_FloatRect& r1, const CFX_FloatRect& r2) {
  return RectRight(r1) <= RectLeft(r2);
}

// Is r1 to the right of r2? (left of r1 >= right of r2)
inline bool IsRightOf(const CFX_FloatRect& r1, const CFX_FloatRect& r2) {
  return RectLeft(r1) >= RectRight(r2);
}

// -----------------------------------------------------------------------------
// Query Rect Construction
// -----------------------------------------------------------------------------

// Create a query rect BELOW the given rect (lower Y values in PDF coords)
inline CFX_FloatRect MakeRectBelow(const CFX_FloatRect& r, float window_h) {
  CFX_FloatRect result;
  result.left = r.left;
  result.right = r.right;
  result.top = r.bottom;               // starts at bottom of original
  result.bottom = r.bottom - window_h;  // extends downward (lower Y)
  return result;
}

// Create a query rect ABOVE the given rect (higher Y values in PDF coords)
inline CFX_FloatRect MakeRectAbove(const CFX_FloatRect& r, float window_h) {
  CFX_FloatRect result;
  result.left = r.left;
  result.right = r.right;
  result.bottom = r.top;             // starts at top of original
  result.top = r.top + window_h;     // extends upward (higher Y)
  return result;
}

// Create a query rect to the RIGHT of the given rect
inline CFX_FloatRect MakeRectRight(const CFX_FloatRect& r, float window_w) {
  CFX_FloatRect result;
  result.left = r.right;
  result.right = r.right + window_w;
  result.top = r.top;
  result.bottom = r.bottom;
  return result;
}

// Create a query rect to the LEFT of the given rect
inline CFX_FloatRect MakeRectLeft(const CFX_FloatRect& r, float window_w) {
  CFX_FloatRect result;
  result.left = r.left - window_w;
  result.right = r.left;
  result.top = r.top;
  result.bottom = r.bottom;
  return result;
}

// Expand rect by given margins
inline CFX_FloatRect ExpandRect(const CFX_FloatRect& r,
                                 float left_margin,
                                 float bottom_margin,
                                 float right_margin,
                                 float top_margin) {
  CFX_FloatRect result;
  result.left = r.left - left_margin;
  result.bottom = r.bottom - bottom_margin;
  result.right = r.right + right_margin;
  result.top = r.top + top_margin;
  return result;
}

// Expand rect uniformly
inline CFX_FloatRect ExpandRect(const CFX_FloatRect& r, float margin) {
  return ExpandRect(r, margin, margin, margin, margin);
}

// -----------------------------------------------------------------------------
// Overlap and Gap Calculations
// -----------------------------------------------------------------------------

// Vertical overlap amount (positive if overlapping)
inline float VerticalOverlap(const CFX_FloatRect& r1, const CFX_FloatRect& r2) {
  float overlap_top = std::min(RectTop(r1), RectTop(r2));
  float overlap_bottom = std::max(RectBottom(r1), RectBottom(r2));
  return std::max(0.0f, overlap_top - overlap_bottom);
}

// Vertical overlap as ratio of smaller rect height
inline float VerticalOverlapRatio(const CFX_FloatRect& r1,
                                   const CFX_FloatRect& r2) {
  float overlap = VerticalOverlap(r1, r2);
  float min_h = std::min(RectHeight(r1), RectHeight(r2));
  return min_h > 0.0f ? overlap / min_h : 0.0f;
}

// Horizontal overlap amount (positive if overlapping)
inline float HorizontalOverlap(const CFX_FloatRect& r1,
                                const CFX_FloatRect& r2) {
  float overlap_left = std::max(RectLeft(r1), RectLeft(r2));
  float overlap_right = std::min(RectRight(r1), RectRight(r2));
  return std::max(0.0f, overlap_right - overlap_left);
}

// Horizontal overlap as ratio of smaller rect width
inline float HorizontalOverlapRatio(const CFX_FloatRect& r1,
                                     const CFX_FloatRect& r2) {
  float overlap = HorizontalOverlap(r1, r2);
  float min_w = std::min(RectWidth(r1), RectWidth(r2));
  return min_w > 0.0f ? overlap / min_w : 0.0f;
}

// Horizontal gap (positive = r2 is to the right with gap, negative = overlap)
inline float HorizontalGap(const CFX_FloatRect& r1, const CFX_FloatRect& r2) {
  return RectLeft(r2) - RectRight(r1);
}

// Vertical gap (positive = r1 is above r2 with gap, negative = overlap)
inline float VerticalGap(const CFX_FloatRect& r1, const CFX_FloatRect& r2) {
  return RectBottom(r1) - RectTop(r2);
}

// -----------------------------------------------------------------------------
// Intersection and Containment
// -----------------------------------------------------------------------------

// Check if two rects intersect
inline bool RectsIntersect(const CFX_FloatRect& r1, const CFX_FloatRect& r2) {
  return RectLeft(r1) < RectRight(r2) && RectRight(r1) > RectLeft(r2) &&
         RectBottom(r1) < RectTop(r2) && RectTop(r1) > RectBottom(r2);
}

// Check if r1 contains r2
inline bool RectContains(const CFX_FloatRect& r1, const CFX_FloatRect& r2) {
  return RectLeft(r1) <= RectLeft(r2) && RectRight(r1) >= RectRight(r2) &&
         RectBottom(r1) <= RectBottom(r2) && RectTop(r1) >= RectTop(r2);
}

// Check if rect contains point
inline bool RectContainsPoint(const CFX_FloatRect& r, float x, float y) {
  return x >= RectLeft(r) && x <= RectRight(r) && y >= RectBottom(r) &&
         y <= RectTop(r);
}

// -----------------------------------------------------------------------------
// Union
// -----------------------------------------------------------------------------

// Compute union of two rects
inline CFX_FloatRect UnionRects(const CFX_FloatRect& r1,
                                 const CFX_FloatRect& r2) {
  CFX_FloatRect result;
  result.left = std::min(RectLeft(r1), RectLeft(r2));
  result.bottom = std::min(RectBottom(r1), RectBottom(r2));
  result.right = std::max(RectRight(r1), RectRight(r2));
  result.top = std::max(RectTop(r1), RectTop(r2));
  return result;
}

// -----------------------------------------------------------------------------
// Alignment Checks
// -----------------------------------------------------------------------------

// Check if two rects are left-aligned within tolerance
inline bool IsLeftAligned(const CFX_FloatRect& r1,
                           const CFX_FloatRect& r2,
                           float tolerance) {
  return std::abs(RectLeft(r1) - RectLeft(r2)) <= tolerance;
}

// Check if two rects are right-aligned within tolerance
inline bool IsRightAligned(const CFX_FloatRect& r1,
                            const CFX_FloatRect& r2,
                            float tolerance) {
  return std::abs(RectRight(r1) - RectRight(r2)) <= tolerance;
}

// Check if two rects are center-aligned (horizontally) within tolerance
inline bool IsCenterAligned(const CFX_FloatRect& r1,
                             const CFX_FloatRect& r2,
                             float tolerance) {
  return std::abs(RectCenterX(r1) - RectCenterX(r2)) <= tolerance;
}

// Check if two rects are aligned (any of left/right/center)
inline bool IsAligned(const CFX_FloatRect& r1,
                       const CFX_FloatRect& r2,
                       float tolerance) {
  return IsLeftAligned(r1, r2, tolerance) ||
         IsRightAligned(r1, r2, tolerance) ||
         IsCenterAligned(r1, r2, tolerance);
}

}  // namespace layout
}  // namespace pdfium

#endif  // CORE_FPDFTEXT_CPDF_LAYOUT_GEOMETRY_H_
