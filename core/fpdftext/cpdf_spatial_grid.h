// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFTEXT_CPDF_SPATIAL_GRID_H_
#define CORE_FPDFTEXT_CPDF_SPATIAL_GRID_H_

#include <algorithm>
#include <vector>

#include "core/fpdftext/cpdf_layout_geometry.h"
#include "core/fxcrt/fx_coordinates.h"

namespace pdfium {
namespace layout {

// A uniform grid spatial index for fast neighbor queries.
// Used for O(1) average-case spatial lookups during word/line building.
class SpatialGrid {
 public:
  // Construct a grid covering the given bounds with the specified cell size.
  // Cell size should typically be 2x the median glyph height.
  SpatialGrid(const CFX_FloatRect& bounds, float cell_size);
  ~SpatialGrid();

  // Insert an item at the given bounding box. The item is represented by
  // its index into an external vector.
  void Insert(int idx, const CFX_FloatRect& bbox);

  // Query all items whose bounding boxes intersect with the given rect.
  std::vector<int> Query(const CFX_FloatRect& rect) const;

  // Query items that could be neighbors to the right of the given rect.
  // This creates a query window: [rect.right, rect.bottom - v_margin,
  //                               rect.right + h_margin, rect.top + v_margin]
  std::vector<int> QueryNeighborsRight(const CFX_FloatRect& rect,
                                        float h_margin,
                                        float v_margin) const;

  // Query items that could be neighbors below the given rect.
  std::vector<int> QueryNeighborsBelow(const CFX_FloatRect& rect,
                                        float v_margin,
                                        float h_margin) const;

  // Clear all items from the grid
  void Clear();

  // Get grid dimensions
  int GetCols() const { return cols_; }
  int GetRows() const { return rows_; }
  float GetCellSize() const { return cell_size_; }

 private:
  // Convert world coordinate to cell index
  int GetCellX(float x) const;
  int GetCellY(float y) const;

  // Get linear cell index from grid coordinates
  int GetCellIndex(int cx, int cy) const;

  float cell_size_;
  int cols_;
  int rows_;
  float x_origin_;
  float y_origin_;

  // Each cell contains indices of items that intersect it
  std::vector<std::vector<int>> cells_;
};

}  // namespace layout
}  // namespace pdfium

#endif  // CORE_FPDFTEXT_CPDF_SPATIAL_GRID_H_
