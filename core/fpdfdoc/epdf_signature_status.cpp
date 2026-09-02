// Copyright 2020 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfdoc/epdf_signature_status.h"

std::mutex& GetSignatureStatusMutex() {
  static std::mutex* mutex = new std::mutex;
  return *mutex;
}

std::unordered_map<const CPDF_Dictionary*, int>& GetSignatureStatusMap() {
  static auto* map = new std::unordered_map<const CPDF_Dictionary*, int>;
  return *map;
}

void EPDF_SetSignatureStatus(const CPDF_Dictionary* sig_dict, int status) {
  if (!sig_dict) {
    return;
  }
  std::lock_guard<std::mutex> lock(GetSignatureStatusMutex());
  GetSignatureStatusMap()[sig_dict] = status;
}

int EPDF_GetSignatureStatus(const CPDF_Dictionary* sig_dict) {
  if (!sig_dict) {
    return kEpdfSignatureStatusUnknown;
  }
  std::lock_guard<std::mutex> lock(GetSignatureStatusMutex());
  const auto& map = GetSignatureStatusMap();
  auto it = map.find(sig_dict);
  return it == map.end() ? kEpdfSignatureStatusUnknown : it->second;
}
