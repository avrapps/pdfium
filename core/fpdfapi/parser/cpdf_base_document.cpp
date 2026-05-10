// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/parser/cpdf_base_document.h"

#include <algorithm>
#include <array>
#include <memory>
#include <queue>
#include <set>
#include <utility>

#include "core/fdrm/fx_crypt_sha.h"
#include "core/fpdfapi/page/cpdf_docpagedata.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_object.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/render/cpdf_docrenderdata.h"
#include "core/fxcrt/fx_stream.h"
#include "core/fxcrt/span.h"

namespace {

constexpr size_t kSha256DigestSize = 32;

void PushIfNew(RetainPtr<const CPDF_Object> object,
               std::set<const CPDF_Object*>* visited,
               std::queue<RetainPtr<const CPDF_Object>>* worklist) {
  if (!object || !visited->insert(object.Get()).second) {
    return;
  }
  worklist->push(std::move(object));
}

bool ComputeStreamSha256(IFX_SeekableReadStream* stream,
                         FX_FILESIZE size,
                         std::array<uint8_t, kSha256DigestSize>* digest) {
  if (!stream || size < 0 || !digest) {
    return false;
  }

  CRYPT_sha2_context context;
  CRYPT_SHA256Start(&context);
  std::array<uint8_t, 8192> buffer = {};
  FX_FILESIZE offset = 0;
  while (offset < size) {
    const size_t read_size = static_cast<size_t>(
        std::min<FX_FILESIZE>(buffer.size(), size - offset));
    if (!stream->ReadBlockAtOffset(pdfium::span(buffer).first(read_size),
                                   offset)) {
      return false;
    }
    CRYPT_SHA256Update(&context, pdfium::span(buffer).first(read_size));
    offset += read_size;
  }

  CRYPT_SHA256Finish(&context, *digest);
  return true;
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
  if (!CacheBaseIdentity()) {
    return CPDF_Parser::FORMAT_ERROR;
  }
  return EagerlyParseAllReachable() ? CPDF_Parser::SUCCESS
                                    : CPDF_Parser::FORMAT_ERROR;
}

bool CPDF_BaseDocument::CacheBaseIdentity() {
  CPDF_Parser* parser = GetParser();
  RetainPtr<IFX_SeekableReadStream> stream =
      parser ? parser->GetFileAccess() : nullptr;
  if (!parser || !stream) {
    return false;
  }

  raw_base_size_ = stream->GetSize();
  if (raw_base_size_ < 0) {
    return false;
  }
  layer_append_base_offset_ = parser->GetDocumentSize();
  return ComputeStreamSha256(stream.Get(), raw_base_size_, &raw_base_sha256_);
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
