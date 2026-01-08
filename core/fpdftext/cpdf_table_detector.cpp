// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdftext/cpdf_table_detector.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/page/cpdf_pageobject.h"
#include "core/fpdfapi/page/cpdf_pathobject.h"

namespace pdfium {
namespace layout {

namespace {

constexpr float kMinEdgeLength = 20.0f;
constexpr float kMaxEdgeThickness = 5.0f;

}  // namespace

TableDetector::TableDetector() = default;

TableDetector::~TableDetector() = default;

bool TableDetector::IsRulingLine(CPDF_PathObject* path,
                                  const AdaptiveParams& params,
                                  RulingEdge* out_edge) {
  if (!path) {
    return false;
  }

  CFX_FloatRect rect = path->GetRect();
  float width = RectWidth(rect);
  float height = RectHeight(rect);

  // Horizontal line: wide and thin
  if (width > kMinEdgeLength && height < kMaxEdgeThickness) {
    out_edge->bbox = rect;
    out_edge->orientation = 'h';
    out_edge->thickness = height;
    return true;
  }

  // Vertical line: tall and thin
  if (height > kMinEdgeLength && width < kMaxEdgeThickness) {
    out_edge->bbox = rect;
    out_edge->orientation = 'v';
    out_edge->thickness = width;
    return true;
  }

  return false;
}

void TableDetector::ExtractRulingEdges(CPDF_Page* page,
                                        const AdaptiveParams& params) {
  edges_.clear();
  h_edges_.clear();
  v_edges_.clear();

  if (!page) {
    return;
  }

  for (auto it = page->begin(); it != page->end(); ++it) {
    CPDF_PageObject* obj = it->get();
    if (!obj || !obj->IsPath()) {
      continue;
    }

    CPDF_PathObject* path = obj->AsPath();
    RulingEdge edge;
    if (IsRulingLine(path, params, &edge)) {
      edges_.push_back(edge);
      if (edge.is_horizontal()) {
        h_edges_.push_back(edge);
      } else {
        v_edges_.push_back(edge);
      }
    }
  }
}

void TableDetector::SnapEdges(float tolerance) {
  // Snap horizontal edges to common Y values
  if (!h_edges_.empty()) {
    // Group by similar Y
    std::vector<float> y_values;
    for (const auto& e : h_edges_) {
      y_values.push_back(RectCenterY(e.bbox));
    }
    std::sort(y_values.begin(), y_values.end());

    // Cluster and snap
    std::vector<float> snap_targets;
    float cluster_sum = y_values[0];
    int cluster_count = 1;

    for (size_t i = 1; i < y_values.size(); ++i) {
      if (y_values[i] - y_values[i - 1] <= tolerance) {
        cluster_sum += y_values[i];
        cluster_count++;
      } else {
        snap_targets.push_back(cluster_sum / cluster_count);
        cluster_sum = y_values[i];
        cluster_count = 1;
      }
    }
    snap_targets.push_back(cluster_sum / cluster_count);

    // Apply snapping
    for (auto& e : h_edges_) {
      float y = RectCenterY(e.bbox);
      float best_target = y;
      float best_dist = tolerance + 1;
      for (float t : snap_targets) {
        float dist = std::abs(y - t);
        if (dist < best_dist) {
          best_dist = dist;
          best_target = t;
        }
      }
      if (best_dist <= tolerance) {
        float half_h = RectHeight(e.bbox) / 2.0f;
        e.bbox.top = best_target + half_h;
        e.bbox.bottom = best_target - half_h;
      }
    }
  }

  // Snap vertical edges to common X values
  if (!v_edges_.empty()) {
    std::vector<float> x_values;
    for (const auto& e : v_edges_) {
      x_values.push_back(RectCenterX(e.bbox));
    }
    std::sort(x_values.begin(), x_values.end());

    std::vector<float> snap_targets;
    float cluster_sum = x_values[0];
    int cluster_count = 1;

    for (size_t i = 1; i < x_values.size(); ++i) {
      if (x_values[i] - x_values[i - 1] <= tolerance) {
        cluster_sum += x_values[i];
        cluster_count++;
      } else {
        snap_targets.push_back(cluster_sum / cluster_count);
        cluster_sum = x_values[i];
        cluster_count = 1;
      }
    }
    snap_targets.push_back(cluster_sum / cluster_count);

    for (auto& e : v_edges_) {
      float x = RectCenterX(e.bbox);
      float best_target = x;
      float best_dist = tolerance + 1;
      for (float t : snap_targets) {
        float dist = std::abs(x - t);
        if (dist < best_dist) {
          best_dist = dist;
          best_target = t;
        }
      }
      if (best_dist <= tolerance) {
        float half_w = RectWidth(e.bbox) / 2.0f;
        e.bbox.left = best_target - half_w;
        e.bbox.right = best_target + half_w;
      }
    }
  }
}

void TableDetector::JoinEdges(float tolerance) {
  // Join collinear horizontal edges
  if (h_edges_.size() > 1) {
    std::sort(h_edges_.begin(), h_edges_.end(),
              [](const RulingEdge& a, const RulingEdge& b) {
                float ya = RectCenterY(a.bbox);
                float yb = RectCenterY(b.bbox);
                if (std::abs(ya - yb) > 0.5f)
                  return ya < yb;
                return RectLeft(a.bbox) < RectLeft(b.bbox);
              });

    std::vector<RulingEdge> joined;
    joined.push_back(h_edges_[0]);

    for (size_t i = 1; i < h_edges_.size(); ++i) {
      RulingEdge& last = joined.back();
      const RulingEdge& curr = h_edges_[i];

      float y_diff = std::abs(RectCenterY(last.bbox) - RectCenterY(curr.bbox));
      float gap = RectLeft(curr.bbox) - RectRight(last.bbox);

      if (y_diff <= tolerance && gap <= tolerance) {
        // Extend last edge
        last.bbox.right = std::max(RectRight(last.bbox), RectRight(curr.bbox));
        last.bbox.top = std::max(RectTop(last.bbox), RectTop(curr.bbox));
        last.bbox.bottom = std::min(RectBottom(last.bbox), RectBottom(curr.bbox));
      } else {
        joined.push_back(curr);
      }
    }
    h_edges_ = std::move(joined);
  }

  // Join collinear vertical edges
  if (v_edges_.size() > 1) {
    std::sort(v_edges_.begin(), v_edges_.end(),
              [](const RulingEdge& a, const RulingEdge& b) {
                float xa = RectCenterX(a.bbox);
                float xb = RectCenterX(b.bbox);
                if (std::abs(xa - xb) > 0.5f)
                  return xa < xb;
                return RectBottom(a.bbox) < RectBottom(b.bbox);
              });

    std::vector<RulingEdge> joined;
    joined.push_back(v_edges_[0]);

    for (size_t i = 1; i < v_edges_.size(); ++i) {
      RulingEdge& last = joined.back();
      const RulingEdge& curr = v_edges_[i];

      float x_diff = std::abs(RectCenterX(last.bbox) - RectCenterX(curr.bbox));
      float gap = RectBottom(curr.bbox) - RectTop(last.bbox);

      if (x_diff <= tolerance && gap <= tolerance) {
        // Extend last edge
        last.bbox.top = std::max(RectTop(last.bbox), RectTop(curr.bbox));
        last.bbox.left = std::min(RectLeft(last.bbox), RectLeft(curr.bbox));
        last.bbox.right = std::max(RectRight(last.bbox), RectRight(curr.bbox));
      } else {
        joined.push_back(curr);
      }
    }
    v_edges_ = std::move(joined);
  }
}

void TableDetector::FindIntersections(float tolerance) {
  intersections_.clear();
  intersection_tol_ = tolerance;

  for (size_t hi = 0; hi < h_edges_.size(); ++hi) {
    const RulingEdge& h = h_edges_[hi];
    float h_y = RectCenterY(h.bbox);
    float h_left = RectLeft(h.bbox);
    float h_right = RectRight(h.bbox);

    for (size_t vi = 0; vi < v_edges_.size(); ++vi) {
      const RulingEdge& v = v_edges_[vi];
      float v_x = RectCenterX(v.bbox);
      float v_bottom = RectBottom(v.bbox);
      float v_top = RectTop(v.bbox);

      // Check if they intersect
      if (v_x >= h_left - tolerance && v_x <= h_right + tolerance &&
          h_y >= v_bottom - tolerance && h_y <= v_top + tolerance) {
        QuantizedPoint qp = QuantizedPoint::FromFloat(v_x, h_y, tolerance);
        intersections_[qp].push_back(static_cast<int>(hi));
        intersections_[qp].push_back(
            static_cast<int>(vi + h_edges_.size()));  // Offset for v edges
      }
    }
  }
}

bool TableDetector::HasParallelEdges(const std::vector<RulingEdge>& edges,
                                      float tolerance) const {
  if (edges.size() < 2) {
    return false;
  }

  // Check for at least 2 edges with similar primary coordinate
  std::vector<float> coords;
  for (const auto& e : edges) {
    coords.push_back(e.primary_coord());
  }
  std::sort(coords.begin(), coords.end());

  for (size_t i = 1; i < coords.size(); ++i) {
    // If two edges have significantly different coords, there are parallels
    if (coords[i] - coords[i - 1] > tolerance * 3) {
      return true;
    }
  }
  return false;
}

std::vector<TableDetector::EdgeCluster> TableDetector::ClusterEdgesByProximity(
    float tolerance) {
  std::vector<EdgeCluster> clusters;

  if (edges_.empty()) {
    return clusters;
  }

  // Build a union-find over edges based on spatial proximity
  std::vector<int> parent(edges_.size());
  std::iota(parent.begin(), parent.end(), 0);

  std::function<int(int)> find = [&](int x) {
    if (parent[x] != x)
      parent[x] = find(parent[x]);
    return parent[x];
  };

  auto unite = [&](int x, int y) {
    int px = find(x), py = find(y);
    if (px != py)
      parent[px] = py;
  };

  // Compare all pairs (O(n²) but n is typically small)
  for (size_t i = 0; i < edges_.size(); ++i) {
    for (size_t j = i + 1; j < edges_.size(); ++j) {
      // Check if edges are close enough to be in the same table
      CFX_FloatRect expanded_i = ExpandRect(edges_[i].bbox, tolerance * 2);
      if (RectsIntersect(expanded_i, edges_[j].bbox)) {
        unite(i, j);
      }
    }
  }

  // Group edges by cluster
  std::unordered_map<int, EdgeCluster> cluster_map;
  for (size_t i = 0; i < edges_.size(); ++i) {
    int root = find(i);
    EdgeCluster& cluster = cluster_map[root];

    if (edges_[i].is_horizontal()) {
      cluster.h_edge_indices.push_back(i);
    } else {
      cluster.v_edge_indices.push_back(i);
    }

    if (cluster.bounds.IsEmpty()) {
      cluster.bounds = edges_[i].bbox;
    } else {
      cluster.bounds = UnionRects(cluster.bounds, edges_[i].bbox);
    }
  }

  for (auto& [_, cluster] : cluster_map) {
    clusters.push_back(std::move(cluster));
  }

  return clusters;
}

std::vector<ProvisionalTableZone> TableDetector::ExtractProvisionalZones(
    CPDF_Page* page,
    const AdaptiveParams& params) {
  ExtractRulingEdges(page, params);

  if (edges_.empty()) {
    return {};
  }

  SnapEdges(params.edge_snap_tol);
  JoinEdges(params.edge_join_tol);

  // Cluster edges by proximity
  auto clusters = ClusterEdgesByProximity(params.edge_join_tol * 2);

  std::vector<ProvisionalTableZone> zones;

  for (const auto& cluster : clusters) {
    ProvisionalTableZone zone;
    zone.bbox = cluster.bounds;
    zone.h_edge_count = static_cast<int>(cluster.h_edge_indices.size());
    zone.v_edge_count = static_cast<int>(cluster.v_edge_indices.size());

    // Count intersections for this cluster
    int intersection_count = 0;
    for (int hi : cluster.h_edge_indices) {
      for (int vi : cluster.v_edge_indices) {
        const RulingEdge& h = edges_[hi];
        const RulingEdge& v = edges_[vi];

        float h_y = RectCenterY(h.bbox);
        float h_left = RectLeft(h.bbox);
        float h_right = RectRight(h.bbox);
        float v_x = RectCenterX(v.bbox);
        float v_bottom = RectBottom(v.bbox);
        float v_top = RectTop(v.bbox);

        if (v_x >= h_left - params.intersection_tol &&
            v_x <= h_right + params.intersection_tol &&
            h_y >= v_bottom - params.intersection_tol &&
            h_y <= v_top + params.intersection_tol) {
          intersection_count++;
        }
      }
    }
    zone.intersection_count = intersection_count;

    // Apply strict criteria
    bool has_both = zone.h_edge_count >= 2 && zone.v_edge_count >= 2;
    bool enough_edges =
        (zone.h_edge_count + zone.v_edge_count) >= params.min_table_edges;
    bool has_internal = zone.intersection_count >= 4;
    float min_dim = std::min(RectWidth(zone.bbox), RectHeight(zone.bbox));
    bool sufficient_area = min_dim > params.median_height * 3;

    // Build edge vectors for parallel check
    std::vector<RulingEdge> cluster_h_edges, cluster_v_edges;
    for (int hi : cluster.h_edge_indices) {
      cluster_h_edges.push_back(edges_[hi]);
    }
    for (int vi : cluster.v_edge_indices) {
      cluster_v_edges.push_back(edges_[vi]);
    }

    bool has_parallel_h = HasParallelEdges(cluster_h_edges, params.edge_snap_tol);
    bool has_parallel_v = HasParallelEdges(cluster_v_edges, params.edge_snap_tol);

    if (has_both && enough_edges && has_internal && sufficient_area) {
      zone.confidence = 0.9f;
      zones.push_back(zone);
    } else if (has_both && (has_parallel_h || has_parallel_v)) {
      zone.confidence = 0.5f;
      zones.push_back(zone);
    }
  }

  return zones;
}

std::vector<TableCell> TableDetector::BuildCells() {
  std::vector<TableCell> cells;

  // Get sorted intersection points
  std::vector<QuantizedPoint> points;
  for (const auto& [pt, _] : intersections_) {
    points.push_back(pt);
  }
  std::sort(points.begin(), points.end());

  // For each intersection, find the smallest cell it forms the top-left of
  for (size_t i = 0; i < points.size(); ++i) {
    const QuantizedPoint& pt = points[i];

    // Find points to the right on the same row
    std::vector<QuantizedPoint> right_pts;
    for (size_t j = i + 1; j < points.size(); ++j) {
      if (points[j].qy == pt.qy && points[j].qx > pt.qx) {
        right_pts.push_back(points[j]);
      }
    }

    // Find points below in the same column
    std::vector<QuantizedPoint> below_pts;
    for (size_t j = i + 1; j < points.size(); ++j) {
      if (points[j].qx == pt.qx && points[j].qy < pt.qy) {
        below_pts.push_back(points[j]);
      }
    }

    // Try to form cells
    for (const auto& right_pt : right_pts) {
      for (const auto& below_pt : below_pts) {
        // Check if bottom-right corner exists
        QuantizedPoint bottom_right;
        bottom_right.qx = right_pt.qx;
        bottom_right.qy = below_pt.qy;

        if (intersections_.count(bottom_right)) {
          // We have a cell!
          TableCell cell;
          cell.bbox.left = pt.ToFloatX(intersection_tol_);
          cell.bbox.top = pt.ToFloatY(intersection_tol_);
          cell.bbox.right = right_pt.ToFloatX(intersection_tol_);
          cell.bbox.bottom = below_pt.ToFloatY(intersection_tol_);
          cells.push_back(cell);
          break;  // Only smallest cell from this point
        }
      }
      if (!cells.empty() && cells.back().bbox.left == pt.ToFloatX(intersection_tol_)) {
        break;  // Found a cell, stop looking
      }
    }
  }

  return cells;
}

std::vector<Table> TableDetector::GroupCellsIntoTables(
    const std::vector<TableCell>& cells) {
  if (cells.empty()) {
    return {};
  }

  // Union-find to group cells that share corners
  std::vector<int> parent(cells.size());
  std::iota(parent.begin(), parent.end(), 0);

  std::function<int(int)> find = [&](int x) {
    if (parent[x] != x)
      parent[x] = find(parent[x]);
    return parent[x];
  };

  auto unite = [&](int x, int y) {
    int px = find(x), py = find(y);
    if (px != py)
      parent[px] = py;
  };

  // Group cells that share corners
  for (size_t i = 0; i < cells.size(); ++i) {
    for (size_t j = i + 1; j < cells.size(); ++j) {
      // Check if cells share any corner
      const auto& c1 = cells[i].bbox;
      const auto& c2 = cells[j].bbox;

      auto corners_match = [&](float x1, float y1, float x2, float y2) {
        return std::abs(x1 - x2) < intersection_tol_ &&
               std::abs(y1 - y2) < intersection_tol_;
      };

      bool share_corner = false;
      // Check all 4 corners of c1 against all 4 corners of c2
      float c1_corners[4][2] = {{c1.left, c1.top},
                                 {c1.right, c1.top},
                                 {c1.left, c1.bottom},
                                 {c1.right, c1.bottom}};
      float c2_corners[4][2] = {{c2.left, c2.top},
                                 {c2.right, c2.top},
                                 {c2.left, c2.bottom},
                                 {c2.right, c2.bottom}};

      for (int ci = 0; ci < 4 && !share_corner; ++ci) {
        for (int cj = 0; cj < 4 && !share_corner; ++cj) {
          if (corners_match(c1_corners[ci][0], c1_corners[ci][1],
                            c2_corners[cj][0], c2_corners[cj][1])) {
            share_corner = true;
          }
        }
      }

      if (share_corner) {
        unite(i, j);
      }
    }
  }

  // Build tables from groups
  std::unordered_map<int, std::vector<int>> groups;
  for (size_t i = 0; i < cells.size(); ++i) {
    groups[find(i)].push_back(i);
  }

  std::vector<Table> tables;
  int table_id = 0;

  for (auto& [_, cell_indices] : groups) {
    if (cell_indices.size() < 2) {
      continue;  // Need at least 2 cells
    }

    Table table;
    table.id = table_id++;

    for (int ci : cell_indices) {
      table.cells.push_back(cells[ci]);
      if (table.bbox.IsEmpty()) {
        table.bbox = cells[ci].bbox;
      } else {
        table.bbox = UnionRects(table.bbox, cells[ci].bbox);
      }
    }

    // Compute row/col counts from unique Y/X positions
    std::set<float> unique_tops, unique_lefts;
    for (const auto& cell : table.cells) {
      unique_tops.insert(std::round(RectTop(cell.bbox) / intersection_tol_) *
                         intersection_tol_);
      unique_lefts.insert(std::round(RectLeft(cell.bbox) / intersection_tol_) *
                          intersection_tol_);
    }
    table.row_count = static_cast<int>(unique_tops.size());
    table.col_count = static_cast<int>(unique_lefts.size());

    // Assign row/col indices to cells
    std::vector<float> sorted_tops(unique_tops.begin(), unique_tops.end());
    std::vector<float> sorted_lefts(unique_lefts.begin(), unique_lefts.end());
    std::sort(sorted_tops.rbegin(), sorted_tops.rend());  // Descending (top first)
    std::sort(sorted_lefts.begin(), sorted_lefts.end());  // Ascending

    for (auto& cell : table.cells) {
      float cell_top =
          std::round(RectTop(cell.bbox) / intersection_tol_) * intersection_tol_;
      float cell_left =
          std::round(RectLeft(cell.bbox) / intersection_tol_) * intersection_tol_;

      auto top_it = std::find(sorted_tops.begin(), sorted_tops.end(), cell_top);
      auto left_it = std::find(sorted_lefts.begin(), sorted_lefts.end(), cell_left);

      cell.row = static_cast<int>(std::distance(sorted_tops.begin(), top_it));
      cell.col = static_cast<int>(std::distance(sorted_lefts.begin(), left_it));
    }

    tables.push_back(std::move(table));
  }

  // Sort tables by position (top to bottom, left to right)
  std::sort(tables.begin(), tables.end(), [](const Table& a, const Table& b) {
    if (std::abs(RectTop(a.bbox) - RectTop(b.bbox)) > 1.0f) {
      return RectTop(a.bbox) > RectTop(b.bbox);  // Higher Y first
    }
    return RectLeft(a.bbox) < RectLeft(b.bbox);
  });

  return tables;
}

std::vector<Table> TableDetector::DetectRuledTables(
    CPDF_Page* page,
    const AdaptiveParams& params) {
  ExtractRulingEdges(page, params);

  if (h_edges_.empty() || v_edges_.empty()) {
    return {};
  }

  SnapEdges(params.edge_snap_tol);
  JoinEdges(params.edge_join_tol);
  FindIntersections(params.intersection_tol);

  if (intersections_.size() < 4) {
    return {};  // Need at least 4 intersections for a table
  }

  auto cells = BuildCells();
  if (cells.size() < static_cast<size_t>(params.min_table_cells)) {
    return {};
  }

  auto tables = GroupCellsIntoTables(cells);

  // Filter tables that don't meet minimum requirements
  tables.erase(
      std::remove_if(tables.begin(), tables.end(),
                     [&](const Table& t) {
                       return t.cells.size() <
                              static_cast<size_t>(params.min_table_cells);
                     }),
      tables.end());

  return tables;
}

std::vector<Table> TableDetector::DetectUnruledTables(
    const std::vector<WordItem>& words,
    const std::vector<LineItem>& lines,
    const ColumnModel& columns,
    const AdaptiveParams& params) {
  std::vector<Table> tables;

  // Process each column region separately
  for (size_t col_idx = 0; col_idx < columns.column_bounds.size(); ++col_idx) {
    const auto& col = columns.column_bounds[col_idx];

    // Get lines in this column
    std::vector<int> region_line_indices;
    for (size_t li = 0; li < lines.size(); ++li) {
      const LineItem& line = lines[li];
      if (line.is_empty()) {
        continue;
      }
      float overlap = HorizontalOverlap(line.bbox, col);
      float ratio =
          RectWidth(line.bbox) > 0 ? overlap / RectWidth(line.bbox) : 0;
      if (ratio > 0.5f) {
        region_line_indices.push_back(static_cast<int>(li));
      }
    }

    if (region_line_indices.size() < 3) {
      continue;  // Need at least 3 lines
    }

    // Cluster word X positions (left, right, center)
    std::vector<float> x_positions;
    for (int li : region_line_indices) {
      for (int wi : lines[li].word_indices) {
        if (wi >= 0 && static_cast<size_t>(wi) < words.size()) {
          x_positions.push_back(RectLeft(words[wi].bbox));
          x_positions.push_back(RectRight(words[wi].bbox));
          x_positions.push_back(RectCenterX(words[wi].bbox));
        }
      }
    }

    if (x_positions.size() < 6) {
      continue;
    }

    // Cluster X positions
    std::sort(x_positions.begin(), x_positions.end());

    std::vector<std::vector<float>> x_clusters;
    std::vector<float> current_cluster;
    current_cluster.push_back(x_positions[0]);

    for (size_t i = 1; i < x_positions.size(); ++i) {
      if (x_positions[i] - x_positions[i - 1] < params.median_width) {
        current_cluster.push_back(x_positions[i]);
      } else {
        if (current_cluster.size() >= 3) {
          x_clusters.push_back(current_cluster);
        }
        current_cluster.clear();
        current_cluster.push_back(x_positions[i]);
      }
    }
    if (current_cluster.size() >= 3) {
      x_clusters.push_back(current_cluster);
    }

    // Need at least 2 consistent vertical alignments
    if (x_clusters.size() < 2) {
      continue;
    }

    // Get representative X for each cluster
    std::vector<float> cluster_x;
    for (const auto& cluster : x_clusters) {
      float sum = 0;
      for (float x : cluster) {
        sum += x;
      }
      cluster_x.push_back(sum / cluster.size());
    }

    // Validate grid pattern: check that most lines have words at multiple
    // cluster positions
    int grid_lines = 0;
    for (int li : region_line_indices) {
      int clusters_hit = 0;
      for (float cx : cluster_x) {
        for (int wi : lines[li].word_indices) {
          if (wi >= 0 && static_cast<size_t>(wi) < words.size()) {
            if (std::abs(RectLeft(words[wi].bbox) - cx) < params.median_width ||
                std::abs(RectRight(words[wi].bbox) - cx) < params.median_width ||
                std::abs(RectCenterX(words[wi].bbox) - cx) <
                    params.median_width) {
              clusters_hit++;
              break;
            }
          }
        }
      }
      if (clusters_hit >= 2) {
        grid_lines++;
      }
    }

    // Require majority of lines to be grid-like
    float grid_ratio = static_cast<float>(grid_lines) / region_line_indices.size();
    if (grid_ratio < 0.6f) {
      continue;
    }

    // Check cells don't contain paragraph-like text (long text in single cell)
    bool has_long_text = false;
    for (int li : region_line_indices) {
      for (int wi : lines[li].word_indices) {
        if (wi >= 0 && static_cast<size_t>(wi) < words.size()) {
          // Long word (> 5 chars) that spans multiple clusters
          if (words[wi].text.GetLength() > 20) {
            has_long_text = true;
            break;
          }
        }
      }
      if (has_long_text) {
        break;
      }
    }
    if (has_long_text) {
      continue;
    }

    // Build table from grid pattern
    Table table;
    table.id = static_cast<int>(tables.size());

    // Compute table bounds
    table.bbox.left = 1e9f;
    table.bbox.bottom = 1e9f;
    table.bbox.right = -1e9f;
    table.bbox.top = -1e9f;
    for (int li : region_line_indices) {
      table.bbox = UnionRects(table.bbox, lines[li].bbox);
    }

    // Create cells based on clusters and lines
    table.row_count = static_cast<int>(region_line_indices.size());
    table.col_count = static_cast<int>(cluster_x.size());

    // Sort lines by Y (top to bottom)
    std::vector<int> sorted_lines = region_line_indices;
    std::stable_sort(sorted_lines.begin(), sorted_lines.end(),
                     [&](int a, int b) {
                       return RectTop(lines[a].bbox) > RectTop(lines[b].bbox);
                     });

    for (size_t row = 0; row < sorted_lines.size(); ++row) {
      int li = sorted_lines[row];
      const LineItem& line = lines[li];

      for (size_t col = 0; col < cluster_x.size(); ++col) {
        float cx = cluster_x[col];
        float next_cx = (col + 1 < cluster_x.size())
                            ? cluster_x[col + 1]
                            : RectRight(col_idx < columns.column_bounds.size()
                                             ? columns.column_bounds[col_idx]
                                             : table.bbox);

        TableCell cell;
        cell.row = static_cast<int>(row);
        cell.col = static_cast<int>(col);
        cell.bbox.left = cx - params.median_width;
        cell.bbox.right = next_cx;
        cell.bbox.top = RectTop(line.bbox);
        cell.bbox.bottom = RectBottom(line.bbox);

        // Find words in this cell
        for (int wi : line.word_indices) {
          if (wi >= 0 && static_cast<size_t>(wi) < words.size()) {
            float word_cx = RectCenterX(words[wi].bbox);
            if (word_cx >= cell.bbox.left && word_cx < cell.bbox.right) {
              cell.word_indices.push_back(wi);
            }
          }
        }

        cell.line_indices.push_back(li);
        table.cells.push_back(cell);
      }
    }

    if (table.row_count >= 3 && table.col_count >= 2) {
      tables.push_back(std::move(table));
    }
  }

  return tables;
}

}  // namespace layout
}  // namespace pdfium
