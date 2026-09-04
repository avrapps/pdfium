// Copyright 2020 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFDOC_EPDF_SIGNATURE_STATUS_COMPOSE_H_
#define CORE_FPDFDOC_EPDF_SIGNATURE_STATUS_COMPOSE_H_

#include <vector>

class CPDF_Stream;

// Experimental EmbedPDF Extension: Adobe/iText/Foxit-style validity-layer swap
// for a visible digital signature, applied to the appearance layers IN MEMORY
// with a save-exclusion guard so the on-disk (signed) bytes never change.
//
// A visible signature appearance is a layered form XObject (FRM) that paints
// child layers n0..n4 via `/nX Do`. Per Adobe's convention:
//   n1 = the "unknown" placeholder graphic (the big yellow "?")
//   n3 = the runtime validity layer (blank until validated)
//   n4 = the status text line ("Signature Not Verified", etc.)
//
// EPDF_ApplySignatureStatusLayers rewrites the in-memory content of n1/n3/n4
// (found via |ap_stream|'s /Resources /XObject) to reflect |status|:
//   VALID   -> blank n1 (remove "?"), draw a green check in n3, text "Signature Valid"
//   INVALID -> blank n1, draw a red X in n3, text "Signature Invalid"
//   UNKNOWN -> restore the originals (show the "?")
//
// The ORIGINAL raw bytes of each touched layer are stashed on first edit, and
// EPDF_RestoreSignatureLayers() (called at the start of the save path) rewrites
// them back before serialization -- so a save produces byte-identical layers
// and the signature is never invalidated. After a save the next render simply
// re-applies the swap.
//
// |status| is 0=UNKNOWN, 1=VALID, 2=INVALID (see epdf_signature_status.h).
void EPDF_ApplySignatureStatusLayers(const CPDF_Stream* ap_stream, int status);

// Restores every stashed signature layer to its original bytes. Idempotent.
// Call before serializing a document so runtime status never reaches disk.
void EPDF_RestoreSignatureLayers();

// Drops the stashed backups (and the keep-alive retain) for the signature
// appearance layers reachable from the given appearance (AP) form streams.
// Called from FPDF_CloseDocument() with the closing document's signature
// appearance streams, so no backup outlives its document and a later save
// never touches a closed document's layers. Idempotent; unknown streams are
// ignored.
void EPDF_CleanupSignatureLayers(
    const std::vector<CPDF_Stream*>& ap_streams);

#endif  // CORE_FPDFDOC_EPDF_SIGNATURE_STATUS_COMPOSE_H_
