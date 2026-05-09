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
#include "core/fpdfapi/parser/cpdf_object.h"
#include "core/fpdfapi/parser/cpdf_parser.h"
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

#if DCHECK_IS_ON()
TEST_F(CPDFLayerDocumentTest, MutatorsDcheckUntilCowSliceLands) {
  const std::string pdf = BuildSimplePdf();
  RetainPtr<CPDF_BaseDocument> base = LoadBaseDocumentFromString(pdf);
  ASSERT_TRUE(base);
  auto layer =
      std::make_unique<CPDF_LayerDocument>(base, MakeStreamForString(pdf));

  EXPECT_DEATH_IF_SUPPORTED(layer->GetMutableIndirectObject(1), "");
  EXPECT_DEATH_IF_SUPPORTED(layer->ParseIndirectObject(1), "");

  RetainPtr<const CPDF_Dictionary> page_dict = layer->GetPageDictionary(0);
  ASSERT_TRUE(page_dict);
  auto page = pdfium::MakeRetain<CPDF_Page>(
      layer.get(),
      pdfium::WrapRetain(const_cast<CPDF_Dictionary*>(page_dict.Get())));
  EXPECT_EQ(0u, layer->GetPromotedObjectCount());
  EXPECT_DEATH_IF_SUPPORTED(page->GetMutableDict(), "");
}
#endif  // DCHECK_IS_ON()
