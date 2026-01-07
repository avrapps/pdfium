// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/render/cpdf_renderobjectfilter.h"

#include <algorithm>

#include "core/fpdfapi/page/cpdf_form.h"
#include "core/fpdfapi/page/cpdf_formobject.h"
#include "core/fpdfapi/page/cpdf_pageobject.h"
#include "core/fpdfapi/page/cpdf_pageobjectholder.h"

namespace pdfium {
namespace render {

// ObjectTable implementation

ObjectTable::ObjectTable() = default;

ObjectTable::~ObjectTable() = default;

void ObjectTable::Build(const CPDF_PageObjectHolder* root) {
  Clear();
  if (!root) {
    return;
  }
  // Start with identity matrix for root page space
  BuildRecursive(root, CFX_Matrix());
}

void ObjectTable::Clear() {
  entries_.clear();
  obj_to_id_.clear();
}

ObjectId ObjectTable::GetId(const CPDF_PageObject* obj) const {
  auto it = obj_to_id_.find(obj);
  return it != obj_to_id_.end() ? it->second : kInvalidObjectId;
}

const ObjectEntry* ObjectTable::GetEntry(ObjectId id) const {
  if (id >= entries_.size()) {
    return nullptr;
  }
  return &entries_[id];
}

void ObjectTable::BuildRecursive(const CPDF_PageObjectHolder* holder,
                                 const CFX_Matrix& holder_to_root) {
  if (!holder) {
    return;
  }

  for (auto it = holder->begin(); it != holder->end(); ++it) {
    const CPDF_PageObject* obj = it->get();
    if (!obj) {
      continue;
    }

    // Assign ObjectId as current size (preorder traversal)
    ObjectId id = static_cast<ObjectId>(entries_.size());

    ObjectEntry entry;
    entry.obj = obj;
    entry.holder_to_root_matrix = holder_to_root;
    entries_.push_back(entry);
    obj_to_id_[obj] = id;

    // Recursively process Form XObjects
    if (obj->IsForm()) {
      const CPDF_FormObject* form_obj = obj->AsForm();
      if (form_obj && form_obj->form()) {
        // Match ProcessForm matrix concatenation order:
        // child_holder_to_root = form_matrix * parent_holder_to_root
        // This matches: CFX_Matrix matrix = pFormObj->form_matrix() * mtObj2Device;
        CFX_Matrix child_holder_to_root = form_obj->form_matrix() * holder_to_root;
        BuildRecursive(form_obj->form(), child_holder_to_root);
      }
    }
  }
}

// ExcludeIdsFilter implementation

ExcludeIdsFilter::ExcludeIdsFilter(const std::unordered_set<ObjectId>& excluded)
    : excluded_(excluded) {}

ExcludeIdsFilter::~ExcludeIdsFilter() = default;

bool ExcludeIdsFilter::ShouldRender(ObjectId id,
                                    const CPDF_PageObject* /*obj*/) const {
  // Render if not in excluded set
  if (id == kInvalidObjectId) {
    return true;  // Unknown objects always render (safety)
  }
  return excluded_.find(id) == excluded_.end();
}

// IncludeOnlyIdsFilter implementation

IncludeOnlyIdsFilter::IncludeOnlyIdsFilter(const std::vector<ObjectId>& included)
    : included_(included) {}

IncludeOnlyIdsFilter::~IncludeOnlyIdsFilter() = default;

bool IncludeOnlyIdsFilter::ShouldRender(ObjectId id,
                                        const CPDF_PageObject* /*obj*/) const {
  // Only render if in included list (must be sorted for binary_search)
  if (id == kInvalidObjectId) {
    return false;  // Unknown objects never render in include-only mode
  }
  return std::binary_search(included_.begin(), included_.end(), id);
}

}  // namespace render
}  // namespace pdfium
