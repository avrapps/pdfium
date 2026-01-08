// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdftext/cpdf_spatial_grid.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace pdfium {
namespace layout {

SpatialGrid::SpatialGrid(const CFX_FloatRect& bounds, float cell_size)
    : cell_size_(std::max(cell_size, 1.0f)),
      x_origin_(bounds.left),
      y_origin_(bounds.bottom) {
  float width = RectWidth(bounds);
  float height = RectHeight(bounds);

  cols_ = std::max(1, static_cast<int>(std::ceil(width / cell_size_)));
  rows_ = std::max(1, static_cast<int>(std::ceil(height / cell_size_)));

  // Limit grid size to prevent memory issues
  constexpr int kMaxCells = 10000;
  if (cols_ * rows_ > kMaxCells) {
    float scale = std::sqrt(static_cast<float>(cols_ * rows_) / kMaxCells);
    cell_size_ *= scale;
    cols_ = std::max(1, static_cast<int>(std::ceil(width / cell_size_)));
    rows_ = std::max(1, static_cast<int>(std::ceil(height / cell_size_)));
  }

  cells_.resize(cols_ * rows_);
}

SpatialGrid::~SpatialGrid() = default;

int SpatialGrid::GetCellX(float x) const {
  int cx = static_cast<int>((x - x_origin_) / cell_size_);
  return std::clamp(cx, 0, cols_ - 1);
}

int SpatialGrid::GetCellY(float y) const {
  int cy = static_cast<int>((y - y_origin_) / cell_size_);
  return std::clamp(cy, 0, rows_ - 1);
}

int SpatialGrid::GetCellIndex(int cx, int cy) const {
  return cy * cols_ + cx;
}

void SpatialGrid::Insert(int idx, const CFX_FloatRect& bbox) {
  int min_cx = GetCellX(RectLeft(bbox));
  int max_cx = GetCellX(RectRight(bbox));
  int min_cy = GetCellY(RectBottom(bbox));
  int max_cy = GetCellY(RectTop(bbox));

  for (int cy = min_cy; cy <= max_cy; ++cy) {
    for (int cx = min_cx; cx <= max_cx; ++cx) {
      int cell_idx = GetCellIndex(cx, cy);
      cells_[cell_idx].push_back(idx);
    }
  }
}

std::vector<int> SpatialGrid::Query(const CFX_FloatRect& rect) const {
  std::unordered_set<int> result_set;

  int min_cx = GetCellX(RectLeft(rect));
  int max_cx = GetCellX(RectRight(rect));
  int min_cy = GetCellY(RectBottom(rect));
  int max_cy = GetCellY(RectTop(rect));

  for (int cy = min_cy; cy <= max_cy; ++cy) {
    for (int cx = min_cx; cx <= max_cx; ++cx) {
      int cell_idx = GetCellIndex(cx, cy);
      for (int item_idx : cells_[cell_idx]) {
        result_set.insert(item_idx);
      }
    }
  }

  return std::vector<int>(result_set.begin(), result_set.end());
}

std::vector<int> SpatialGrid::QueryNeighborsRight(const CFX_FloatRect& rect,
                                                   float h_margin,
                                                   float v_margin) const {
  CFX_FloatRect query;
  query.left = RectRight(rect);
  query.right = RectRight(rect) + h_margin;
  query.bottom = RectBottom(rect) - v_margin;
  query.top = RectTop(rect) + v_margin;
  return Query(query);
}

std::vector<int> SpatialGrid::QueryNeighborsBelow(const CFX_FloatRect& rect,
                                                   float v_margin,
                                                   float h_margin) const {
  // In PDF coords, "below" means lower Y values
  CFX_FloatRect query = MakeRectBelow(rect, v_margin);
  query.left = RectLeft(rect) - h_margin;
  query.right = RectRight(rect) + h_margin;
  return Query(query);
}

void SpatialGrid::Clear() {
  for (auto& cell : cells_) {
    cell.clear();
  }
}

}  // namespace layout
}  // namespace pdfium
