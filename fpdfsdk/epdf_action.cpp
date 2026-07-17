// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "public/epdf_action.h"

#include <array>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "core/fpdfapi/page/cpdf_annotcontext.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfdoc/cpdf_action.h"
#include "core/fxcrt/bytestring.h"
#include "core/fxcrt/compiler_specific.h"
#include "core/fxcrt/fx_string_wrappers.h"
#include "core/fxcrt/numerics/safe_conversions.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/span.h"
#include "fpdfsdk/cpdfsdk_helpers.h"
#include "fpdfsdk/epdf_action_helpers.h"

namespace epdf {

namespace {

constexpr size_t kMaxActionDepth = 64;
constexpr size_t kMaxActionNodes = 1024;
constexpr size_t kMaxJavaScriptCodeUnits = 8 * 1024 * 1024;

struct ActionNodeRecord {
  ByteString subtype;
  int type = EPDF_ACTION_TYPE_UNKNOWN;
  std::optional<WideString> javascript;
  std::vector<EPDF_ACTION_NODE_ID> next;
  // Keeps the node's action dictionary alive for the model's lifetime so
  // the EPDFAction_GetNode{Dest,URI,FilePath,Name} payload getters can
  // rehydrate a CPDF_Action on demand. Nothing is extracted eagerly:
  // building a model costs the same as before this field existed.
  RetainPtr<const CPDF_Dictionary> dict;
};

int NormalizeActionType(CPDF_Action::Type type) {
  switch (type) {
    case CPDF_Action::Type::kUnknown:
      return EPDF_ACTION_TYPE_UNKNOWN;
    case CPDF_Action::Type::kGoTo:
      return EPDF_ACTION_TYPE_GOTO;
    case CPDF_Action::Type::kGoToR:
      return EPDF_ACTION_TYPE_GOTO_REMOTE;
    case CPDF_Action::Type::kGoToE:
      return EPDF_ACTION_TYPE_GOTO_EMBEDDED;
    case CPDF_Action::Type::kLaunch:
      return EPDF_ACTION_TYPE_LAUNCH;
    case CPDF_Action::Type::kThread:
      return EPDF_ACTION_TYPE_THREAD;
    case CPDF_Action::Type::kURI:
      return EPDF_ACTION_TYPE_URI;
    case CPDF_Action::Type::kSound:
      return EPDF_ACTION_TYPE_SOUND;
    case CPDF_Action::Type::kMovie:
      return EPDF_ACTION_TYPE_MOVIE;
    case CPDF_Action::Type::kHide:
      return EPDF_ACTION_TYPE_HIDE;
    case CPDF_Action::Type::kNamed:
      return EPDF_ACTION_TYPE_NAMED;
    case CPDF_Action::Type::kSubmitForm:
      return EPDF_ACTION_TYPE_SUBMIT_FORM;
    case CPDF_Action::Type::kResetForm:
      return EPDF_ACTION_TYPE_RESET_FORM;
    case CPDF_Action::Type::kImportData:
      return EPDF_ACTION_TYPE_IMPORT_DATA;
    case CPDF_Action::Type::kJavaScript:
      return EPDF_ACTION_TYPE_JAVASCRIPT;
    case CPDF_Action::Type::kSetOCGState:
      return EPDF_ACTION_TYPE_SET_OCG_STATE;
    case CPDF_Action::Type::kRendition:
      return EPDF_ACTION_TYPE_RENDITION;
    case CPDF_Action::Type::kTrans:
      return EPDF_ACTION_TYPE_TRANSITION;
    case CPDF_Action::Type::kGoTo3DView:
      return EPDF_ACTION_TYPE_GOTO_3D_VIEW;
  }
  return EPDF_ACTION_TYPE_UNKNOWN;
}

}  // namespace

struct ActionModelData {
  std::vector<ActionNodeRecord> nodes;
  uint32_t warning_flags = 0;
};

namespace {

std::optional<EPDF_ACTION_NODE_ID> AppendAction(
    const CPDF_Action& action,
    size_t depth,
    size_t* javascript_code_units,
    std::set<const CPDF_Dictionary*>* active_path,
    ActionModelData* model) {
  const CPDF_Dictionary* dict = action.GetDict();
  if (!dict) {
    model->warning_flags |= EPDF_ACTION_WARNING_MALFORMED_NEXT;
    return std::nullopt;
  }
  if (depth >= kMaxActionDepth || model->nodes.size() >= kMaxActionNodes) {
    model->warning_flags |= EPDF_ACTION_WARNING_INCOMPLETE;
    return std::nullopt;
  }

  ActionNodeRecord node;
  node.subtype = dict->GetNameFor("S");
  node.type = NormalizeActionType(action.GetType());
  node.dict = pdfium::WrapRetain(dict);
  if (node.type == EPDF_ACTION_TYPE_JAVASCRIPT ||
      node.type == EPDF_ACTION_TYPE_RENDITION) {
    node.javascript = action.MaybeGetJavaScript();
    if (node.javascript.has_value()) {
      const size_t length = node.javascript->GetLength();
      if (length > kMaxJavaScriptCodeUnits - *javascript_code_units) {
        model->warning_flags |= EPDF_ACTION_WARNING_INCOMPLETE;
        return std::nullopt;
      }
      *javascript_code_units += length;
    }
  }

  const EPDF_ACTION_NODE_ID node_id =
      pdfium::checked_cast<EPDF_ACTION_NODE_ID>(model->nodes.size());
  model->nodes.push_back(std::move(node));
  active_path->insert(dict);

  if (dict->KeyExist("Next")) {
    RetainPtr<const CPDF_Object> next = dict->GetDirectObjectFor("Next");
    if (!next || (!next->IsDictionary() && !next->IsArray())) {
      model->warning_flags |= EPDF_ACTION_WARNING_MALFORMED_NEXT;
    }
  }
  const size_t child_count = action.GetSubActionsCount();
  for (size_t i = 0; i < child_count; ++i) {
    CPDF_Action child = action.GetSubAction(i);
    const CPDF_Dictionary* child_dict = child.GetDict();
    if (!child_dict) {
      model->warning_flags |= EPDF_ACTION_WARNING_MALFORMED_NEXT;
      continue;
    }
    if (active_path->contains(child_dict)) {
      model->warning_flags |= EPDF_ACTION_WARNING_CYCLE_DROPPED;
      continue;
    }
    std::optional<EPDF_ACTION_NODE_ID> child_id = AppendAction(
        child, depth + 1, javascript_code_units, active_path, model);
    if (child_id.has_value()) {
      model->nodes[node_id].next.push_back(child_id.value());
    }
  }

  active_path->erase(dict);
  return node_id;
}

const ActionModelData* DataFromHandle(EPDF_ACTION_MODEL model);

}  // namespace

ActionModelDataPtr BuildActionModel(const CPDF_Action& action) {
  if (!action.HasDict()) {
    return nullptr;
  }
  auto model = std::make_shared<ActionModelData>();
  size_t javascript_code_units = 0;
  std::set<const CPDF_Dictionary*> active_path;
  AppendAction(action, 0, &javascript_code_units, &active_path, model.get());
  return model;
}

}  // namespace epdf

struct epdf_action_model_t__ {
  explicit epdf_action_model_t__(epdf::ActionModelDataPtr data)
      : data(std::move(data)) {}

  epdf::ActionModelDataPtr data;
};

namespace epdf {

namespace {

const ActionModelData* DataFromHandle(EPDF_ACTION_MODEL model) {
  return model && model->data ? model->data.get() : nullptr;
}

const ActionNodeRecord* GetNode(EPDF_ACTION_MODEL model,
                                EPDF_ACTION_NODE_ID node) {
  const ActionModelData* data = DataFromHandle(model);
  return data && node < data->nodes.size() ? &data->nodes[node] : nullptr;
}

EPDF_ACTION_MODEL MakeModelFromDictionary(
    RetainPtr<const CPDF_Dictionary> dictionary) {
  return dictionary ? MakeActionModelHandle(
                          BuildActionModel(CPDF_Action(std::move(dictionary))))
                    : nullptr;
}

const CPDF_Dictionary* GetDocumentRoot(FPDF_DOCUMENT document) {
  CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
  return doc ? doc->GetRoot() : nullptr;
}

RetainPtr<const CPDF_Dictionary> GetPageDictionaryByObjectNumber(
    FPDF_DOCUMENT document,
    uint32_t page_object_number) {
  CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
  if (!doc || page_object_number == 0) {
    return nullptr;
  }
#ifdef PDF_ENABLE_XFA
  if (doc->GetExtension()) {
    return nullptr;
  }
#endif  // PDF_ENABLE_XFA
  const int page_index = doc->GetPageIndex(page_object_number);
  return page_index >= 0 ? doc->GetPageDictionary(page_index) : nullptr;
}

}  // namespace

EPDF_ACTION_MODEL MakeActionModelHandle(ActionModelDataPtr data) {
  return data ? new epdf_action_model_t__(std::move(data)) : nullptr;
}

}  // namespace epdf

FPDF_EXPORT void FPDF_CALLCONV EPDFAction_CloseModel(EPDF_ACTION_MODEL model) {
  delete model;
}

FPDF_EXPORT EPDF_ACTION_MODEL FPDF_CALLCONV
EPDFAction_LoadModel(FPDF_ACTION action) {
  CPDF_Dictionary* dictionary = CPDFDictionaryFromFPDFAction(action);
  return dictionary ? epdf::MakeActionModelHandle(epdf::BuildActionModel(
                          CPDF_Action(pdfium::WrapRetain(dictionary))))
                    : nullptr;
}

FPDF_EXPORT EPDF_ACTION_NODE_ID FPDF_CALLCONV
EPDFAction_GetRootNode(EPDF_ACTION_MODEL model) {
  const epdf::ActionModelData* data = epdf::DataFromHandle(model);
  return data && !data->nodes.empty() ? 0 : EPDF_ACTION_NODE_INVALID;
}

FPDF_EXPORT int FPDF_CALLCONV EPDFAction_GetNodeCount(EPDF_ACTION_MODEL model) {
  const epdf::ActionModelData* data = epdf::DataFromHandle(model);
  return data ? pdfium::checked_cast<int>(data->nodes.size()) : 0;
}

FPDF_EXPORT int FPDF_CALLCONV EPDFAction_GetNodeType(EPDF_ACTION_MODEL model,
                                                     EPDF_ACTION_NODE_ID node) {
  const epdf::ActionNodeRecord* record = epdf::GetNode(model, node);
  return record ? record->type : EPDF_ACTION_TYPE_UNKNOWN;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFAction_GetNodeSubtype(EPDF_ACTION_MODEL model,
                          EPDF_ACTION_NODE_ID node,
                          char* buffer,
                          unsigned long buflen) {
  const epdf::ActionNodeRecord* record = epdf::GetNode(model, node);
  if (!record) {
    return 0;
  }
  return NulTerminateMaybeCopyAndReturnLength(
      record->subtype, UNSAFE_BUFFERS(SpanFromFPDFApiArgs(buffer, buflen)));
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFAction_NodeHasJavaScript(EPDF_ACTION_MODEL model,
                             EPDF_ACTION_NODE_ID node) {
  const epdf::ActionNodeRecord* record = epdf::GetNode(model, node);
  return record && record->javascript.has_value();
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFAction_GetNodeJavaScript(EPDF_ACTION_MODEL model,
                             EPDF_ACTION_NODE_ID node,
                             FPDF_WCHAR* buffer,
                             unsigned long buflen) {
  const epdf::ActionNodeRecord* record = epdf::GetNode(model, node);
  if (!record || !record->javascript.has_value()) {
    return 0;
  }
  return Utf16EncodeMaybeCopyAndReturnLength(
      record->javascript.value(),
      UNSAFE_BUFFERS(SpanFromFPDFApiArgs(buffer, buflen)));
}

FPDF_EXPORT FPDF_DEST FPDF_CALLCONV
EPDFAction_GetNodeDest(FPDF_DOCUMENT document,
                       EPDF_ACTION_MODEL model,
                       EPDF_ACTION_NODE_ID node) {
  CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
  const epdf::ActionNodeRecord* record = epdf::GetNode(model, node);
  if (!doc || !record || !record->dict) {
    return nullptr;
  }
  // Same type gate as FPDFAction_GetDest.
  if (record->type != EPDF_ACTION_TYPE_GOTO &&
      record->type != EPDF_ACTION_TYPE_GOTO_REMOTE &&
      record->type != EPDF_ACTION_TYPE_GOTO_EMBEDDED) {
    return nullptr;
  }
  CPDF_Action cAction(record->dict);
  return FPDFDestFromCPDFArray(cAction.GetDest(doc).GetArray());
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFAction_GetNodeURI(FPDF_DOCUMENT document,
                      EPDF_ACTION_MODEL model,
                      EPDF_ACTION_NODE_ID node,
                      void* buffer,
                      unsigned long buflen) {
  CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
  const epdf::ActionNodeRecord* record = epdf::GetNode(model, node);
  if (!doc || !record || !record->dict ||
      record->type != EPDF_ACTION_TYPE_URI) {
    return 0;
  }
  CPDF_Action cAction(record->dict);
  ByteString uri = cAction.GetURI(doc);
  // SAFETY: required from caller.
  return NulTerminateMaybeCopyAndReturnLength(
      uri, UNSAFE_BUFFERS(SpanFromFPDFApiArgs(buffer, buflen)));
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFAction_GetNodeFilePath(EPDF_ACTION_MODEL model,
                           EPDF_ACTION_NODE_ID node,
                           void* buffer,
                           unsigned long buflen) {
  const epdf::ActionNodeRecord* record = epdf::GetNode(model, node);
  if (!record || !record->dict) {
    return 0;
  }
  // Same type gate as FPDFAction_GetFilePath.
  if (record->type != EPDF_ACTION_TYPE_GOTO_REMOTE &&
      record->type != EPDF_ACTION_TYPE_GOTO_EMBEDDED &&
      record->type != EPDF_ACTION_TYPE_LAUNCH) {
    return 0;
  }
  CPDF_Action cAction(record->dict);
  ByteString path = cAction.GetFilePath().ToUTF8();
  // SAFETY: required from caller.
  return NulTerminateMaybeCopyAndReturnLength(
      path, UNSAFE_BUFFERS(SpanFromFPDFApiArgs(buffer, buflen)));
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFAction_GetNodeName(EPDF_ACTION_MODEL model,
                       EPDF_ACTION_NODE_ID node,
                       void* buffer,
                       unsigned long buflen) {
  const epdf::ActionNodeRecord* record = epdf::GetNode(model, node);
  if (!record || !record->dict ||
      record->type != EPDF_ACTION_TYPE_NAMED) {
    return 0;
  }
  ByteString name = record->dict->GetNameFor("N");
  // SAFETY: required from caller.
  return NulTerminateMaybeCopyAndReturnLength(
      name, UNSAFE_BUFFERS(SpanFromFPDFApiArgs(buffer, buflen)));
}

FPDF_EXPORT int FPDF_CALLCONV
EPDFAction_GetNextCount(EPDF_ACTION_MODEL model, EPDF_ACTION_NODE_ID node) {
  const epdf::ActionNodeRecord* record = epdf::GetNode(model, node);
  return record ? pdfium::checked_cast<int>(record->next.size()) : 0;
}

FPDF_EXPORT EPDF_ACTION_NODE_ID FPDF_CALLCONV
EPDFAction_GetNextAt(EPDF_ACTION_MODEL model,
                     EPDF_ACTION_NODE_ID node,
                     int index) {
  const epdf::ActionNodeRecord* record = epdf::GetNode(model, node);
  if (!record || index < 0 ||
      static_cast<size_t>(index) >= record->next.size()) {
    return EPDF_ACTION_NODE_INVALID;
  }
  return record->next[static_cast<size_t>(index)];
}

FPDF_EXPORT uint32_t FPDF_CALLCONV
EPDFAction_GetWarningFlags(EPDF_ACTION_MODEL model) {
  const epdf::ActionModelData* data = epdf::DataFromHandle(model);
  return data ? data->warning_flags : EPDF_ACTION_WARNING_INCOMPLETE;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFAction_IsComplete(EPDF_ACTION_MODEL model) {
  const epdf::ActionModelData* data = epdf::DataFromHandle(model);
  return data && !data->nodes.empty() &&
         !(data->warning_flags & EPDF_ACTION_WARNING_INCOMPLETE);
}

FPDF_EXPORT EPDF_ACTION_MODEL FPDF_CALLCONV
EPDFDoc_GetOpenActionModel(FPDF_DOCUMENT document) {
  const CPDF_Dictionary* root = epdf::GetDocumentRoot(document);
  return root ? epdf::MakeModelFromDictionary(root->GetDictFor("OpenAction"))
              : nullptr;
}

FPDF_EXPORT EPDF_ACTION_MODEL FPDF_CALLCONV
EPDFDoc_GetAdditionalActionModel(FPDF_DOCUMENT document, int event) {
  static constexpr std::array<const char*, 5> kKeys = {"WC", "WS", "DS", "WP",
                                                       "DP"};
  if (event < 0 || event >= static_cast<int>(kKeys.size())) {
    return nullptr;
  }
  const CPDF_Dictionary* root = epdf::GetDocumentRoot(document);
  RetainPtr<const CPDF_Dictionary> additional =
      root ? root->GetDictFor("AA") : nullptr;
  return additional ? epdf::MakeModelFromDictionary(
                          additional->GetDictFor(kKeys[event]))
                    : nullptr;
}

FPDF_EXPORT EPDF_ACTION_MODEL FPDF_CALLCONV
EPDFDoc_GetPageActionModel(FPDF_DOCUMENT document,
                           uint32_t page_object_number,
                           int event) {
  if (event != EPDF_PAGE_ACTION_OPEN && event != EPDF_PAGE_ACTION_CLOSE) {
    return nullptr;
  }
  RetainPtr<const CPDF_Dictionary> page =
      epdf::GetPageDictionaryByObjectNumber(document, page_object_number);
  RetainPtr<const CPDF_Dictionary> additional =
      page ? page->GetDictFor("AA") : nullptr;
  const char* key = event == EPDF_PAGE_ACTION_OPEN ? "O" : "C";
  return additional ? epdf::MakeModelFromDictionary(additional->GetDictFor(key))
                    : nullptr;
}

FPDF_EXPORT EPDF_ACTION_MODEL FPDF_CALLCONV
EPDFAnnot_GetActionModel(FPDF_ANNOTATION annotation, int event) {
  static constexpr std::array<const char*, 10> kAdditionalKeys = {
      "E", "X", "D", "U", "Fo", "Bl", "PO", "PC", "PV", "PI"};
  if (event < EPDF_ANNOT_ACTION_ACTIVATE ||
      event > EPDF_ANNOT_ACTION_PAGE_INVISIBLE) {
    return nullptr;
  }
  CPDF_AnnotContext* context = CPDFAnnotContextFromFPDFAnnotation(annotation);
  const CPDF_Dictionary* annotation_dict =
      context ? context->GetAnnotDict() : nullptr;
  if (!annotation_dict) {
    return nullptr;
  }
  if (event == EPDF_ANNOT_ACTION_ACTIVATE) {
    return epdf::MakeModelFromDictionary(annotation_dict->GetDictFor("A"));
  }
  RetainPtr<const CPDF_Dictionary> additional =
      annotation_dict->GetDictFor("AA");
  const int additional_index = event - EPDF_ANNOT_ACTION_CURSOR_ENTER;
  return additional ? epdf::MakeModelFromDictionary(additional->GetDictFor(
                          kAdditionalKeys[additional_index]))
                    : nullptr;
}
