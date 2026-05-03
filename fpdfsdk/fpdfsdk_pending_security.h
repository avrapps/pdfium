// Copyright 2014 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FPDFSDK_FPDFSDK_PENDING_SECURITY_H_
#define FPDFSDK_FPDFSDK_PENDING_SECURITY_H_

#include <mutex>
#include <unordered_map>

#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_security_handler.h"
#include "core/fxcrt/retain_ptr.h"
#include "public/fpdfview.h"

// Experimental EmbedPDF Extension: pending security state shared between
// fpdf_view.cpp (writers: EPDF_SetEncryption / EPDF_RemoveEncryption /
// EPDF_CleanupPendingSecurity) and fpdf_save.cpp (reader: DoDocSave).
//
// The storage is keyed by FPDF_DOCUMENT handle and protected by a mutex.
// Both the mutex and the map are intentionally leaked (heap-allocated behind
// function-local statics) to avoid the static destruction order fiasco -- the
// map holds RetainPtr<> handles to PDFium objects whose destructors must not
// race with process shutdown of other globals.

enum class PendingSecurityMode { kNone, kEncrypt, kRemove };

struct PendingSecurity {
  PendingSecurityMode mode = PendingSecurityMode::kNone;
  RetainPtr<CPDF_Dictionary> encrypt_dict;           // Only for kEncrypt
  RetainPtr<CPDF_SecurityHandler> security_handler;  // Only for kEncrypt
};

std::mutex& GetPendingSecurityMutex();
std::unordered_map<FPDF_DOCUMENT, PendingSecurity>& GetPendingSecurityMap();

#endif  // FPDFSDK_FPDFSDK_PENDING_SECURITY_H_
