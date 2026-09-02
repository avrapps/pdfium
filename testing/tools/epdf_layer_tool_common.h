// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef TESTING_TOOLS_EPDF_LAYER_TOOL_COMMON_H_
#define TESTING_TOOLS_EPDF_LAYER_TOOL_COMMON_H_

#include <stddef.h>
#include <stdint.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "public/fpdf_save.h"
#include "public/fpdfview.h"

namespace epdf_layer_tool {

struct MemoryFile {
  explicit MemoryFile(std::vector<uint8_t> input)
      : owned_bytes(std::move(input)), bytes(&owned_bytes) {
    InitAccess();
  }

  explicit MemoryFile(const std::vector<uint8_t>* input) : bytes(input) {
    InitAccess();
  }

  void InitAccess() {
    access.m_FileLen = static_cast<unsigned long>(bytes->size());
    access.m_GetBlock = &MemoryFile::GetBlock;
    access.m_Param = this;
  }

  FPDF_FILEACCESS* file_access() { return &access; }

  static int GetBlock(void* param,
                      unsigned long pos,
                      unsigned char* buf,
                      unsigned long size) {
    MemoryFile* file = static_cast<MemoryFile*>(param);
    if (!file || !file->bytes || pos > file->bytes->size() ||
        size > file->bytes->size() - pos) {
      return 0;
    }
    memcpy(buf, file->bytes->data() + pos, size);
    return 1;
  }

  std::vector<uint8_t> owned_bytes;
  const std::vector<uint8_t>* bytes = nullptr;
  FPDF_FILEACCESS access = {};
};

struct StringWriter : FPDF_FILEWRITE {
  StringWriter() {
    version = 1;
    WriteBlock = &StringWriter::WriteBlockCallback;
  }

  void Clear() { data.clear(); }

  static int WriteBlockCallback(FPDF_FILEWRITE* file_write,
                                const void* buffer,
                                unsigned long size) {
    StringWriter* writer = static_cast<StringWriter*>(file_write);
    writer->data.append(static_cast<const char*>(buffer), size);
    return 1;
  }

  std::string data;
};

inline bool ReadFile(const std::string& path, std::vector<uint8_t>* out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  file.seekg(0, std::ios::end);
  const std::streamoff length = file.tellg();
  if (length < 0) {
    return false;
  }
  file.seekg(0, std::ios::beg);
  out->resize(static_cast<size_t>(length));
  return out->empty() ||
         file.read(reinterpret_cast<char*>(out->data()), length).good();
}

inline std::vector<uint8_t> MaterializeLayerBytes(
    const std::vector<uint8_t>& base_bytes,
    const std::string& delta) {
  std::vector<uint8_t> materialized = base_bytes;
  materialized.insert(materialized.end(), delta.begin(), delta.end());
  return materialized;
}

inline void PrintUsage(const char* argv0, const char* extra) {
  std::fprintf(stderr, "Usage: %s <pdf_path> %s\n", argv0, extra);
}

}  // namespace epdf_layer_tool

#endif  // TESTING_TOOLS_EPDF_LAYER_TOOL_COMMON_H_
