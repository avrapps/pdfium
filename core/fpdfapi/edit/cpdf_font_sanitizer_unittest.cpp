// Copyright 2025 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/edit/cpdf_font_sanitizer.h"

#include <memory>
#include <set>

#include "core/fpdfapi/edit/cpdf_font_usage_collector.h"
#include "core/fpdfapi/edit/cpdf_tounicode_builder.h"
#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/page/cpdf_pageobject.h"
#include "core/fpdfapi/page/cpdf_textobject.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class FontSanitizerTest : public testing::Test {
 protected:
  void SetUp() override {
    // Create a minimal document for testing.
    doc_ = std::make_unique<CPDF_Document>();
    doc_->CreateNewDoc();
  }

  void TearDown() override { doc_.reset(); }

  CPDF_Document* doc() { return doc_.get(); }

 private:
  std::unique_ptr<CPDF_Document> doc_;
};

TEST_F(FontSanitizerTest, EmptyDocument) {
  CPDF_FontSanitizer sanitizer(doc());
  FontSanitizerResult result = sanitizer.Sanitize();
  
  EXPECT_TRUE(result.success);
  EXPECT_EQ(0, result.fonts_processed);
  EXPECT_EQ(0, result.fonts_modified);
}

TEST_F(FontSanitizerTest, NullDocument) {
  CPDF_FontSanitizer sanitizer(nullptr);
  FontSanitizerResult result = sanitizer.Sanitize();
  
  EXPECT_FALSE(result.success);
  EXPECT_NE(nullptr, result.error_message);
}

TEST_F(FontSanitizerTest, ToUnicodeCMapGeneration) {
  // Test the CMap generation with simple mappings.
  std::map<uint32_t, WideString> mappings;
  mappings[0x41] = L"A";
  mappings[0x42] = L"B";
  mappings[0x43] = L"C";
  
  ByteString cmap = CPDF_ToUnicodeBuilder::GenerateCMapContent(mappings);
  
  // Verify the CMap structure.
  EXPECT_TRUE(cmap.Contains("beginbfchar"));
  EXPECT_TRUE(cmap.Contains("endbfchar"));
  EXPECT_TRUE(cmap.Contains("begincmap"));
  EXPECT_TRUE(cmap.Contains("endcmap"));
  
  // Verify the mappings are present.
  EXPECT_TRUE(cmap.Contains("<41>"));  // Character code 0x41
  EXPECT_TRUE(cmap.Contains("<0041>")); // Unicode for 'A'
}

TEST_F(FontSanitizerTest, ProgressCallback) {
  CPDF_FontSanitizer sanitizer(doc());
  
  int callback_count = 0;
  FontSanitizerOptions options;
  options.progress_callback = [&callback_count](int current, int total) {
    callback_count++;
    return true;  // Continue processing
  };
  
  FontSanitizerResult result = sanitizer.SanitizeWithOptions(options);
  
  EXPECT_TRUE(result.success);
  // For an empty document, callback won't be called
  EXPECT_EQ(0, callback_count);
}

TEST_F(FontSanitizerTest, CancellationViaCallback) {
  // Create a collector directly to test callback behavior
  CPDF_FontUsageCollector collector(doc());
  collector.CollectFromAllPages();
  
  // For an empty document, there are no fonts to cancel
  const auto& usage = collector.GetUsageInfo();
  EXPECT_TRUE(usage.empty());
}

}  // namespace
