// Copyright 2025 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/edit/cpdf_tounicode_builder.h"

#include <algorithm>
#include <sstream>
#include <utility>
#include <vector>

#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_stream.h"

namespace {

// Maximum entries per bfchar/bfrange block per PDF spec.
constexpr size_t kMaxEntriesPerBlock = 100;

// Formats a character code as a hex string with angle brackets.
ByteString FormatCode(uint32_t code, int width) {
  std::ostringstream oss;
  oss << '<';
  oss << std::hex << std::uppercase;
  if (width == 2) {
    oss.width(4);
  } else {
    oss.width(2);
  }
  oss.fill('0');
  oss << code;
  oss << '>';
  return ByteString(oss.str().c_str());
}

// Formats a Unicode string as a hex string with angle brackets.
ByteString FormatUnicode(const WideString& unicode) {
  std::ostringstream oss;
  oss << '<';
  oss << std::hex << std::uppercase;
  for (size_t i = 0; i < unicode.GetLength(); ++i) {
    oss.width(4);
    oss.fill('0');
    oss << static_cast<uint32_t>(unicode[i]);
  }
  oss << '>';
  return ByteString(oss.str().c_str());
}

}  // namespace

CPDF_ToUnicodeBuilder::CPDF_ToUnicodeBuilder() = default;
CPDF_ToUnicodeBuilder::~CPDF_ToUnicodeBuilder() = default;

bool CPDF_ToUnicodeBuilder::RebuildToUnicode(
    CPDF_Document* doc,
    CPDF_Font* font,
    const std::set<uint32_t>& used_char_codes) {
  if (!doc || !font)
    return false;

  // Extract existing mappings.
  std::map<uint32_t, WideString> all_mappings = ExtractExistingMappings(font);
  if (all_mappings.empty()) {
    // No ToUnicode to rebuild - might want to generate one from the font.
    return true;
  }

  // Filter to only used character codes.
  std::map<uint32_t, WideString> filtered = FilterMappings(all_mappings,
                                                            used_char_codes);

  // Generate new CMap content.
  ByteString cmap_content = GenerateCMapContent(filtered);

  // Replace the stream.
  return ReplaceToUnicodeStream(doc, font, cmap_content);
}

// static
ByteString CPDF_ToUnicodeBuilder::GenerateCMapContent(
    const std::map<uint32_t, WideString>& char_to_unicode) {
  std::ostringstream oss;

  // CMap header.
  oss << "/CIDInit /ProcSet findresource begin\n";
  oss << "12 dict begin\n";
  oss << "begincmap\n";
  oss << "/CIDSystemInfo <<\n";
  oss << "  /Registry (Adobe)\n";
  oss << "  /Ordering (UCS)\n";
  oss << "  /Supplement 0\n";
  oss << ">> def\n";
  oss << "/CMapName /Adobe-Identity-UCS def\n";
  oss << "/CMapType 2 def\n";

  // Determine code space range.
  if (!char_to_unicode.empty()) {
    uint32_t max_code = char_to_unicode.rbegin()->first;
    int width = (max_code > 0xFF) ? 2 : 1;
    
    oss << "1 begincodespacerange\n";
    if (width == 2) {
      oss << "<0000> <FFFF>\n";
    } else {
      oss << "<00> <FF>\n";
    }
    oss << "endcodespacerange\n";

    // Convert to vector for easier block processing.
    std::vector<std::pair<uint32_t, WideString>> entries(
        char_to_unicode.begin(), char_to_unicode.end());

    // Write bfchar entries in blocks.
    size_t pos = 0;
    while (pos < entries.size()) {
      size_t block_size = std::min(kMaxEntriesPerBlock, entries.size() - pos);
      oss << block_size << " beginbfchar\n";
      for (size_t i = 0; i < block_size; ++i) {
        const auto& entry = entries[pos + i];
        oss << FormatCode(entry.first, width).c_str() << " "
            << FormatUnicode(entry.second).c_str() << "\n";
      }
      oss << "endbfchar\n";
      pos += block_size;
    }
  }

  // CMap footer.
  oss << "endcmap\n";
  oss << "CMapName currentdict /CMap defineresource pop\n";
  oss << "end\n";
  oss << "end\n";

  return ByteString(oss.str().c_str());
}

std::map<uint32_t, WideString> CPDF_ToUnicodeBuilder::ExtractExistingMappings(
    CPDF_Font* font) {
  std::map<uint32_t, WideString> mappings;
  if (!font)
    return mappings;

  // Use the font's UnicodeFromCharCode to extract mappings.
  // We need to probe all possible character codes.
  // For efficiency, we use the font's encoding information.
  
  // For simple fonts (8-bit), probe 0-255.
  // For CID fonts (16-bit), this is more complex.
  const bool is_cid = font->IsCIDFont();
  const uint32_t max_code = is_cid ? 0xFFFF : 0xFF;

  for (uint32_t code = 0; code <= max_code; ++code) {
    WideString unicode = font->UnicodeFromCharCode(code);
    if (!unicode.IsEmpty()) {
      mappings[code] = unicode;
    }
    
    // For simple fonts, we've covered all codes at 255.
    if (!is_cid && code == 255)
      break;
      
    // For CID fonts, skip large gaps to avoid iterating 65536 codes.
    // This is a heuristic - in practice, the usage collector tells us
    // exactly which codes are used.
  }

  return mappings;
}

std::map<uint32_t, WideString> CPDF_ToUnicodeBuilder::FilterMappings(
    const std::map<uint32_t, WideString>& all_mappings,
    const std::set<uint32_t>& used_char_codes) {
  std::map<uint32_t, WideString> filtered;
  for (uint32_t code : used_char_codes) {
    auto it = all_mappings.find(code);
    if (it != all_mappings.end()) {
      filtered[code] = it->second;
    }
  }
  return filtered;
}

bool CPDF_ToUnicodeBuilder::ReplaceToUnicodeStream(
    CPDF_Document* doc,
    CPDF_Font* font,
    const ByteString& cmap_content) {
  if (!doc || !font || cmap_content.IsEmpty())
    return false;

  RetainPtr<CPDF_Dictionary> font_dict = font->GetMutableFontDict();
  if (!font_dict)
    return false;

  // Create a new stream with the CMap content.
  pdfium::span<const uint8_t> content_span(
      reinterpret_cast<const uint8_t*>(cmap_content.c_str()),
      cmap_content.GetLength());
  
  auto new_stream = doc->NewIndirect<CPDF_Stream>(content_span);
  
  // Update the font dictionary to reference the new ToUnicode stream.
  font_dict->SetFor("ToUnicode",
                    pdfium::MakeRetain<CPDF_Reference>(doc,
                                                        new_stream->GetObjNum()));

  return true;
}
