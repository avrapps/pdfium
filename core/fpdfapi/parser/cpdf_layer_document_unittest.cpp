// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/parser/cpdf_layer_document.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/page/cpdf_pagemodule.h"
#include "core/fpdfapi/parser/cpdf_base_document.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/cpdf_object.h"
#include "core/fpdfapi/parser/cpdf_parser.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fxcrt/cfx_read_only_span_stream.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/span.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class CPDFLayerDocumentTest : public testing::Test {
 protected:
  static void SetUpTestSuite() { pdfium::InitializePageModule(); }
  static void TearDownTestSuite() { pdfium::DestroyPageModule(); }
};

std::string BuildSimplePdf() {
  const std::vector<std::string> objects = {
      "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
      "2 0 obj\n<< /Type /Pages /Count 1 /Kids [3 0 R] >>\nendobj\n",
      "3 0 obj\n"
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] >>\n"
      "endobj\n",
  };

  std::ostringstream pdf;
  pdf << "%PDF-1.7\n";
  std::vector<size_t> offsets;
  for (const std::string& object : objects) {
    offsets.push_back(pdf.tellp());
    pdf << object;
  }

  const size_t xref_offset = pdf.tellp();
  pdf << "xref\n0 " << (objects.size() + 1) << "\n0000000000 65535 f \n";
  for (size_t offset : offsets) {
    pdf << std::setw(10) << std::setfill('0') << offset << " 00000 n \n";
  }
  pdf << "trailer\n<< /Size " << (objects.size() + 1)
      << " /Root 1 0 R >>\nstartxref\n"
      << xref_offset << "\n%%EOF\n";
  return pdf.str();
}

std::string BuildPdfWithDirectResources() {
  const std::vector<std::string> objects = {
      "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
      "2 0 obj\n<< /Type /Pages /Count 1 /Kids [3 0 R] >>\nendobj\n",
      "3 0 obj\n"
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100]\n"
      "   /Resources << /ProcSet [/PDF] >> >>\n"
      "endobj\n",
  };

  std::ostringstream pdf;
  pdf << "%PDF-1.7\n";
  std::vector<size_t> offsets;
  for (const std::string& object : objects) {
    offsets.push_back(pdf.tellp());
    pdf << object;
  }

  const size_t xref_offset = pdf.tellp();
  pdf << "xref\n0 " << (objects.size() + 1) << "\n0000000000 65535 f \n";
  for (size_t offset : offsets) {
    pdf << std::setw(10) << std::setfill('0') << offset << " 00000 n \n";
  }
  pdf << "trailer\n<< /Size " << (objects.size() + 1)
      << " /Root 1 0 R >>\nstartxref\n"
      << xref_offset << "\n%%EOF\n";
  return pdf.str();
}

RetainPtr<CFX_ReadOnlySpanStream> MakeStreamForString(const std::string& data) {
  return pdfium::MakeRetain<CFX_ReadOnlySpanStream>(
      pdfium::span(reinterpret_cast<const uint8_t*>(data.data()), data.size()));
}

RetainPtr<CPDF_BaseDocument> LoadBaseDocumentFromString(
    const std::string& data) {
  RetainPtr<CPDF_BaseDocument> document =
      pdfium::MakeRetain<CPDF_BaseDocument>();
  if (document->LoadBaseDoc(MakeStreamForString(data), "") !=
      CPDF_Parser::SUCCESS) {
    return nullptr;
  }
  return document;
}

RetainPtr<CPDF_Page> MakeLayerPage(CPDF_LayerDocument* layer, int page_index) {
  RetainPtr<const CPDF_Dictionary> page_dict =
      layer->GetPageDictionary(page_index);
  if (!page_dict) {
    return nullptr;
  }
  return pdfium::MakeRetain<CPDF_Page>(
      layer,
      pdfium::WrapRetain(const_cast<CPDF_Dictionary*>(page_dict.Get())));
}

}  // namespace

TEST_F(CPDFLayerDocumentTest, FreshLayerFallsThroughToFrozenBase) {
  const std::string pdf = BuildSimplePdf();
  RetainPtr<CPDF_BaseDocument> base = LoadBaseDocumentFromString(pdf);
  ASSERT_TRUE(base);

  auto layer =
      std::make_unique<CPDF_LayerDocument>(base, MakeStreamForString(pdf));

  EXPECT_EQ(CPDF_LayerDocument::OpenStatus::kSuccess, layer->ingest_status());
  EXPECT_TRUE(layer->IsLayerDocument());
  EXPECT_EQ(base->GetParser(), layer->GetParser());
  EXPECT_EQ(base->GetLastObjNum(), layer->GetLastObjNum());
  EXPECT_EQ(0u, layer->GetPromotedObjectCount());
  EXPECT_EQ(1, layer->GetPageCount());

  RetainPtr<const CPDF_Dictionary> page = layer->GetPageDictionary(0);
  ASSERT_TRUE(page);
  EXPECT_EQ(3u, page->GetObjNum());
  EXPECT_EQ(base->GetFrozenObjectForLayer(3).Get(), page.Get());
  EXPECT_EQ(base->GetUserPermissions(false), layer->GetUserPermissions(false));
  EXPECT_EQ(0u, layer->GetPromotedObjectCount());
}

TEST_F(CPDFLayerDocumentTest, DeleteBaseObjectIsNoOp) {
  const std::string pdf = BuildSimplePdf();
  RetainPtr<CPDF_BaseDocument> base = LoadBaseDocumentFromString(pdf);
  ASSERT_TRUE(base);
  auto layer =
      std::make_unique<CPDF_LayerDocument>(base, MakeStreamForString(pdf));

  ASSERT_TRUE(layer->GetIndirectObject(1));
  layer->DeleteIndirectObject(1);
  EXPECT_TRUE(layer->GetIndirectObject(1));
  EXPECT_EQ(0u, layer->GetPromotedObjectCount());
}

TEST_F(CPDFLayerDocumentTest, AppendedBytesFailClosedUntilDeltaIngestLands) {
  const std::string pdf = BuildSimplePdf();
  RetainPtr<CPDF_BaseDocument> base = LoadBaseDocumentFromString(pdf);
  ASSERT_TRUE(base);
  const std::string layer_bytes = pdf + "\n% appended delta placeholder\n";

  auto layer = std::make_unique<CPDF_LayerDocument>(
      base, MakeStreamForString(layer_bytes));

  EXPECT_EQ(CPDF_LayerDocument::OpenStatus::kMalformedDelta,
            layer->ingest_status());
  EXPECT_EQ(0u, layer->GetPromotedObjectCount());
}

TEST_F(CPDFLayerDocumentTest, GetMutableIndirectObjectPromotesFromBase) {
  const std::string pdf = BuildSimplePdf();
  RetainPtr<CPDF_BaseDocument> base = LoadBaseDocumentFromString(pdf);
  ASSERT_TRUE(base);
  auto layer =
      std::make_unique<CPDF_LayerDocument>(base, MakeStreamForString(pdf));

  RetainPtr<CPDF_Object> promoted = layer->GetMutableIndirectObject(1);
  ASSERT_TRUE(promoted);
  EXPECT_EQ(1u, promoted->GetObjNum());
  EXPECT_NE(base->GetFrozenObjectForLayer(1).Get(), promoted.Get());
  EXPECT_EQ(promoted.Get(), layer->FindPromotedObject(1).Get());
  EXPECT_EQ(1u, layer->GetPromotedObjectCount());
  EXPECT_FALSE(promoted->IsFrozen());
  EXPECT_TRUE(base->GetFrozenObjectForLayer(1)->IsFrozen());
}

TEST_F(CPDFLayerDocumentTest, PageMutableDictPromotesAndLeavesBaseFrozen) {
  const std::string pdf = BuildSimplePdf();
  RetainPtr<CPDF_BaseDocument> base = LoadBaseDocumentFromString(pdf);
  ASSERT_TRUE(base);
  auto layer =
      std::make_unique<CPDF_LayerDocument>(base, MakeStreamForString(pdf));

  auto page = MakeLayerPage(layer.get(), 0);
  ASSERT_TRUE(page);
  EXPECT_EQ(0u, layer->GetPromotedObjectCount());

  RetainPtr<CPDF_Dictionary> page_dict = page->GetMutableDict();
  ASSERT_TRUE(page_dict);
  EXPECT_EQ(3u, page_dict->GetObjNum());
  EXPECT_EQ(1u, layer->GetPromotedObjectCount());
  EXPECT_TRUE(layer->FindPromotedObject(3));
  EXPECT_NE(base->GetFrozenObjectForLayer(3).Get(), page_dict.Get());

  page_dict->SetNewFor<CPDF_Number>("Tier3Marker", 73);
  EXPECT_EQ(73, page->GetDict()->GetIntegerFor("Tier3Marker"));
  ASSERT_TRUE(base->GetFrozenObjectForLayer(3)->AsDictionary());
  EXPECT_FALSE(
      base->GetFrozenObjectForLayer(3)->AsDictionary()->KeyExist("Tier3Marker"));
}

TEST_F(CPDFLayerDocumentTest, PromotedReferencesResolveThroughLayerHolder) {
  const std::string pdf = BuildSimplePdf();
  RetainPtr<CPDF_BaseDocument> base = LoadBaseDocumentFromString(pdf);
  ASSERT_TRUE(base);
  auto layer =
      std::make_unique<CPDF_LayerDocument>(base, MakeStreamForString(pdf));

  auto page = MakeLayerPage(layer.get(), 0);
  ASSERT_TRUE(page);
  RetainPtr<CPDF_Dictionary> page_dict = page->GetMutableDict();
  ASSERT_TRUE(page_dict);

  RetainPtr<const CPDF_Reference> parent_ref =
      ToReference(page_dict->GetObjectFor("Parent"));
  ASSERT_TRUE(parent_ref);
  EXPECT_TRUE(parent_ref->HasIndirectObjectHolder());

  RetainPtr<CPDF_Dictionary> parent = page_dict->GetMutableDictFor("Parent");
  ASSERT_TRUE(parent);
  EXPECT_EQ("Pages", parent->GetNameFor("Type"));
  EXPECT_TRUE(layer->FindPromotedObject(2));
  EXPECT_EQ(2u, layer->GetPromotedObjectCount());
  EXPECT_FALSE(base->GetFrozenObjectForLayer(2)->AsDictionary()->KeyExist(
      "Tier3ParentMarker"));

  parent->SetNewFor<CPDF_Number>("Tier3ParentMarker", 91);
  EXPECT_EQ(91, layer->GetMutableIndirectObject(2)
                    ->AsMutableDictionary()
                    ->GetIntegerFor("Tier3ParentMarker"));
  EXPECT_FALSE(base->GetFrozenObjectForLayer(2)->AsDictionary()->KeyExist(
      "Tier3ParentMarker"));
}

TEST_F(CPDFLayerDocumentTest, CrossHandleReadRefreshesAfterPagePromotion) {
  const std::string pdf = BuildSimplePdf();
  RetainPtr<CPDF_BaseDocument> base = LoadBaseDocumentFromString(pdf);
  ASSERT_TRUE(base);
  auto layer =
      std::make_unique<CPDF_LayerDocument>(base, MakeStreamForString(pdf));

  auto page_a = MakeLayerPage(layer.get(), 0);
  auto page_b = MakeLayerPage(layer.get(), 0);
  ASSERT_TRUE(page_a);
  ASSERT_TRUE(page_b);

  page_a->GetMutableDict()->SetNewFor<CPDF_Number>("Foo", 1);
  EXPECT_EQ(1, page_b->GetDict()->GetIntegerFor("Foo"));
  EXPECT_EQ(1u, layer->GetPromotedObjectCount());

  page_b->GetMutableDict()->SetNewFor<CPDF_Number>("Bar", 2);
  EXPECT_EQ(2, page_a->GetDict()->GetIntegerFor("Bar"));
  EXPECT_EQ(1u, layer->GetPromotedObjectCount());
  EXPECT_FALSE(base->GetFrozenObjectForLayer(3)->AsDictionary()->KeyExist("Foo"));
  EXPECT_FALSE(base->GetFrozenObjectForLayer(3)->AsDictionary()->KeyExist("Bar"));
}

TEST_F(CPDFLayerDocumentTest, DirectResourcesPromoteOwningPage) {
  const std::string pdf = BuildPdfWithDirectResources();
  RetainPtr<CPDF_BaseDocument> base = LoadBaseDocumentFromString(pdf);
  ASSERT_TRUE(base);
  auto layer =
      std::make_unique<CPDF_LayerDocument>(base, MakeStreamForString(pdf));

  auto page = MakeLayerPage(layer.get(), 0);
  ASSERT_TRUE(page);
  RetainPtr<CPDF_Dictionary> resources = page->GetMutableResources();
  ASSERT_TRUE(resources);
  EXPECT_EQ(1u, layer->GetPromotedObjectCount());

  resources->SetNewFor<CPDF_Number>("Tier3ResourceMarker", 5);
  EXPECT_EQ(5, page->GetResources()->GetIntegerFor("Tier3ResourceMarker"));

  RetainPtr<const CPDF_Dictionary> base_page =
      base->GetFrozenObjectForLayer(3)->GetDict();
  ASSERT_TRUE(base_page);
  RetainPtr<const CPDF_Dictionary> base_resources =
      base_page->GetDictFor("Resources");
  ASSERT_TRUE(base_resources);
  EXPECT_FALSE(base_resources->KeyExist("Tier3ResourceMarker"));
}

#if DCHECK_IS_ON()
TEST_F(CPDFLayerDocumentTest, ParseIndirectObjectStillUnsupportedOnLayer) {
  const std::string pdf = BuildSimplePdf();
  RetainPtr<CPDF_BaseDocument> base = LoadBaseDocumentFromString(pdf);
  ASSERT_TRUE(base);
  auto layer =
      std::make_unique<CPDF_LayerDocument>(base, MakeStreamForString(pdf));

  EXPECT_DEATH_IF_SUPPORTED(layer->ParseIndirectObject(1), "");
}
#endif  // DCHECK_IS_ON()
