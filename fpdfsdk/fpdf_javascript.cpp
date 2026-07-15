// Copyright 2019 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "public/fpdf_javascript.h"

#include <memory>
#include <utility>

#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfdoc/cpdf_action.h"
#include "core/fpdfdoc/cpdf_nametree.h"
#include "core/fxcrt/compiler_specific.h"
#include "core/fxcrt/numerics/safe_conversions.h"
#include "fpdfsdk/cpdfsdk_helpers.h"
#include "fpdfsdk/epdf_action_helpers.h"
#include "public/epdf_action.h"

struct CPDF_JavaScript {
  WideString name;
  WideString script;
};

namespace {

RetainPtr<CPDF_Dictionary> GetNamedJavaScriptActionDictionary(
    FPDF_DOCUMENT document,
    int index,
    WideString* name,
    std::optional<WideString>* script) {
  CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
  if (!doc || index < 0) {
    return nullptr;
  }

  auto name_tree = CPDF_NameTree::CreateForReading(doc, "JavaScript");
  if (!name_tree || static_cast<size_t>(index) >= name_tree->GetCount()) {
    return nullptr;
  }

  RetainPtr<CPDF_Dictionary> dictionary =
      ToDictionary(name_tree->LookupValueAndName(index, name));
  if (!dictionary) {
    return nullptr;
  }

  CPDF_Action action(pdfium::WrapRetain(dictionary.Get()));
  if (action.GetType() != CPDF_Action::Type::kJavaScript) {
    return nullptr;
  }
  *script = action.MaybeGetJavaScript();
  return script->has_value() ? std::move(dictionary) : nullptr;
}

}  // namespace

FPDF_EXPORT int FPDF_CALLCONV
FPDFDoc_GetJavaScriptActionCount(FPDF_DOCUMENT document) {
  CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
  if (!doc) {
    return -1;
  }

  auto name_tree = CPDF_NameTree::CreateForReading(doc, "JavaScript");
  return name_tree ? pdfium::checked_cast<int>(name_tree->GetCount()) : 0;
}

FPDF_EXPORT FPDF_JAVASCRIPT_ACTION FPDF_CALLCONV
FPDFDoc_GetJavaScriptAction(FPDF_DOCUMENT document, int index) {
  WideString name;
  std::optional<WideString> script;
  if (!GetNamedJavaScriptActionDictionary(document, index, &name, &script)) {
    return nullptr;
  }

  auto js = std::make_unique<CPDF_JavaScript>();
  js->name = name;
  js->script = script.value();
  return FPDFJavaScriptActionFromCPDFJavaScriptAction(js.release());
}

FPDF_EXPORT EPDF_ACTION_MODEL FPDF_CALLCONV
EPDFDoc_GetNamedJavaScriptActionModel(FPDF_DOCUMENT document, int index) {
  WideString name;
  std::optional<WideString> script;
  RetainPtr<CPDF_Dictionary> dictionary =
      GetNamedJavaScriptActionDictionary(document, index, &name, &script);
  return dictionary ? epdf::MakeActionModelHandle(epdf::BuildActionModel(
                          CPDF_Action(std::move(dictionary))))
                    : nullptr;
}

FPDF_EXPORT void FPDF_CALLCONV
FPDFDoc_CloseJavaScriptAction(FPDF_JAVASCRIPT_ACTION javascript) {
  // Take object back across API and destroy it.
  std::unique_ptr<CPDF_JavaScript>(
      CPDFJavaScriptActionFromFPDFJavaScriptAction(javascript));
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDFJavaScriptAction_GetName(FPDF_JAVASCRIPT_ACTION javascript,
                             FPDF_WCHAR* buffer,
                             unsigned long buflen) {
  CPDF_JavaScript* js =
      CPDFJavaScriptActionFromFPDFJavaScriptAction(javascript);
  if (!js) {
    return 0;
  }
  // SAFETY: required from caller.
  return Utf16EncodeMaybeCopyAndReturnLength(
      js->name, UNSAFE_BUFFERS(SpanFromFPDFApiArgs(buffer, buflen)));
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDFJavaScriptAction_GetScript(FPDF_JAVASCRIPT_ACTION javascript,
                               FPDF_WCHAR* buffer,
                               unsigned long buflen) {
  CPDF_JavaScript* js =
      CPDFJavaScriptActionFromFPDFJavaScriptAction(javascript);
  if (!js) {
    return 0;
  }
  // SAFETY: required from caller.
  return Utf16EncodeMaybeCopyAndReturnLength(
      js->script, UNSAFE_BUFFERS(SpanFromFPDFApiArgs(buffer, buflen)));
}
