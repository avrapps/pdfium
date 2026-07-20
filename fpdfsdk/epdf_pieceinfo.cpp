// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "public/epdf_pieceinfo.h"

#include <optional>
#include <utility>

#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_boolean.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_name.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/cpdf_object.h"
#include "core/fpdfapi/parser/cpdf_string.h"
#include "core/fxcrt/bytestring.h"
#include "core/fxcrt/fx_string_wrappers.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/span.h"
#include "fpdfsdk/cpdfsdk_helpers.h"

namespace {

constexpr char kPieceInfoKey[] = "PieceInfo";
constexpr char kPrivateKey[] = "Private";
constexpr char kLastModifiedKey[] = "LastModified";
constexpr char kModDateKey[] = "ModDate";

const CPDF_Dictionary* GetDocumentCatalog(FPDF_DOCUMENT document) {
  CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
  return doc ? doc->GetRoot() : nullptr;
}

RetainPtr<CPDF_Dictionary> GetMutableDocumentCatalog(FPDF_DOCUMENT document) {
  CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
  return doc ? doc->GetMutableRoot() : nullptr;
}

RetainPtr<const CPDF_Dictionary> GetDocumentInfo(FPDF_DOCUMENT document) {
  CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
  return doc ? doc->GetInfo() : nullptr;
}

RetainPtr<const CPDF_Dictionary> GetPageDictionaryByObjectNumber(
    FPDF_DOCUMENT document,
    unsigned int page_object_number) {
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

RetainPtr<CPDF_Dictionary> GetMutablePageDictionaryByObjectNumber(
    FPDF_DOCUMENT document,
    unsigned int page_object_number) {
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
  return page_index >= 0 ? doc->GetMutablePageDictionary(page_index) : nullptr;
}

RetainPtr<const CPDF_Dictionary> GetApplicationDictionary(
    const CPDF_Dictionary* page,
    FPDF_BYTESTRING application) {
  if (!page || !application || !*application) {
    return nullptr;
  }
  RetainPtr<const CPDF_Dictionary> piece_info = page->GetDictFor(kPieceInfoKey);
  return piece_info ? piece_info->GetDictFor(application) : nullptr;
}

RetainPtr<const CPDF_Dictionary> GetPrivateDictionary(
    const CPDF_Dictionary* page,
    FPDF_BYTESTRING application) {
  RetainPtr<const CPDF_Dictionary> app =
      GetApplicationDictionary(page, application);
  return app ? app->GetDictFor(kPrivateKey) : nullptr;
}

RetainPtr<const CPDF_Object> GetPrivateObject(const CPDF_Dictionary* page,
                                              FPDF_BYTESTRING application,
                                              FPDF_BYTESTRING key) {
  if (!key || !*key) {
    return nullptr;
  }
  RetainPtr<const CPDF_Dictionary> private_dict =
      GetPrivateDictionary(page, application);
  return private_dict ? private_dict->GetDirectObjectFor(key) : nullptr;
}

RetainPtr<CPDF_Dictionary> GetOrCreateDictionary(CPDF_Dictionary* parent,
                                                 ByteStringView key) {
  if (!parent) {
    return nullptr;
  }
  if (parent->KeyExist(key)) {
    return parent->GetMutableDictFor(key);
  }
  return parent->SetNewFor<CPDF_Dictionary>(ByteString(key));
}

struct MutablePieceInfo {
  RetainPtr<CPDF_Dictionary> holder;
  RetainPtr<CPDF_Dictionary> application;
  RetainPtr<CPDF_Dictionary> private_dict;
};

std::optional<WideString> GetTimestamp(FPDF_WIDESTRING content_last_modified) {
  if (!content_last_modified) {
    return std::nullopt;
  }
  WideString timestamp =
      UNSAFE_BUFFERS(WideStringFromFPDFWideString(content_last_modified));
  return timestamp.IsEmpty() ? std::nullopt
                             : std::optional<WideString>(std::move(timestamp));
}

MutablePieceInfo GetOrCreateMutablePieceInfo(
    FPDF_DOCUMENT document,
    unsigned int page_object_number,
    FPDF_BYTESTRING application,
    FPDF_WIDESTRING content_last_modified) {
  if (!application || !*application) {
    return {};
  }
  std::optional<WideString> timestamp = GetTimestamp(content_last_modified);
  if (!timestamp.has_value()) {
    return {};
  }

  RetainPtr<CPDF_Dictionary> page =
      GetMutablePageDictionaryByObjectNumber(document, page_object_number);
  if (!page) {
    return {};
  }
  RetainPtr<CPDF_Dictionary> piece_info =
      GetOrCreateDictionary(page.Get(), kPieceInfoKey);
  if (!piece_info) {
    return {};
  }
  RetainPtr<CPDF_Dictionary> app =
      GetOrCreateDictionary(piece_info.Get(), application);
  if (!app) {
    return {};
  }
  RetainPtr<CPDF_Dictionary> private_dict =
      GetOrCreateDictionary(app.Get(), kPrivateKey);
  if (!private_dict) {
    return {};
  }

  page->SetNewFor<CPDF_String>(kLastModifiedKey, timestamp->AsStringView());
  app->SetNewFor<CPDF_String>(kLastModifiedKey, timestamp->AsStringView());
  return {std::move(page), std::move(app), std::move(private_dict)};
}

MutablePieceInfo GetOrCreateMutableDocumentPieceInfo(
    FPDF_DOCUMENT document,
    FPDF_BYTESTRING application,
    FPDF_WIDESTRING document_last_modified) {
  if (!application || !*application) {
    return {};
  }
  std::optional<WideString> timestamp = GetTimestamp(document_last_modified);
  if (!timestamp.has_value()) {
    return {};
  }

  CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
  if (!doc) {
    return {};
  }
  RetainPtr<CPDF_Dictionary> catalog = doc->GetMutableRoot();
  RetainPtr<CPDF_Dictionary> info = doc->GetOrCreateInfo();
  if (!catalog || !info) {
    return {};
  }
  RetainPtr<CPDF_Dictionary> piece_info =
      GetOrCreateDictionary(catalog.Get(), kPieceInfoKey);
  if (!piece_info) {
    return {};
  }
  RetainPtr<CPDF_Dictionary> app =
      GetOrCreateDictionary(piece_info.Get(), application);
  if (!app) {
    return {};
  }
  RetainPtr<CPDF_Dictionary> private_dict =
      GetOrCreateDictionary(app.Get(), kPrivateKey);
  if (!private_dict) {
    return {};
  }

  info->SetNewFor<CPDF_String>(kModDateKey, timestamp->AsStringView());
  app->SetNewFor<CPDF_String>(kLastModifiedKey, timestamp->AsStringView());
  return {std::move(catalog), std::move(app), std::move(private_dict)};
}

unsigned long CopyWideString(const WideString& value,
                             FPDF_WCHAR* buffer,
                             unsigned long buflen) {
  // SAFETY: required from caller.
  return Utf16EncodeMaybeCopyAndReturnLength(
      value, UNSAFE_BUFFERS(SpanFromFPDFApiArgs(buffer, buflen)));
}

unsigned long CopyByteString(ByteStringView value,
                             char* buffer,
                             unsigned long buflen) {
  // SAFETY: required from caller.
  return NulTerminateMaybeCopyAndReturnLength(
      ByteString(value), UNSAFE_BUFFERS(SpanFromFPDFApiArgs(buffer, buflen)));
}

}  // namespace

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_HasPieceInfoEntry(FPDF_DOCUMENT document, FPDF_BYTESTRING application) {
  return !!GetApplicationDictionary(GetDocumentCatalog(document), application);
}

FPDF_EXPORT int FPDF_CALLCONV
EPDFDoc_GetPieceInfoEntryCount(FPDF_DOCUMENT document) {
  const CPDF_Dictionary* catalog = GetDocumentCatalog(document);
  RetainPtr<const CPDF_Dictionary> piece_info =
      catalog ? catalog->GetDictFor(kPieceInfoKey) : nullptr;
  return piece_info ? static_cast<int>(piece_info->size()) : 0;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPieceInfoEntryAt(FPDF_DOCUMENT document,
                            int index,
                            char* buffer,
                            unsigned long buflen) {
  if (index < 0) {
    return 0;
  }
  const CPDF_Dictionary* catalog = GetDocumentCatalog(document);
  RetainPtr<const CPDF_Dictionary> piece_info =
      catalog ? catalog->GetDictFor(kPieceInfoKey) : nullptr;
  if (!piece_info || index >= static_cast<int>(piece_info->size())) {
    return 0;
  }

  int current = 0;
  CPDF_DictionaryLocker locker(piece_info);
  for (const auto& item : locker) {
    if (current++ == index) {
      return CopyByteString(item.first.AsStringView(), buffer, buflen);
    }
  }
  return 0;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetLastModified(FPDF_DOCUMENT document,
                        FPDF_WCHAR* buffer,
                        unsigned long buflen) {
  RetainPtr<const CPDF_Dictionary> info = GetDocumentInfo(document);
  if (!info) {
    return 0;
  }
  RetainPtr<const CPDF_Object> value = info->GetDirectObjectFor(kModDateKey);
  return value && value->GetType() == CPDF_Object::Type::kString
             ? CopyWideString(value->GetUnicodeText(), buffer, buflen)
             : 0;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPieceInfoLastModified(FPDF_DOCUMENT document,
                                 FPDF_BYTESTRING application,
                                 FPDF_WCHAR* buffer,
                                 unsigned long buflen) {
  RetainPtr<const CPDF_Dictionary> app =
      GetApplicationDictionary(GetDocumentCatalog(document), application);
  if (!app) {
    return 0;
  }
  RetainPtr<const CPDF_Object> value =
      app->GetDirectObjectFor(kLastModifiedKey);
  return value && value->GetType() == CPDF_Object::Type::kString
             ? CopyWideString(value->GetUnicodeText(), buffer, buflen)
             : 0;
}

FPDF_EXPORT int FPDF_CALLCONV
EPDFDoc_GetPieceInfoKeyCount(FPDF_DOCUMENT document,
                             FPDF_BYTESTRING application) {
  RetainPtr<const CPDF_Dictionary> private_dict =
      GetPrivateDictionary(GetDocumentCatalog(document), application);
  return private_dict ? static_cast<int>(private_dict->size()) : 0;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPieceInfoKeyAt(FPDF_DOCUMENT document,
                          FPDF_BYTESTRING application,
                          int index,
                          char* buffer,
                          unsigned long buflen) {
  if (index < 0) {
    return 0;
  }
  RetainPtr<const CPDF_Dictionary> private_dict =
      GetPrivateDictionary(GetDocumentCatalog(document), application);
  if (!private_dict || index >= static_cast<int>(private_dict->size())) {
    return 0;
  }

  int current = 0;
  CPDF_DictionaryLocker locker(private_dict);
  for (const auto& item : locker) {
    if (current++ == index) {
      return CopyByteString(item.first.AsStringView(), buffer, buflen);
    }
  }
  return 0;
}

FPDF_EXPORT FPDF_OBJECT_TYPE FPDF_CALLCONV
EPDFDoc_GetPieceInfoValueType(FPDF_DOCUMENT document,
                              FPDF_BYTESTRING application,
                              FPDF_BYTESTRING key) {
  RetainPtr<const CPDF_Object> object =
      GetPrivateObject(GetDocumentCatalog(document), application, key);
  return object ? static_cast<FPDF_OBJECT_TYPE>(object->GetType())
                : FPDF_OBJECT_UNKNOWN;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPieceInfoString(FPDF_DOCUMENT document,
                           FPDF_BYTESTRING application,
                           FPDF_BYTESTRING key,
                           FPDF_WIDESTRING value,
                           FPDF_WIDESTRING document_last_modified) {
  if (!key || !*key || !value) {
    return false;
  }
  MutablePieceInfo target = GetOrCreateMutableDocumentPieceInfo(
      document, application, document_last_modified);
  if (!target.private_dict) {
    return false;
  }
  target.private_dict->SetNewFor<CPDF_String>(
      key, UNSAFE_BUFFERS(WideStringFromFPDFWideString(value).AsStringView()));
  return true;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPieceInfoString(FPDF_DOCUMENT document,
                           FPDF_BYTESTRING application,
                           FPDF_BYTESTRING key,
                           FPDF_WCHAR* buffer,
                           unsigned long buflen) {
  RetainPtr<const CPDF_Object> object =
      GetPrivateObject(GetDocumentCatalog(document), application, key);
  return object && object->GetType() == CPDF_Object::Type::kString
             ? CopyWideString(object->GetUnicodeText(), buffer, buflen)
             : 0;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPieceInfoNumber(FPDF_DOCUMENT document,
                           FPDF_BYTESTRING application,
                           FPDF_BYTESTRING key,
                           float value,
                           FPDF_WIDESTRING document_last_modified) {
  if (!key || !*key) {
    return false;
  }
  MutablePieceInfo target = GetOrCreateMutableDocumentPieceInfo(
      document, application, document_last_modified);
  if (!target.private_dict) {
    return false;
  }
  target.private_dict->SetNewFor<CPDF_Number>(key, value);
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_GetPieceInfoNumber(FPDF_DOCUMENT document,
                           FPDF_BYTESTRING application,
                           FPDF_BYTESTRING key,
                           float* value) {
  if (!value) {
    return false;
  }
  RetainPtr<const CPDF_Object> object =
      GetPrivateObject(GetDocumentCatalog(document), application, key);
  if (!object || object->GetType() != CPDF_Object::Type::kNumber) {
    return false;
  }
  *value = object->GetNumber();
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPieceInfoBoolean(FPDF_DOCUMENT document,
                            FPDF_BYTESTRING application,
                            FPDF_BYTESTRING key,
                            FPDF_BOOL value,
                            FPDF_WIDESTRING document_last_modified) {
  if (!key || !*key) {
    return false;
  }
  MutablePieceInfo target = GetOrCreateMutableDocumentPieceInfo(
      document, application, document_last_modified);
  if (!target.private_dict) {
    return false;
  }
  target.private_dict->SetNewFor<CPDF_Boolean>(key, !!value);
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_GetPieceInfoBoolean(FPDF_DOCUMENT document,
                            FPDF_BYTESTRING application,
                            FPDF_BYTESTRING key,
                            FPDF_BOOL* value) {
  if (!value) {
    return false;
  }
  RetainPtr<const CPDF_Object> object =
      GetPrivateObject(GetDocumentCatalog(document), application, key);
  if (!object || object->GetType() != CPDF_Object::Type::kBoolean) {
    return false;
  }
  *value = object->GetInteger() != 0;
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPieceInfoName(FPDF_DOCUMENT document,
                         FPDF_BYTESTRING application,
                         FPDF_BYTESTRING key,
                         FPDF_BYTESTRING value,
                         FPDF_WIDESTRING document_last_modified) {
  if (!key || !*key || !value || !*value) {
    return false;
  }
  MutablePieceInfo target = GetOrCreateMutableDocumentPieceInfo(
      document, application, document_last_modified);
  if (!target.private_dict) {
    return false;
  }
  target.private_dict->SetNewFor<CPDF_Name>(key, value);
  return true;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPieceInfoName(FPDF_DOCUMENT document,
                         FPDF_BYTESTRING application,
                         FPDF_BYTESTRING key,
                         char* buffer,
                         unsigned long buflen) {
  RetainPtr<const CPDF_Object> object =
      GetPrivateObject(GetDocumentCatalog(document), application, key);
  return object && object->GetType() == CPDF_Object::Type::kName
             ? CopyByteString(object->GetString().AsStringView(), buffer,
                              buflen)
             : 0;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPieceInfoStringArray(FPDF_DOCUMENT document,
                                FPDF_BYTESTRING application,
                                FPDF_BYTESTRING key,
                                const FPDF_WIDESTRING* values,
                                unsigned long value_count,
                                FPDF_WIDESTRING document_last_modified) {
  if (!key || !*key || (value_count > 0 && !values)) {
    return false;
  }
  for (unsigned long i = 0; i < value_count; ++i) {
    if (!UNSAFE_BUFFERS(values[i])) {
      return false;
    }
  }
  MutablePieceInfo target = GetOrCreateMutableDocumentPieceInfo(
      document, application, document_last_modified);
  if (!target.private_dict) {
    return false;
  }

  auto array = pdfium::MakeRetain<CPDF_Array>();
  for (unsigned long i = 0; i < value_count; ++i) {
    FPDF_WIDESTRING value = UNSAFE_BUFFERS(values[i]);
    array->AppendNew<CPDF_String>(
        UNSAFE_BUFFERS(WideStringFromFPDFWideString(value).AsStringView()));
  }
  target.private_dict->SetFor(key, std::move(array));
  return true;
}

FPDF_EXPORT int FPDF_CALLCONV
EPDFDoc_GetPieceInfoStringArrayCount(FPDF_DOCUMENT document,
                                     FPDF_BYTESTRING application,
                                     FPDF_BYTESTRING key) {
  RetainPtr<const CPDF_Object> object =
      GetPrivateObject(GetDocumentCatalog(document), application, key);
  RetainPtr<const CPDF_Array> array = ToArray(std::move(object));
  if (!array) {
    return -1;
  }
  for (size_t i = 0; i < array->size(); ++i) {
    RetainPtr<const CPDF_Object> item = array->GetDirectObjectAt(i);
    if (!item || item->GetType() != CPDF_Object::Type::kString) {
      return -1;
    }
  }
  return static_cast<int>(array->size());
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPieceInfoStringArrayAt(FPDF_DOCUMENT document,
                                  FPDF_BYTESTRING application,
                                  FPDF_BYTESTRING key,
                                  int index,
                                  FPDF_WCHAR* buffer,
                                  unsigned long buflen) {
  if (index < 0) {
    return 0;
  }
  RetainPtr<const CPDF_Object> object =
      GetPrivateObject(GetDocumentCatalog(document), application, key);
  RetainPtr<const CPDF_Array> array = ToArray(std::move(object));
  if (!array || index >= static_cast<int>(array->size())) {
    return 0;
  }
  RetainPtr<const CPDF_Object> item = array->GetDirectObjectAt(index);
  return item && item->GetType() == CPDF_Object::Type::kString
             ? CopyWideString(item->GetUnicodeText(), buffer, buflen)
             : 0;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_ClearPieceInfoKey(FPDF_DOCUMENT document,
                          FPDF_BYTESTRING application,
                          FPDF_BYTESTRING key,
                          FPDF_WIDESTRING document_last_modified) {
  if (!key || !*key || !application || !*application) {
    return false;
  }

  const CPDF_Dictionary* catalog = GetDocumentCatalog(document);
  if (!catalog) {
    return false;
  }
  RetainPtr<const CPDF_Object> piece_info_object =
      catalog->GetDirectObjectFor(kPieceInfoKey);
  if (!piece_info_object) {
    return true;
  }
  const CPDF_Dictionary* piece_info = piece_info_object->AsDictionary();
  if (!piece_info) {
    return false;
  }
  RetainPtr<const CPDF_Object> app_object =
      piece_info->GetDirectObjectFor(application);
  if (!app_object) {
    return true;
  }
  const CPDF_Dictionary* app = app_object->AsDictionary();
  if (!app) {
    return false;
  }
  RetainPtr<const CPDF_Object> private_object =
      app->GetDirectObjectFor(kPrivateKey);
  if (!private_object) {
    return true;
  }
  const CPDF_Dictionary* private_dict = private_object->AsDictionary();
  if (!private_dict) {
    return false;
  }
  if (!private_dict->KeyExist(key)) {
    return true;
  }

  MutablePieceInfo target = GetOrCreateMutableDocumentPieceInfo(
      document, application, document_last_modified);
  if (!target.private_dict) {
    return false;
  }
  target.private_dict->RemoveFor(key);
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_ClearPieceInfoEntry(FPDF_DOCUMENT document,
                            FPDF_BYTESTRING application) {
  if (!application || !*application) {
    return false;
  }
  const CPDF_Dictionary* const_catalog = GetDocumentCatalog(document);
  if (!const_catalog) {
    return false;
  }
  RetainPtr<const CPDF_Object> piece_info_object =
      const_catalog->GetDirectObjectFor(kPieceInfoKey);
  if (!piece_info_object) {
    return true;
  }
  const CPDF_Dictionary* const_piece_info = piece_info_object->AsDictionary();
  if (!const_piece_info) {
    return false;
  }
  if (!const_piece_info->KeyExist(application)) {
    return true;
  }

  RetainPtr<CPDF_Dictionary> catalog = GetMutableDocumentCatalog(document);
  if (!catalog) {
    return false;
  }
  RetainPtr<CPDF_Dictionary> piece_info =
      catalog->GetMutableDictFor(kPieceInfoKey);
  if (!piece_info) {
    return !catalog->KeyExist(kPieceInfoKey);
  }
  piece_info->RemoveFor(application);
  if (piece_info->size() == 0) {
    catalog->RemoveFor(kPieceInfoKey);
  }
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_HasPagePieceInfoEntry(FPDF_DOCUMENT document,
                              unsigned int page_object_number,
                              FPDF_BYTESTRING application) {
  RetainPtr<const CPDF_Dictionary> page =
      GetPageDictionaryByObjectNumber(document, page_object_number);
  return !!GetApplicationDictionary(page.Get(), application);
}

FPDF_EXPORT int FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoEntryCount(FPDF_DOCUMENT document,
                                   unsigned int page_object_number) {
  RetainPtr<const CPDF_Dictionary> page =
      GetPageDictionaryByObjectNumber(document, page_object_number);
  RetainPtr<const CPDF_Dictionary> piece_info =
      page ? page->GetDictFor(kPieceInfoKey) : nullptr;
  return piece_info ? static_cast<int>(piece_info->size()) : 0;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoEntryAt(FPDF_DOCUMENT document,
                                unsigned int page_object_number,
                                int index,
                                char* buffer,
                                unsigned long buflen) {
  if (index < 0) {
    return 0;
  }
  RetainPtr<const CPDF_Dictionary> page =
      GetPageDictionaryByObjectNumber(document, page_object_number);
  RetainPtr<const CPDF_Dictionary> piece_info =
      page ? page->GetDictFor(kPieceInfoKey) : nullptr;
  if (!piece_info || index >= static_cast<int>(piece_info->size())) {
    return 0;
  }

  int current = 0;
  CPDF_DictionaryLocker locker(piece_info);
  for (const auto& item : locker) {
    if (current++ == index) {
      return CopyByteString(item.first.AsStringView(), buffer, buflen);
    }
  }
  return 0;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPageLastModified(FPDF_DOCUMENT document,
                            unsigned int page_object_number,
                            FPDF_WCHAR* buffer,
                            unsigned long buflen) {
  RetainPtr<const CPDF_Dictionary> page =
      GetPageDictionaryByObjectNumber(document, page_object_number);
  if (!page) {
    return 0;
  }
  RetainPtr<const CPDF_Object> value =
      page->GetDirectObjectFor(kLastModifiedKey);
  return value && value->GetType() == CPDF_Object::Type::kString
             ? CopyWideString(value->GetUnicodeText(), buffer, buflen)
             : 0;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoLastModified(FPDF_DOCUMENT document,
                                     unsigned int page_object_number,
                                     FPDF_BYTESTRING application,
                                     FPDF_WCHAR* buffer,
                                     unsigned long buflen) {
  RetainPtr<const CPDF_Dictionary> page =
      GetPageDictionaryByObjectNumber(document, page_object_number);
  RetainPtr<const CPDF_Dictionary> app =
      GetApplicationDictionary(page.Get(), application);
  if (!app) {
    return 0;
  }
  RetainPtr<const CPDF_Object> value =
      app->GetDirectObjectFor(kLastModifiedKey);
  return value && value->GetType() == CPDF_Object::Type::kString
             ? CopyWideString(value->GetUnicodeText(), buffer, buflen)
             : 0;
}

FPDF_EXPORT int FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoKeyCount(FPDF_DOCUMENT document,
                                 unsigned int page_object_number,
                                 FPDF_BYTESTRING application) {
  RetainPtr<const CPDF_Dictionary> page =
      GetPageDictionaryByObjectNumber(document, page_object_number);
  RetainPtr<const CPDF_Dictionary> private_dict =
      GetPrivateDictionary(page.Get(), application);
  return private_dict ? static_cast<int>(private_dict->size()) : 0;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoKeyAt(FPDF_DOCUMENT document,
                              unsigned int page_object_number,
                              FPDF_BYTESTRING application,
                              int index,
                              char* buffer,
                              unsigned long buflen) {
  if (index < 0) {
    return 0;
  }
  RetainPtr<const CPDF_Dictionary> page =
      GetPageDictionaryByObjectNumber(document, page_object_number);
  RetainPtr<const CPDF_Dictionary> private_dict =
      GetPrivateDictionary(page.Get(), application);
  if (!private_dict || index >= static_cast<int>(private_dict->size())) {
    return 0;
  }

  int current = 0;
  CPDF_DictionaryLocker locker(private_dict);
  for (const auto& item : locker) {
    if (current++ == index) {
      return CopyByteString(item.first.AsStringView(), buffer, buflen);
    }
  }
  return 0;
}

FPDF_EXPORT FPDF_OBJECT_TYPE FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoValueType(FPDF_DOCUMENT document,
                                  unsigned int page_object_number,
                                  FPDF_BYTESTRING application,
                                  FPDF_BYTESTRING key) {
  RetainPtr<const CPDF_Dictionary> page =
      GetPageDictionaryByObjectNumber(document, page_object_number);
  RetainPtr<const CPDF_Object> object =
      GetPrivateObject(page.Get(), application, key);
  return object ? static_cast<FPDF_OBJECT_TYPE>(object->GetType())
                : FPDF_OBJECT_UNKNOWN;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPagePieceInfoString(FPDF_DOCUMENT document,
                               unsigned int page_object_number,
                               FPDF_BYTESTRING application,
                               FPDF_BYTESTRING key,
                               FPDF_WIDESTRING value,
                               FPDF_WIDESTRING content_last_modified) {
  if (!key || !*key || !value) {
    return false;
  }
  MutablePieceInfo target = GetOrCreateMutablePieceInfo(
      document, page_object_number, application, content_last_modified);
  if (!target.private_dict) {
    return false;
  }
  target.private_dict->SetNewFor<CPDF_String>(
      key, UNSAFE_BUFFERS(WideStringFromFPDFWideString(value).AsStringView()));
  return true;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoString(FPDF_DOCUMENT document,
                               unsigned int page_object_number,
                               FPDF_BYTESTRING application,
                               FPDF_BYTESTRING key,
                               FPDF_WCHAR* buffer,
                               unsigned long buflen) {
  RetainPtr<const CPDF_Dictionary> page =
      GetPageDictionaryByObjectNumber(document, page_object_number);
  RetainPtr<const CPDF_Object> object =
      GetPrivateObject(page.Get(), application, key);
  return object && object->GetType() == CPDF_Object::Type::kString
             ? CopyWideString(object->GetUnicodeText(), buffer, buflen)
             : 0;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPagePieceInfoNumber(FPDF_DOCUMENT document,
                               unsigned int page_object_number,
                               FPDF_BYTESTRING application,
                               FPDF_BYTESTRING key,
                               float value,
                               FPDF_WIDESTRING content_last_modified) {
  if (!key || !*key) {
    return false;
  }
  MutablePieceInfo target = GetOrCreateMutablePieceInfo(
      document, page_object_number, application, content_last_modified);
  if (!target.private_dict) {
    return false;
  }
  target.private_dict->SetNewFor<CPDF_Number>(key, value);
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoNumber(FPDF_DOCUMENT document,
                               unsigned int page_object_number,
                               FPDF_BYTESTRING application,
                               FPDF_BYTESTRING key,
                               float* value) {
  if (!value) {
    return false;
  }
  RetainPtr<const CPDF_Dictionary> page =
      GetPageDictionaryByObjectNumber(document, page_object_number);
  RetainPtr<const CPDF_Object> object =
      GetPrivateObject(page.Get(), application, key);
  if (!object || object->GetType() != CPDF_Object::Type::kNumber) {
    return false;
  }
  *value = object->GetNumber();
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPagePieceInfoBoolean(FPDF_DOCUMENT document,
                                unsigned int page_object_number,
                                FPDF_BYTESTRING application,
                                FPDF_BYTESTRING key,
                                FPDF_BOOL value,
                                FPDF_WIDESTRING content_last_modified) {
  if (!key || !*key) {
    return false;
  }
  MutablePieceInfo target = GetOrCreateMutablePieceInfo(
      document, page_object_number, application, content_last_modified);
  if (!target.private_dict) {
    return false;
  }
  target.private_dict->SetNewFor<CPDF_Boolean>(key, !!value);
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoBoolean(FPDF_DOCUMENT document,
                                unsigned int page_object_number,
                                FPDF_BYTESTRING application,
                                FPDF_BYTESTRING key,
                                FPDF_BOOL* value) {
  if (!value) {
    return false;
  }
  RetainPtr<const CPDF_Dictionary> page =
      GetPageDictionaryByObjectNumber(document, page_object_number);
  RetainPtr<const CPDF_Object> object =
      GetPrivateObject(page.Get(), application, key);
  if (!object || object->GetType() != CPDF_Object::Type::kBoolean) {
    return false;
  }
  *value = object->GetInteger() != 0;
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPagePieceInfoName(FPDF_DOCUMENT document,
                             unsigned int page_object_number,
                             FPDF_BYTESTRING application,
                             FPDF_BYTESTRING key,
                             FPDF_BYTESTRING value,
                             FPDF_WIDESTRING content_last_modified) {
  if (!key || !*key || !value || !*value) {
    return false;
  }
  MutablePieceInfo target = GetOrCreateMutablePieceInfo(
      document, page_object_number, application, content_last_modified);
  if (!target.private_dict) {
    return false;
  }
  target.private_dict->SetNewFor<CPDF_Name>(key, value);
  return true;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoName(FPDF_DOCUMENT document,
                             unsigned int page_object_number,
                             FPDF_BYTESTRING application,
                             FPDF_BYTESTRING key,
                             char* buffer,
                             unsigned long buflen) {
  RetainPtr<const CPDF_Dictionary> page =
      GetPageDictionaryByObjectNumber(document, page_object_number);
  RetainPtr<const CPDF_Object> object =
      GetPrivateObject(page.Get(), application, key);
  return object && object->GetType() == CPDF_Object::Type::kName
             ? CopyByteString(object->GetString().AsStringView(), buffer,
                              buflen)
             : 0;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPagePieceInfoStringArray(FPDF_DOCUMENT document,
                                    unsigned int page_object_number,
                                    FPDF_BYTESTRING application,
                                    FPDF_BYTESTRING key,
                                    const FPDF_WIDESTRING* values,
                                    unsigned long value_count,
                                    FPDF_WIDESTRING content_last_modified) {
  if (!key || !*key || (value_count > 0 && !values)) {
    return false;
  }
  for (unsigned long i = 0; i < value_count; ++i) {
    if (!UNSAFE_BUFFERS(values[i])) {
      return false;
    }
  }
  MutablePieceInfo target = GetOrCreateMutablePieceInfo(
      document, page_object_number, application, content_last_modified);
  if (!target.private_dict) {
    return false;
  }

  auto array = pdfium::MakeRetain<CPDF_Array>();
  for (unsigned long i = 0; i < value_count; ++i) {
    FPDF_WIDESTRING value = UNSAFE_BUFFERS(values[i]);
    array->AppendNew<CPDF_String>(
        UNSAFE_BUFFERS(WideStringFromFPDFWideString(value).AsStringView()));
  }
  target.private_dict->SetFor(key, std::move(array));
  return true;
}

FPDF_EXPORT int FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoStringArrayCount(FPDF_DOCUMENT document,
                                         unsigned int page_object_number,
                                         FPDF_BYTESTRING application,
                                         FPDF_BYTESTRING key) {
  RetainPtr<const CPDF_Dictionary> page =
      GetPageDictionaryByObjectNumber(document, page_object_number);
  RetainPtr<const CPDF_Object> object =
      GetPrivateObject(page.Get(), application, key);
  RetainPtr<const CPDF_Array> array = ToArray(std::move(object));
  if (!array) {
    return -1;
  }
  for (size_t i = 0; i < array->size(); ++i) {
    RetainPtr<const CPDF_Object> item = array->GetDirectObjectAt(i);
    if (!item || item->GetType() != CPDF_Object::Type::kString) {
      return -1;
    }
  }
  return static_cast<int>(array->size());
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoStringArrayAt(FPDF_DOCUMENT document,
                                      unsigned int page_object_number,
                                      FPDF_BYTESTRING application,
                                      FPDF_BYTESTRING key,
                                      int index,
                                      FPDF_WCHAR* buffer,
                                      unsigned long buflen) {
  if (index < 0) {
    return 0;
  }
  RetainPtr<const CPDF_Dictionary> page =
      GetPageDictionaryByObjectNumber(document, page_object_number);
  RetainPtr<const CPDF_Object> object =
      GetPrivateObject(page.Get(), application, key);
  RetainPtr<const CPDF_Array> array = ToArray(std::move(object));
  if (!array || index >= static_cast<int>(array->size())) {
    return 0;
  }
  RetainPtr<const CPDF_Object> item = array->GetDirectObjectAt(index);
  return item && item->GetType() == CPDF_Object::Type::kString
             ? CopyWideString(item->GetUnicodeText(), buffer, buflen)
             : 0;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_ClearPagePieceInfoKey(FPDF_DOCUMENT document,
                              unsigned int page_object_number,
                              FPDF_BYTESTRING application,
                              FPDF_BYTESTRING key,
                              FPDF_WIDESTRING content_last_modified) {
  if (!key || !*key) {
    return false;
  }

  RetainPtr<const CPDF_Dictionary> page =
      GetPageDictionaryByObjectNumber(document, page_object_number);
  if (!page || !application || !*application) {
    return false;
  }
  RetainPtr<const CPDF_Object> piece_info_object =
      page->GetDirectObjectFor(kPieceInfoKey);
  if (!piece_info_object) {
    return true;
  }
  const CPDF_Dictionary* piece_info = piece_info_object->AsDictionary();
  if (!piece_info) {
    return false;
  }
  RetainPtr<const CPDF_Object> app_object =
      piece_info->GetDirectObjectFor(application);
  if (!app_object) {
    return true;
  }
  const CPDF_Dictionary* app = app_object->AsDictionary();
  if (!app) {
    return false;
  }
  RetainPtr<const CPDF_Object> private_object =
      app->GetDirectObjectFor(kPrivateKey);
  if (!private_object) {
    return true;
  }
  const CPDF_Dictionary* private_dict = private_object->AsDictionary();
  if (!private_dict) {
    return false;
  }
  if (!private_dict->KeyExist(key)) {
    return true;
  }

  MutablePieceInfo target = GetOrCreateMutablePieceInfo(
      document, page_object_number, application, content_last_modified);
  if (!target.private_dict) {
    return false;
  }
  target.private_dict->RemoveFor(key);
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_ClearPagePieceInfoEntry(FPDF_DOCUMENT document,
                                unsigned int page_object_number,
                                FPDF_BYTESTRING application) {
  if (!application || !*application) {
    return false;
  }
  RetainPtr<const CPDF_Dictionary> const_page =
      GetPageDictionaryByObjectNumber(document, page_object_number);
  if (!const_page) {
    return false;
  }
  RetainPtr<const CPDF_Object> piece_info_object =
      const_page->GetDirectObjectFor(kPieceInfoKey);
  if (!piece_info_object) {
    return true;
  }
  const CPDF_Dictionary* const_piece_info = piece_info_object->AsDictionary();
  if (!const_piece_info) {
    return false;
  }
  if (!const_piece_info->KeyExist(application)) {
    return true;
  }

  RetainPtr<CPDF_Dictionary> page =
      GetMutablePageDictionaryByObjectNumber(document, page_object_number);
  if (!page) {
    return false;
  }
  RetainPtr<CPDF_Dictionary> piece_info =
      page->GetMutableDictFor(kPieceInfoKey);
  if (!piece_info) {
    return !page->KeyExist(kPieceInfoKey);
  }
  piece_info->RemoveFor(application);
  if (piece_info->size() == 0) {
    page->RemoveFor(kPieceInfoKey);
  }
  return true;
}
