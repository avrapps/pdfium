// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/fpdf_parser_utility.h"
#include "core/fxge/cfx_defaultrenderdevice.h"
#include "fpdfsdk/cpdfsdk_helpers.h"
#include "public/epdf_form.h"
#include "public/fpdf_annot.h"
#include "public/fpdf_flatten.h"
#include "public/fpdf_save.h"
#include "public/fpdfview.h"
#include "testing/embedder_test.h"
#include "testing/embedder_test_constants.h"
#include "testing/fx_string_testhelpers.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/test_loader.h"
#include "testing/utils/file_util.h"
#include "testing/utils/path_service.h"

#include <string>
#include <vector>

using testing::HasSubstr;
using testing::Not;

namespace {

class FPDFFlattenEmbedderTest : public EmbedderTest {
 protected:
  struct LayerDocument {
    std::vector<uint8_t> bytes;
    EPDF_BASE_DOCUMENT base = nullptr;
    FPDF_DOCUMENT layer = nullptr;

    ~LayerDocument() {
      if (layer) {
        FPDF_CloseDocument(layer);
      }
      if (base) {
        EPDF_ReleaseBaseDocument(base);
      }
    }
  };

  bool OpenLayer(const char* file_name, LayerDocument* out) {
    const std::string path = PathService::GetTestFilePath(file_name);
    if (path.empty()) {
      return false;
    }
    out->bytes = GetFileContents(path.c_str());
    if (out->bytes.empty()) {
      return false;
    }
    out->base = EPDF_LoadMemBaseDocument(
        out->bytes.data(), static_cast<int>(out->bytes.size()), nullptr);
    if (!out->base) {
      return false;
    }
    EPDFLayerOpenStatus status = EPDFLayerOpenStatus_kOpenFailed;
    out->layer = EPDFLayer_OpenLayer(out->base, nullptr, nullptr, &status);
    return out->layer && status == EPDFLayerOpenStatus_kSuccess;
  }
};

}  // namespace

TEST_F(FPDFFlattenEmbedderTest, FlatNothing) {
  ASSERT_TRUE(OpenDocument("hello_world.pdf"));
  ScopedPage page = LoadScopedPage(0);
  EXPECT_TRUE(page);
  EXPECT_EQ(FLATTEN_NOTHINGTODO,
            FPDFPage_Flatten(page.get(), FLAT_NORMALDISPLAY));
}

TEST_F(FPDFFlattenEmbedderTest, FlatNormal) {
  ASSERT_TRUE(OpenDocument("annotiter.pdf"));
  ScopedPage page = LoadScopedPage(0);
  EXPECT_TRUE(page);
  EXPECT_EQ(FLATTEN_SUCCESS, FPDFPage_Flatten(page.get(), FLAT_NORMALDISPLAY));
}

TEST_F(FPDFFlattenEmbedderTest, FlatPrint) {
  ASSERT_TRUE(OpenDocument("annotiter.pdf"));
  ScopedPage page = LoadScopedPage(0);
  EXPECT_TRUE(page);
  EXPECT_EQ(FLATTEN_SUCCESS, FPDFPage_Flatten(page.get(), FLAT_PRINT));
}

TEST_F(FPDFFlattenEmbedderTest, FlattenSpecificAnnotationByHandle) {
  ASSERT_TRUE(OpenDocument("flatten_selective.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);
  ASSERT_EQ(6, FPDFPage_GetAnnotCount(page.get()));

  ScopedFPDFAnnotation target(EPDFPage_GetAnnotByObjectNumber(page.get(), 4u));
  ASSERT_TRUE(target);
  EXPECT_EQ(FLATTEN_FAIL, EPDFAnnot_Flatten(page.get(), target.get(), 99));
  EXPECT_EQ(FLATTEN_FAIL,
            EPDFAnnot_Flatten(page.get(), nullptr, FLAT_NORMALDISPLAY));

  ScopedFPDFAnnotation hidden(EPDFPage_GetAnnotByObjectNumber(page.get(), 5u));
  ASSERT_TRUE(hidden);
  EXPECT_EQ(FLATTEN_NOTHINGTODO,
            EPDFAnnot_Flatten(page.get(), hidden.get(), FLAT_NORMALDISPLAY));
  EXPECT_EQ(6, FPDFPage_GetAnnotCount(page.get()));

  ASSERT_EQ(FLATTEN_SUCCESS,
            EPDFAnnot_Flatten(page.get(), target.get(), FLAT_NORMALDISPLAY));
  EXPECT_EQ(5, FPDFPage_GetAnnotCount(page.get()));
  EXPECT_FALSE(EPDFPage_GetAnnotByObjectNumber(page.get(), 4u));
  ScopedFPDFAnnotation preserved(
      EPDFPage_GetAnnotByObjectNumber(page.get(), 5u));
  EXPECT_TRUE(preserved);

  ASSERT_TRUE(FPDF_SaveAsCopy(document(), this, 0));
  ASSERT_TRUE(OpenSavedDocument());
  FPDF_PAGE saved_page = LoadSavedPage(0);
  ASSERT_TRUE(saved_page);
  EXPECT_EQ(5, FPDFPage_GetAnnotCount(saved_page));
  EXPECT_FALSE(EPDFPage_GetAnnotByObjectNumber(saved_page, 4u));
  CloseSavedPage(saved_page);
}

TEST_F(FPDFFlattenEmbedderTest,
       FlattenPagePreservesUnpaintedAnnotationsAndDetachesWidget) {
  ASSERT_TRUE(OpenDocument("flatten_selective.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ASSERT_EQ(FLATTEN_SUCCESS, EPDFPage_Flatten(page.get(), FLAT_NORMALDISPLAY));
  // Loading a regular PDFium page synthesizes a default appearance for the
  // Text annotation (object 6), so it is paintable here as well.
  EXPECT_EQ(2, FPDFPage_GetAnnotCount(page.get()));
  EXPECT_FALSE(EPDFPage_GetAnnotByObjectNumber(page.get(), 4u));
  EXPECT_FALSE(EPDFPage_GetAnnotByObjectNumber(page.get(), 13u));
  EXPECT_FALSE(EPDFPage_GetAnnotByObjectNumber(page.get(), 16u));
  for (unsigned int object_number : {5u, 9u}) {
    ScopedFPDFAnnotation annotation(
        EPDFPage_GetAnnotByObjectNumber(page.get(), object_number));
    EXPECT_TRUE(annotation) << object_number;
  }

  CPDF_Document* pdf = CPDFDocumentFromFPDFDocument(document());
  ASSERT_TRUE(pdf);
  RetainPtr<const CPDF_Dictionary> page_dictionary = pdf->GetPageDictionary(0);
  ASSERT_TRUE(page_dictionary);
  EXPECT_TRUE(page_dictionary->KeyExist("MediaBox"));
  EXPECT_TRUE(page_dictionary->KeyExist("CropBox"));
  RetainPtr<const CPDF_Dictionary> resources =
      page_dictionary->GetDictFor("Resources");
  ASSERT_TRUE(resources);
  EXPECT_TRUE(resources->KeyExist("ExtGState"));
  EXPECT_TRUE(resources->KeyExist("XObject"));

  EPDF_FORM_MODEL model = EPDFForm_LoadModel(document());
  ASSERT_TRUE(model);
  ASSERT_EQ(1, EPDFForm_CountFields(model));
  EXPECT_EQ(0, EPDFForm_CountFieldWidgets(model, 0));
  EPDFForm_CloseModel(model);
}

TEST_F(FPDFFlattenEmbedderTest, FlattenPageHonorsPrintUsage) {
  ASSERT_TRUE(OpenDocument("flatten_selective.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);
  ASSERT_EQ(FLATTEN_SUCCESS, EPDFPage_Flatten(page.get(), FLAT_PRINT));
  EXPECT_EQ(4, FPDFPage_GetAnnotCount(page.get()));
  EXPECT_FALSE(EPDFPage_GetAnnotByObjectNumber(page.get(), 4u));
  EXPECT_FALSE(EPDFPage_GetAnnotByObjectNumber(page.get(), 13u));
  ScopedFPDFAnnotation normal_only(
      EPDFPage_GetAnnotByObjectNumber(page.get(), 16u));
  EXPECT_TRUE(normal_only);
}

TEST_F(FPDFFlattenEmbedderTest, FlattenMergedWidgetRemovesFieldTreeEntry) {
  ASSERT_TRUE(OpenDocument("text_form.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);
  ScopedFPDFAnnotation widget(FPDFPage_GetAnnot(page.get(), 0));
  ASSERT_TRUE(widget);
  ASSERT_EQ(4u, EPDFAnnot_GetObjectNumber(widget.get()));
  ASSERT_TRUE(EPDFAnnot_GenerateFormFieldAP(widget.get()));

  ASSERT_EQ(FLATTEN_SUCCESS,
            EPDFAnnot_Flatten(page.get(), widget.get(), FLAT_NORMALDISPLAY));
  EXPECT_EQ(0, FPDFPage_GetAnnotCount(page.get()));

  EPDF_FORM_MODEL model = EPDFForm_LoadModel(document());
  ASSERT_TRUE(model);
  EXPECT_EQ(0, EPDFForm_CountFields(model));
  EPDFForm_CloseModel(model);
}

TEST_F(FPDFFlattenEmbedderTest, FlattenPageIsLayerSafeAndDeltaDurable) {
  LayerDocument document;
  ASSERT_TRUE(OpenLayer("flatten_selective.pdf", &document));
  ASSERT_EQ(0ul, EPDFLayer_GetPromotedObjectCount(document.layer));

  ScopedFPDFPage page(FPDF_LoadPage(document.layer, 0));
  ASSERT_TRUE(page);
  ScopedFPDFAnnotation hidden(EPDFPage_GetAnnotByObjectNumber(page.get(), 5u));
  ASSERT_TRUE(hidden);
  EXPECT_EQ(FLATTEN_NOTHINGTODO,
            EPDFAnnot_Flatten(page.get(), hidden.get(), FLAT_NORMALDISPLAY));
  EXPECT_EQ(0ul, EPDFLayer_GetPromotedObjectCount(document.layer));

  ASSERT_EQ(FLATTEN_SUCCESS, EPDFPage_Flatten(page.get(), FLAT_NORMALDISPLAY));
  EXPECT_TRUE(EPDFLayer_IsObjectPromoted(document.layer, 3u));
  EXPECT_TRUE(EPDFLayer_IsObjectPromoted(document.layer, 12u));
  EXPECT_TRUE(EPDFLayer_IsObjectPromoted(document.layer, 13u));
  EXPECT_FALSE(EPDFLayer_IsObjectPromoted(document.layer, 4u));
  EXPECT_FALSE(EPDFLayer_IsObjectPromoted(document.layer, 7u));
  EXPECT_FALSE(EPDFLayer_IsObjectPromoted(document.layer, 14u));
  EXPECT_FALSE(EPDFLayer_IsObjectPromoted(document.layer, 15u));
  EXPECT_FALSE(EPDFLayer_IsObjectPromoted(document.layer, 17u));

  EXPECT_EQ(3, FPDFPage_GetAnnotCount(page.get()));

  ClearString();
  EPDFLayerSaveStatus save_status = EPDFLayerSaveStatus_kSaveFailed;
  ASSERT_TRUE(EPDFLayer_SaveDelta(document.layer, this, &save_status));
  ASSERT_EQ(EPDFLayerSaveStatus_kSuccess, save_status);
  const std::string delta = GetString();
  ASSERT_FALSE(delta.empty());

  TestLoader loader(pdfium::as_bytes(pdfium::span(delta.data(), delta.size())));
  FPDF_FILEACCESS access = {};
  access.m_FileLen = static_cast<unsigned long>(delta.size());
  access.m_GetBlock = TestLoader::GetBlock;
  access.m_Param = &loader;
  EPDFLayerOpenStatus open_status = EPDFLayerOpenStatus_kOpenFailed;
  ScopedFPDFDocument replay(
      EPDFLayer_OpenLayer(document.base, &access, nullptr, &open_status));
  ASSERT_TRUE(replay);
  ASSERT_EQ(EPDFLayerOpenStatus_kSuccess, open_status);

  ScopedFPDFPage replay_page(FPDF_LoadPage(replay.get(), 0));
  ASSERT_TRUE(replay_page);
  EXPECT_EQ(3, FPDFPage_GetAnnotCount(replay_page.get()));
  EPDF_FORM_MODEL model = EPDFForm_LoadModel(replay.get());
  ASSERT_TRUE(model);
  ASSERT_EQ(1, EPDFForm_CountFields(model));
  EXPECT_EQ(0, EPDFForm_CountFieldWidgets(model, 0));
  EPDFForm_CloseModel(model);
}

TEST_F(FPDFFlattenEmbedderTest, FlattenReadsAlreadyPromotedAnnotationState) {
  LayerDocument document;
  ASSERT_TRUE(OpenLayer("flatten_selective.pdf", &document));
  ScopedFPDFPage page(FPDF_LoadPage(document.layer, 0));
  ASSERT_TRUE(page);
  ScopedFPDFAnnotation target(EPDFPage_GetAnnotByObjectNumber(page.get(), 4u));
  ASSERT_TRUE(target);
  CPDF_Document* pdf = CPDFDocumentFromFPDFDocument(document.layer);
  ASSERT_TRUE(pdf);
  RetainPtr<CPDF_Dictionary> annotation =
      ToDictionary(pdf->GetMutableIndirectObject(4u));
  ASSERT_TRUE(annotation);
  annotation->SetNewFor<CPDF_Number>("F", FPDF_ANNOT_FLAG_HIDDEN);
  ASSERT_TRUE(EPDFLayer_IsObjectPromoted(document.layer, 4u));
  ASSERT_FALSE(EPDFLayer_IsObjectPromoted(document.layer, 3u));

  EXPECT_EQ(FLATTEN_NOTHINGTODO,
            EPDFAnnot_Flatten(page.get(), target.get(), FLAT_NORMALDISPLAY));
  EXPECT_EQ(1ul, EPDFLayer_GetPromotedObjectCount(document.layer));
  EXPECT_FALSE(EPDFLayer_IsObjectPromoted(document.layer, 3u));
}

TEST_F(FPDFFlattenEmbedderTest, FlattenDirectAnnotationByHandle) {
  ScopedFPDFDocument document(FPDF_CreateNewDocument());
  ASSERT_TRUE(document);
  ScopedFPDFPage page(FPDFPage_New(document.get(), 0, 100, 100));
  ASSERT_TRUE(page);
  ScopedFPDFPage other_page(FPDFPage_New(document.get(), 1, 100, 100));
  ASSERT_TRUE(other_page);

  ScopedFPDFAnnotation annotation(
      FPDFPage_CreateAnnot(page.get(), FPDF_ANNOT_INK));
  ASSERT_TRUE(annotation);
  ASSERT_EQ(0u, EPDFAnnot_GetObjectNumber(annotation.get()));
  const FS_RECTF rectangle = {10.0f, 40.0f, 40.0f, 10.0f};
  ASSERT_TRUE(FPDFAnnot_SetRect(annotation.get(), &rectangle));
  ScopedFPDFWideString appearance = GetFPDFWideString(L"0 0 10 10 re f");
  ASSERT_TRUE(FPDFAnnot_SetAP(
      annotation.get(), FPDF_ANNOT_APPEARANCEMODE_NORMAL, appearance.get()));

  EXPECT_EQ(FLATTEN_FAIL, EPDFAnnot_Flatten(other_page.get(), annotation.get(),
                                            FLAT_NORMALDISPLAY));
  ASSERT_EQ(FLATTEN_SUCCESS, EPDFAnnot_Flatten(page.get(), annotation.get(),
                                               FLAT_NORMALDISPLAY));
  EXPECT_EQ(0, FPDFPage_GetAnnotCount(page.get()));
}

TEST_F(FPDFFlattenEmbedderTest, FlatWithBadFont) {
  ASSERT_TRUE(OpenDocument("344775293.pdf"));
  ScopedPage page = LoadScopedPage(0);
  EXPECT_TRUE(page);

  FORM_OnLButtonDown(form_handle(), page.get(), 0, 20, 30);
  FORM_OnLButtonUp(form_handle(), page.get(), 0, 20, 30);

  EXPECT_EQ(FLATTEN_SUCCESS, FPDFPage_Flatten(page.get(), FLAT_PRINT));
  EXPECT_TRUE(FPDF_SaveAsCopy(document(), this, 0));

  EXPECT_THAT(GetString(), Not(HasSubstr("/PDFDocEncoding")));
}

TEST_F(FPDFFlattenEmbedderTest, FlatWithFontNoBaseEncoding) {
  ASSERT_TRUE(OpenDocument("363015187.pdf"));
  ScopedPage page = LoadScopedPage(0);
  EXPECT_TRUE(page);

  EXPECT_EQ(FLATTEN_SUCCESS, FPDFPage_Flatten(page.get(), FLAT_PRINT));
  EXPECT_TRUE(FPDF_SaveAsCopy(document(), this, 0));

  EXPECT_THAT(GetString(), HasSubstr("/Differences"));
}

TEST_F(FPDFFlattenEmbedderTest, Bug861842) {
  ASSERT_TRUE(OpenDocument("bug_861842.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFBitmap bitmap = RenderLoadedPageWithFlags(page.get(), FPDF_ANNOT);
  CompareBitmapWithExpectationSuffix(bitmap.get(), "bug_861842");

  EXPECT_EQ(FLATTEN_SUCCESS, FPDFPage_Flatten(page.get(), FLAT_PRINT));
  EXPECT_TRUE(FPDF_SaveAsCopy(document(), this, 0));

  // TODO(crbug.com/861842): This should not render blank.
  VerifySavedDocumentWithExpectationSuffix("blank_100x120");
}

TEST_F(FPDFFlattenEmbedderTest, Bug889099) {
  ASSERT_TRUE(OpenDocument("bug_889099.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  // The original document has a malformed media box; the height is -400.
  ScopedFPDFBitmap bitmap = RenderLoadedPageWithFlags(page.get(), FPDF_ANNOT);
  CompareBitmapWithExpectationSuffix(bitmap.get(), "bug_889099");

  EXPECT_EQ(FLATTEN_SUCCESS, FPDFPage_Flatten(page.get(), FLAT_PRINT));
  EXPECT_TRUE(FPDF_SaveAsCopy(document(), this, 0));

  VerifySavedDocumentWithExpectationSuffix("bug_889099_flattened");
}

TEST_F(FPDFFlattenEmbedderTest, Bug890322) {
  ASSERT_TRUE(OpenDocument("bug_890322.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFBitmap bitmap = RenderLoadedPageWithFlags(page.get(), FPDF_ANNOT);
  CompareBitmapWithExpectationSuffix(bitmap.get(), pdfium::kBug890322Png);

  EXPECT_EQ(FLATTEN_SUCCESS, FPDFPage_Flatten(page.get(), FLAT_PRINT));
  EXPECT_TRUE(FPDF_SaveAsCopy(document(), this, 0));

  VerifySavedDocumentWithExpectationSuffix(pdfium::kBug890322Png);
}

TEST_F(FPDFFlattenEmbedderTest, Bug896366) {
  ASSERT_TRUE(OpenDocument("bug_896366.pdf"));
  ScopedPage page = LoadScopedPage(0);
  ASSERT_TRUE(page);

  ScopedFPDFBitmap bitmap = RenderLoadedPageWithFlags(page.get(), FPDF_ANNOT);
  CompareBitmapWithExpectationSuffix(bitmap.get(), "bug_896366");

  EXPECT_EQ(FLATTEN_SUCCESS, FPDFPage_Flatten(page.get(), FLAT_PRINT));
  EXPECT_TRUE(FPDF_SaveAsCopy(document(), this, 0));

  VerifySavedDocumentWithExpectationSuffix("bug_896366");
}
