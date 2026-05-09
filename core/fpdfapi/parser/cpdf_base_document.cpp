// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/parser/cpdf_base_document.h"

#include <memory>
#include <queue>
#include <set>
#include <utility>

#include "core/fpdfapi/page/cpdf_docpagedata.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_object.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/render/cpdf_docrenderdata.h"

namespace {

void PushIfNew(RetainPtr<const CPDF_Object> object,
               std::set<const CPDF_Object*>* visited,
               std::queue<RetainPtr<const CPDF_Object>>* worklist) {
  if (!object || !visited->insert(object.Get()).second) {
    return;
  }
  worklist->push(std::move(object));
}

}  // namespace

CPDF_BaseDocument::CPDF_BaseDocument()
    : CPDF_Document(std::make_unique<CPDF_DocRenderData>(),
                    std::make_unique<CPDF_DocPageData>()) {}

CPDF_BaseDocument::~CPDF_BaseDocument() = default;

CPDF_Parser::Error CPDF_BaseDocument::LoadBaseDoc(
    RetainPtr<IFX_SeekableReadStream> file_access,
    const ByteString& password) {
  CPDF_Parser::Error error = LoadDoc(std::move(file_access), password);
  if (error != CPDF_Parser::SUCCESS) {
    return error;
  }
  return EagerlyParseAllReachable() ? CPDF_Parser::SUCCESS
                                    : CPDF_Parser::FORMAT_ERROR;
}

bool CPDF_BaseDocument::EagerlyParseAllReachable() {
  if (!GetParser() || !GetRoot()) {
    return false;
  }

  std::set<const CPDF_Object*> visited;
  std::queue<RetainPtr<const CPDF_Object>> worklist;
  PushIfNew(pdfium::WrapRetain(GetParser()->GetTrailer()), &visited, &worklist);
  PushIfNew(pdfium::WrapRetain(GetRoot()), &visited, &worklist);
  PushIfNew(GetInfo(), &visited, &worklist);
  PushIfNew(GetParser()->GetEncryptDict(), &visited, &worklist);

  while (!worklist.empty()) {
    RetainPtr<const CPDF_Object> object = worklist.front();
    worklist.pop();

    switch (object->GetType()) {
      case CPDF_Object::kReference: {
        const uint32_t ref_objnum = object->AsReference()->GetRefObjNum();
        PushIfNew(GetOrParseIndirectObject(ref_objnum), &visited, &worklist);
        break;
      }
      case CPDF_Object::kArray: {
        CPDF_ArrayLocker locker(object->AsArray());
        for (const auto& child : locker) {
          PushIfNew(child, &visited, &worklist);
        }
        break;
      }
      case CPDF_Object::kDictionary: {
        CPDF_DictionaryLocker locker(object->AsDictionary());
        for (const auto& child : locker) {
          PushIfNew(child.second, &visited, &worklist);
        }
        break;
      }
      case CPDF_Object::kStream: {
        PushIfNew(object->AsStream()->GetDict(), &visited, &worklist);
        break;
      }
      default:
        break;
    }
  }

  Freeze();
  return true;
}

RetainPtr<const CPDF_Object> CPDF_BaseDocument::GetFrozenObjectForLayer(
    uint32_t objnum) const {
  return GetIndirectObject(objnum);
}
