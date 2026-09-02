// Copyright 2020 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFDOC_EPDF_SIGNATURE_STATUS_H_
#define CORE_FPDFDOC_EPDF_SIGNATURE_STATUS_H_

#include <mutex>
#include <unordered_map>

#include "core/fpdfapi/parser/cpdf_dictionary.h"

// Experimental EmbedPDF Extension: runtime-only digital-signature validation
// status, stored in core so both the appearance renderer (core/fpdfdoc) and the
// public API (fpdfsdk/fpdf_signature.cpp) can share it.
//
// Status is an int matching the public enum FPDF_SIGNATURE_VALIDATION_STATUS:
//   0 = UNKNOWN (default), 1 = VALID, 2 = INVALID.
//
// This is VIEWER/RUNTIME state only: never written to the PDF, never
// serialized, never stored in any signature dictionary. Keyed by the signature
// field dictionary pointer (what an FPDF_SIGNATURE handle wraps); those dicts
// are owned by the CPDF_Document and valid until close. Multiple signatures
// coexist independently. Mutex-guarded; maps intentionally leaked behind
// function-local statics to avoid the static destruction order fiasco.

inline constexpr int kEpdfSignatureStatusUnknown = 0;
inline constexpr int kEpdfSignatureStatusValid = 1;
inline constexpr int kEpdfSignatureStatusInvalid = 2;

std::mutex& GetSignatureStatusMutex();

std::unordered_map<const CPDF_Dictionary*, int>& GetSignatureStatusMap();

// Records runtime status for |sig_dict|. Runtime state only; no PDF mutation.
void EPDF_SetSignatureStatus(const CPDF_Dictionary* sig_dict, int status);

// Returns the runtime status for |sig_dict|, or UNKNOWN when never set.
int EPDF_GetSignatureStatus(const CPDF_Dictionary* sig_dict);

#endif  // CORE_FPDFDOC_EPDF_SIGNATURE_STATUS_H_
