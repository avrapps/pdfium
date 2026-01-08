// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFTEXT_CPDF_TABLE_DETECTOR_H_
#define CORE_FPDFTEXT_CPDF_TABLE_DETECTOR_H_

#include <map>
#include <vector>

#include "core/fpdftext/cpdf_layout_types.h"

class CPDF_Page;
class CPDF_PageObject;
class CPDF_PathObject;

namespace pdfium {
namespace layout {

// Detects ruled tables from path objects on a PDF page.
// This handles Phase 0.5 (provisional zones) and Phase 4 (full detection).
class TableDetector {
 public:
  TableDetector();
  ~TableDetector();

  // Phase 0.5: Quick extraction of provisional table zones for exclusion
  // from column detection. Uses strict criteria to avoid false positives.
  std::vector<ProvisionalTableZone> ExtractProvisionalZones(
      CPDF_Page* page,
      const AdaptiveParams& params);

  // Phase 4: Full ruled table detection with cell extraction.
  std::vector<Table> DetectRuledTables(CPDF_Page* page,
                                        const AdaptiveParams& params);

  // Phase 4 (opt-in): Unruled table detection based on grid alignment.
  // This has higher false-positive risk and should be used cautiously.
  std::vector<Table> DetectUnruledTables(
      const std::vector<WordItem>& words,
      const std::vector<LineItem>& lines,
      const ColumnModel& columns,
      const AdaptiveParams& params);

  // Get extracted ruling edges (available after either detection method)
  const std::vector<RulingEdge>& GetEdges() const { return edges_; }

 private:
  // Extract ruling edges from page path objects
  void ExtractRulingEdges(CPDF_Page* page, const AdaptiveParams& params);

  // Check if a path object represents a ruling line
  bool IsRulingLine(CPDF_PathObject* path,
                    const AdaptiveParams& params,
                    RulingEdge* out_edge);

  // Snap edges to grid positions
  void SnapEdges(float tolerance);

  // Join collinear edges that are close together
  void JoinEdges(float tolerance);

  // Find intersections between horizontal and vertical edges
  void FindIntersections(float tolerance);

  // Build cells from intersection points
  std::vector<TableCell> BuildCells();

  // Group cells into tables
  std::vector<Table> GroupCellsIntoTables(const std::vector<TableCell>& cells);

  // Check if two edges are parallel and close (for provisional detection)
  bool HasParallelEdges(const std::vector<RulingEdge>& edges,
                        float tolerance) const;

  // Cluster edges by proximity
  struct EdgeCluster {
    std::vector<int> h_edge_indices;
    std::vector<int> v_edge_indices;
    CFX_FloatRect bounds;
  };

  std::vector<EdgeCluster> ClusterEdgesByProximity(float tolerance);

  std::vector<RulingEdge> edges_;
  std::vector<RulingEdge> h_edges_;
  std::vector<RulingEdge> v_edges_;

  // Intersection map using quantized points
  std::map<QuantizedPoint, std::vector<int>> intersections_;
  float intersection_tol_ = 3.0f;
};

}  // namespace layout
}  // namespace pdfium

#endif  // CORE_FPDFTEXT_CPDF_TABLE_DETECTOR_H_
