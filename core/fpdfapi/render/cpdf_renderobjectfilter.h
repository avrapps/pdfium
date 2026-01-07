// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFAPI_RENDER_CPDF_RENDEROBJECTFILTER_H_
#define CORE_FPDFAPI_RENDER_CPDF_RENDEROBJECTFILTER_H_

#include <stdint.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/fxcrt/fx_coordinates.h"

class CPDF_FormObject;
class CPDF_PageObject;
class CPDF_PageObjectHolder;

namespace pdfium {
namespace render {

// Flat ID assigned during preorder traversal of page objects
using ObjectId = uint32_t;
constexpr ObjectId kInvalidObjectId = UINT32_MAX;

// Entry in the ObjectTable mapping ObjectId to object and transform
struct ObjectEntry {
  // Pointer to the page object (const to avoid accidental mutation)
  const CPDF_PageObject* obj = nullptr;

  // Matrix from object's HOLDER space to root page space.
  // For direct page objects: identity matrix
  // For form children: form_matrix * parent_holder_to_root
  // Note: object's internal matrix is handled by RenderSingleObject
  CFX_Matrix holder_to_root_matrix;
};

// Table mapping between ObjectId and CPDF_PageObject pointers.
// Built once during detection, valid until page object tree changes.
//
// VALIDITY CONTRACT:
// ObjectIds and pointer mappings are valid only until the page's object tree
// changes. Any modification (add/remove/reorder objects) requires:
// 1. Call EPDFPage_InvalidateTextBlocks()
// 2. Re-detect via EPDFPage_DetectTextBlocks() (rebuilds ObjectTable)
class ObjectTable {
 public:
  ObjectTable();
  ~ObjectTable();

  // Build the table by recursively traversing the page object tree.
  // This assigns ObjectIds in preorder traversal order.
  void Build(const CPDF_PageObjectHolder* root);

  // Clear the table
  void Clear();

  // Look up ObjectId for a given page object pointer.
  // Returns kInvalidObjectId if not found.
  ObjectId GetId(const CPDF_PageObject* obj) const;

  // Look up entry for a given ObjectId.
  // Returns nullptr if id is out of range.
  const ObjectEntry* GetEntry(ObjectId id) const;

  // Number of entries in the table
  size_t size() const { return entries_.size(); }

  // Check if table has been built
  bool empty() const { return entries_.empty(); }

 private:
  // Recursive helper to build the table
  void BuildRecursive(const CPDF_PageObjectHolder* holder,
                      const CFX_Matrix& holder_to_root);

  // ObjectId -> ObjectEntry (index is ObjectId)
  std::vector<ObjectEntry> entries_;

  // CPDF_PageObject* -> ObjectId for fast lookup from CharInfo
  std::unordered_map<const CPDF_PageObject*, ObjectId> obj_to_id_;
};

// Abstract base class for filtering page objects during rendering.
// Passed as const pointer through the render call stack.
class CPDF_RenderObjectFilter {
 public:
  virtual ~CPDF_RenderObjectFilter() = default;

  // Returns true if the object with given id should be rendered.
  // Called during object list iteration with the ObjectId looked up from table.
  virtual bool ShouldRender(ObjectId id, const CPDF_PageObject* obj) const = 0;
};

// Filter that excludes specific ObjectIds (for background rendering).
// Uses unordered_set for O(1) lookup.
class ExcludeIdsFilter final : public CPDF_RenderObjectFilter {
 public:
  // Takes reference to avoid copying. Caller must ensure set outlives filter.
  explicit ExcludeIdsFilter(const std::unordered_set<ObjectId>& excluded);
  ~ExcludeIdsFilter() override;

  bool ShouldRender(ObjectId id, const CPDF_PageObject* obj) const override;

 private:
  const std::unordered_set<ObjectId>& excluded_;
};

// Filter that includes only specific ObjectIds (for block rendering).
// Uses sorted vector + binary_search for O(log N) lookup.
class IncludeOnlyIdsFilter final : public CPDF_RenderObjectFilter {
 public:
  // Takes reference to sorted vector. Caller must ensure vector outlives filter.
  explicit IncludeOnlyIdsFilter(const std::vector<ObjectId>& included);
  ~IncludeOnlyIdsFilter() override;

  bool ShouldRender(ObjectId id, const CPDF_PageObject* obj) const override;

 private:
  const std::vector<ObjectId>& included_;
};

}  // namespace render
}  // namespace pdfium

#endif  // CORE_FPDFAPI_RENDER_CPDF_RENDEROBJECTFILTER_H_
