// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/parser/cpdf_base_document.h"

#include <cstdint>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/fpdfapi/page/cpdf_pagemodule.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/cpdf_string.h"
#include "core/fxcrt/cfx_read_only_span_stream.h"
#include "core/fxcrt/check.h"
#include "core/fxcrt/data_vector.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/span.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class CPDFBaseDocumentTest : public testing::Test {
 protected:
  static void SetUpTestSuite() { pdfium::InitializePageModule(); }
  static void TearDownTestSuite() { pdfium::DestroyPageModule(); }
};

RetainPtr<CPDF_BaseDocument> LoadBaseDocumentFromString(
    const std::string& data) {
  RetainPtr<CPDF_BaseDocument> document =
      pdfium::MakeRetain<CPDF_BaseDocument>();
  auto stream = pdfium::MakeRetain<CFX_ReadOnlySpanStream>(
      pdfium::span(reinterpret_cast<const uint8_t*>(data.data()),
                   data.size()));
  if (document->LoadBaseDoc(std::move(stream), "") != CPDF_Parser::SUCCESS) {
    return nullptr;
  }
  return document;
}

size_t CountIndirectObjects(const CPDF_IndirectObjectHolder& holder) {
  return static_cast<size_t>(std::distance(holder.begin(), holder.end()));
}

std::string BuildPdfWithOrphanObject() {
  const std::vector<std::string> objects = {
      "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
      "2 0 obj\n<< /Type /Pages /Count 1 /Kids [3 0 R] >>\nendobj\n",
      "3 0 obj\n"
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] >>\n"
      "endobj\n",
      "4 0 obj\n<< /Orphan true >>\nendobj\n",
  };

  std::ostringstream pdf;
  pdf << "%PDF-1.7\n";
  std::vector<size_t> offsets;
  for (const std::string& object : objects) {
    offsets.push_back(pdf.tellp());
    pdf << object;
  }

  const size_t xref_offset = pdf.tellp();
  pdf << "xref\n0 " << (objects.size() + 1)
      << "\n0000000000 65535 f \n";
  for (size_t offset : offsets) {
    pdf << std::setw(10) << std::setfill('0') << offset << " 00000 n \n";
  }
  pdf << "trailer\n<< /Size " << (objects.size() + 1)
      << " /Root 1 0 R >>\nstartxref\n"
      << xref_offset << "\n%%EOF\n";
  return pdf.str();
}

}  // namespace

TEST_F(CPDFBaseDocumentTest, LoadFreezesReachableGraph) {
  const std::string pdf = BuildPdfWithOrphanObject();
  RetainPtr<CPDF_BaseDocument> document = LoadBaseDocumentFromString(pdf);
  ASSERT_TRUE(document);

  const size_t initial_object_count = CountIndirectObjects(*document);
  EXPECT_TRUE(document->IsHolderFrozen());
  EXPECT_GT(initial_object_count, 0u);

  ASSERT_TRUE(document->GetPageDictionary(0));
  EXPECT_EQ(initial_object_count, CountIndirectObjects(*document));
  ASSERT_TRUE(document->GetPageDictionary(0));
  EXPECT_EQ(initial_object_count, CountIndirectObjects(*document));
}

TEST_F(CPDFBaseDocumentTest, IsFrozenVisibleThroughConstObject) {
  auto object = pdfium::MakeRetain<CPDF_Dictionary>();
  object->Freeze();
  const CPDF_Object* const_object = object.Get();
  EXPECT_TRUE(const_object->IsFrozen());
}

TEST_F(CPDFBaseDocumentTest, CloneOfFrozenObjectIsMutable) {
  auto dict = pdfium::MakeRetain<CPDF_Dictionary>();
  RetainPtr<CPDF_Array> nested = dict->SetNewFor<CPDF_Array>("Kids");
  nested->AppendNew<CPDF_String>("child");
  dict->Freeze();

  RetainPtr<CPDF_Dictionary> clone = ToDictionary(dict->Clone());
  ASSERT_TRUE(clone);
  EXPECT_FALSE(clone->IsFrozen());

  RetainPtr<CPDF_Array> cloned_kids = clone->GetMutableArrayFor("Kids");
  ASSERT_TRUE(cloned_kids);
  EXPECT_FALSE(cloned_kids->IsFrozen());
  EXPECT_FALSE(cloned_kids->GetMutableObjectAt(0)->IsFrozen());

  clone->SetNewFor<CPDF_Number>("Mutable", 1);
  cloned_kids->AppendNew<CPDF_Number>(2);
}

TEST_F(CPDFBaseDocumentTest, RetainableRefCountSanity) {
  RetainPtr<CPDF_BaseDocument> document =
      pdfium::MakeRetain<CPDF_BaseDocument>();
  EXPECT_TRUE(document->HasOneRef());
  RetainPtr<CPDF_BaseDocument> second_reference = document;
  EXPECT_FALSE(document->HasOneRef());
  second_reference.Reset();
  EXPECT_TRUE(document->HasOneRef());
}

#if DCHECK_IS_ON()
TEST_F(CPDFBaseDocumentTest, HolderMutatorsDcheckAfterFreeze) {
  CPDF_IndirectObjectHolder holder;
  holder.NewIndirect<CPDF_Dictionary>();
  holder.Freeze();

  EXPECT_DEATH_IF_SUPPORTED(holder.NewIndirect<CPDF_Dictionary>(), "");
  auto replacement = pdfium::MakeRetain<CPDF_Dictionary>();
  replacement->SetGenNum(1);
  EXPECT_DEATH_IF_SUPPORTED(holder.ReplaceIndirectObjectIfHigherGeneration(
                                1, std::move(replacement)),
                            "");
  EXPECT_DEATH_IF_SUPPORTED(holder.DeleteIndirectObject(1), "");
}

TEST_F(CPDFBaseDocumentTest, ObjectMutatorsDcheckAfterFreeze) {
  auto dict = pdfium::MakeRetain<CPDF_Dictionary>();
  RetainPtr<CPDF_Array> array = dict->SetNewFor<CPDF_Array>("Array");
  array->AppendNew<CPDF_Number>(0);
  RetainPtr<CPDF_String> string =
      dict->SetNewFor<CPDF_String>("String", "value");
  RetainPtr<CPDF_Stream> stream =
      pdfium::MakeRetain<CPDF_Stream>(pdfium::span<const uint8_t>());
  dict->Freeze();
  stream->Freeze();

  EXPECT_DEATH_IF_SUPPORTED(dict->SetNewFor<CPDF_Number>("New", 1), "");
  EXPECT_DEATH_IF_SUPPORTED(dict->RemoveFor("String"), "");
  EXPECT_DEATH_IF_SUPPORTED(array->AppendNew<CPDF_Number>(1), "");
  EXPECT_DEATH_IF_SUPPORTED(array->InsertNewAt<CPDF_Number>(0, 1), "");
  EXPECT_DEATH_IF_SUPPORTED(array->SetNewAt<CPDF_Number>(0, 1), "");
  EXPECT_DEATH_IF_SUPPORTED(array->RemoveAt(0), "");
  EXPECT_DEATH_IF_SUPPORTED(array->Clear(), "");
  EXPECT_DEATH_IF_SUPPORTED(stream->SetData(pdfium::span<const uint8_t>()),
                            "");
  EXPECT_DEATH_IF_SUPPORTED(
      stream->SetDataAndRemoveFilter(pdfium::span<const uint8_t>()), "");
  EXPECT_DEATH_IF_SUPPORTED(stream->TakeData(DataVector<uint8_t>()), "");
  EXPECT_DEATH_IF_SUPPORTED(string->SetString("changed"), "");
}

TEST_F(CPDFBaseDocumentTest, ReadMissAfterFreezeDchecks) {
  const std::string pdf = BuildPdfWithOrphanObject();
  RetainPtr<CPDF_BaseDocument> document = LoadBaseDocumentFromString(pdf);
  ASSERT_TRUE(document);
  EXPECT_TRUE(document->IsHolderFrozen());
  EXPECT_FALSE(document->GetFrozenObjectForLayer(4));

  EXPECT_DEATH_IF_SUPPORTED(document->GetOrParseIndirectObject(4), "");
}
#endif  // DCHECK_IS_ON()
