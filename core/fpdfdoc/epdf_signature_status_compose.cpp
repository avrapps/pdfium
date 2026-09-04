// Copyright 2020 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfdoc/epdf_signature_status_compose.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_object.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/cpdf_stream_acc.h"
#include "core/fpdfdoc/epdf_signature_status.h"
#include "core/fxcrt/bytestring.h"
#include "core/fxcrt/data_vector.h"

namespace {

// Original state of a signature layer stream, captured on first edit so the
// exact bytes can be restored before a save.
struct LayerBackup {
  std::vector<uint8_t> raw_data;               // original (possibly encoded)
  RetainPtr<CPDF_Object> filter;               // original /Filter (cloned)
  RetainPtr<CPDF_Object> decode_parms;         // original /DecodeParms (cloned)
  bool had_filter = false;
  bool had_decode_parms = false;
};

std::mutex& LayerMutex() {
  static std::mutex* m = new std::mutex;
  return *m;
}

// Keyed by a RetainPtr to the layer stream so the stream object stays alive as
// long as a backup exists. A raw pointer here would dangle once the owning
// document is closed, and the next save's EPDF_RestoreSignatureLayers() would
// call SetData() on freed memory (SIGSEGV in CPDF_Dictionary::SetForInternal).
std::unordered_map<CPDF_Stream*, LayerBackup>& LayerBackups() {
  static auto* m = new std::unordered_map<CPDF_Stream*, LayerBackup>;
  return *m;
}

// Retains each stashed stream, keyed by its raw pointer, so the CPDF_Stream is
// kept alive for the lifetime of its backup. Parallel to LayerBackups().
std::unordered_map<CPDF_Stream*, RetainPtr<CPDF_Stream>>& LayerKeepAlive() {
  static auto* m = new std::unordered_map<CPDF_Stream*, RetainPtr<CPDF_Stream>>;
  return *m;
}

// Erases a single stashed layer (backup + keep-alive). Caller holds LayerMutex.
void EraseLayerLocked(CPDF_Stream* stream) {
  LayerBackups().erase(stream);
  LayerKeepAlive().erase(stream);
}

// Restores every stashed layer to its original bytes and clears the maps.
// Caller MUST hold LayerMutex (LayerMutex is non-recursive).
void RestoreAllLocked() {
  auto& backups = LayerBackups();
  for (auto& [stream, backup] : backups) {
    // Restore original raw bytes.
    stream->SetData(backup.raw_data);
    RetainPtr<CPDF_Dictionary> dict = stream->GetMutableDict();
    if (!dict) {
      continue;
    }
    // Restore original /Filter and /DecodeParms exactly.
    if (backup.had_filter) {
      dict->SetFor("Filter", backup.filter->Clone());
    } else {
      dict->RemoveFor("Filter");
    }
    if (backup.had_decode_parms) {
      dict->SetFor("DecodeParms", backup.decode_parms->Clone());
    } else {
      dict->RemoveFor("DecodeParms");
    }
  }
  backups.clear();
  LayerKeepAlive().clear();
}

void StashOriginal(CPDF_Stream* stream) {
  auto& backups = LayerBackups();
  if (backups.count(stream)) {
    return;  // already stashed
  }
  // Keep the stream alive for as long as we hold a backup for it, so the raw
  // pointer key can never dangle after the owning document is torn down.
  LayerKeepAlive()[stream] = pdfium::WrapRetain(stream);
  LayerBackup b;
  // The stream may be file-based (mmap'd from the PDF) or memory-based; use the
  // matching accessor. GetInMemoryRawData() aborts on a file-based stream.
  if (stream->IsMemoryBased()) {
    pdfium::span<const uint8_t> raw = stream->GetInMemoryRawData();
    b.raw_data.assign(raw.begin(), raw.end());
  } else {
    DataVector<uint8_t> raw = stream->ReadAllRawData();
    b.raw_data.assign(raw.begin(), raw.end());
  }
  RetainPtr<CPDF_Dictionary> dict = stream->GetMutableDict();
  if (dict) {
    if (RetainPtr<const CPDF_Object> f = dict->GetObjectFor("Filter")) {
      b.filter = f->Clone();
      b.had_filter = true;
    }
    if (RetainPtr<const CPDF_Object> dp = dict->GetObjectFor("DecodeParms")) {
      b.decode_parms = dp->Clone();
      b.had_decode_parms = true;
    }
  }
  backups[stream] = std::move(b);
}

// Replaces a layer stream's content with |content| (uncompressed). The original
// bytes are stashed first so a save can restore them exactly.
void SetLayerContent(CPDF_Stream* stream, const ByteString& content) {
  if (!stream) {
    return;
  }
  StashOriginal(stream);
  stream->SetDataAndRemoveFilter(content.unsigned_span());
}

// Fetch a child layer stream (n0/n1/.../n4) by name from a form stream's
// /Resources /XObject. The visible-signature appearance nests the nX layers one
// level down inside the "FRM" form, so this searches the given stream's XObject
// resources AND recurses into child form XObjects (bounded depth) to find them.
CPDF_Stream* FindLayerRecursive(const CPDF_Stream* form_stream,
                                ByteStringView name,
                                int depth) {
  if (!form_stream || depth > 4) {
    return nullptr;
  }
  RetainPtr<const CPDF_Dictionary> dict = form_stream->GetDict();
  if (!dict) {
    return nullptr;
  }
  RetainPtr<const CPDF_Dictionary> res = dict->GetDictFor("Resources");
  if (!res) {
    return nullptr;
  }
  RetainPtr<CPDF_Dictionary> xobj =
      const_cast<CPDF_Dictionary*>(res.Get())->GetMutableDictFor("XObject");
  if (!xobj) {
    return nullptr;
  }
  // Direct hit at this level.
  if (CPDF_Stream* direct = xobj->GetMutableStreamFor(name).Get()) {
    return direct;
  }
  // Otherwise recurse into every child form XObject (e.g. FRM).
  for (const ByteString& key : xobj->GetKeys()) {
    RetainPtr<CPDF_Stream> child = xobj->GetMutableStreamFor(key.AsStringView());
    if (!child) {
      continue;
    }
    if (CPDF_Stream* found =
            FindLayerRecursive(child.Get(), name, depth + 1)) {
      return found;
    }
  }
  return nullptr;
}

CPDF_Stream* GetLayer(const CPDF_Stream* ap_stream, ByteStringView name) {
  return FindLayerRecursive(ap_stream, name, 0);
}

// Adobe's EXACT validity checkmark, extracted verbatim from a flattened
// Acrobat-rendered signature. It is three stacked copies of the same 5-segment
// polygon at slightly offset origins: a white highlight, a black shadow/outline,
// and the green check on top. Reproduced here relative to the green copy's
// origin (448.0443, 344.0644); the other two copies keep their true relative
// offsets (white: -0.3486,-0.6475; black: +1.4443,-2.4901).
//
// An outer transform scales/positions the ~47x48-unit artwork into the n3
// validity layer's 100x100 coordinate space (centered, ~1.4x, lower-left biased
// so the tick sits over the signer text like Adobe).
const char kCheckContent[] =
    "q\n"
    "1.4 0 0 1.4 34 18 cm\n"                 // place+scale art into 100x100
    // 1) White offset highlight.
    "1 1 1 rg\n"
    "q 1 0 0 1 -0.3486 -0.6475 cm\n"
    "0 0 m\n"
    "-18.427 18.128 l\n"
    "-12.152 26.743 l\n"
    "-0.249 12.55 l\n"
    "20.369 47.959 l\n"
    "28.586 41.784 l\n"
    "h\n"
    "f\n"
    "Q\n"
    // 2) Black shadow / outline.
    "0 0 0 rg\n"
    "q 1 0 0 1 1.4443 -2.4901 cm\n"
    "0 0 m\n"
    "-17.48 17.381 l\n"
    "-12.401 24.154 l\n"
    "-0.498 9.96 l\n"
    "20.468 45.618 l\n"
    "27.74 40.24 l\n"
    "h\n"
    "f\n"
    "Q\n"
    // 3) Exact green checkmark (fill + thin stroke).
    "0 0.7 0.23 rg\n"
    "0.747 w 4 M 0 J\n"
    "q 1 0 0 1 0 0 cm\n"
    "0 0 m\n"
    "-17.53 17.381 l\n"
    "-12.401 24.204 l\n"
    "-0.498 10.01 l\n"
    "20.419 45.668 l\n"
    "27.74 40.289 l\n"
    "h\n"
    "B\n"
    "Q\n"
    "Q\n";

// Red X (invalid). Adobe uses its own private glyph here too; this is a filled
// bordered cross in the same 100x100 space until an exact Adobe path is
// extracted for the invalid case.
const char kCrossContent[] =
    "q\n"
    "0.84 0.11 0.11 rg\n"
    "0.45 0.03 0.03 RG\n"
    "3 w 1 J 1 j\n"
    "22 30 m 34 18 l 50 34 l 66 18 l 78 30 l 62 46 l 78 62 l 66 74 l "
    "50 58 l 34 74 l 22 62 l 38 46 l h\n"
    "B\n"
    "Q\n";

// Escapes a byte string for inclusion inside a PDF literal string ( ... ).
ByteString EscapePdfString(const std::string& in) {
  std::string out;
  for (char c : in) {
    if (c == '(' || c == ')' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  return ByteString(out.c_str());
}

// Extracts the signer name from the n2 signer-text layer. n2 typically reads:
//   BT ... (Digitally signed by)Tj ... (DS MINISTRY ...)Tj (AFFAIRS ...)Tj
//   (Date: ...)Tj ET
// We collect the (...)Tj strings that come AFTER "Digitally signed by" and
// BEFORE a "Date:"/"Reason:"/"Location:" line, joining them with spaces.
std::string ExtractSignerName(const CPDF_Stream* n2) {
  if (!n2) {
    return std::string();
  }
  auto acc = pdfium::MakeRetain<CPDF_StreamAcc>(pdfium::WrapRetain(n2));
  acc->LoadAllDataFiltered();
  pdfium::span<const uint8_t> raw = acc->GetSpan();
  std::string content(reinterpret_cast<const char*>(raw.data()), raw.size());

  std::string name;
  bool started = false;
  size_t pos = 0;
  const size_t n = content.size();
  while (pos < n) {
    size_t open = content.find('(', pos);
    if (open == std::string::npos) {
      break;
    }
    size_t i = open + 1;
    std::string token;
    while (i < n) {
      char c = content[i];
      if (c == '\\' && i + 1 < n) {
        token += content[i + 1];
        i += 2;
        continue;
      }
      if (c == ')') {
        break;
      }
      token += c;
      ++i;
    }
    pos = i + 1;

    if (!started) {
      if (token.find("Digitally signed by") != std::string::npos) {
        started = true;
      }
      continue;
    }
    if (token.find("Date:") != std::string::npos ||
        token.find("Reason:") != std::string::npos ||
        token.find("Location:") != std::string::npos) {
      break;
    }
    if (token.empty()) {
      continue;
    }
    if (!name.empty()) {
      name += " ";
    }
    name += token;
  }
  return name;
}

}  // namespace

void EPDF_ApplySignatureStatusLayers(const CPDF_Stream* ap_stream, int status) {
  std::lock_guard<std::mutex> lock(LayerMutex());

  CPDF_Stream* n1 = GetLayer(ap_stream, "n1");  // the "?"
  CPDF_Stream* n3 = GetLayer(ap_stream, "n3");  // validity layer
  CPDF_Stream* n4 = GetLayer(ap_stream, "n4");  // status text

  if (status == kEpdfSignatureStatusUnknown) {
    RestoreAllLocked();  // show the original "?"; LayerMutex already held
    return;
  }

  // Blank the "?" placeholder layer.
  if (n1) {
    SetLayerContent(n1, ByteString("% DSBlank\n"));
  }

  // Draw the status glyph into the validity layer (n3).
  if (n3) {
    SetLayerContent(n3, ByteString(status == kEpdfSignatureStatusValid
                                       ? kCheckContent
                                       : kCrossContent));
  }

  // n4 holds the status text line. VALID -> "Document Signed by <signer>"
  // (signer name pulled from the n2 layer); INVALID -> "Signature invalid".
  if (n4) {
    std::string label;
    if (status == kEpdfSignatureStatusValid) {
      std::string signer = ExtractSignerName(GetLayer(ap_stream, "n2"));
      label = signer.empty() ? std::string("Document Signed")
                             : std::string("Document Signed by ") + signer;
    } else {
      label = "Signature invalid";
    }
    ByteString text;
    text += "BT\n1 0 0 1 2 53 Tm\n/F1 13 Tf\n(";
    text += EscapePdfString(label);
    text += ")Tj\nET\n";
    SetLayerContent(n4, text);
  }
}

void EPDF_RestoreSignatureLayers() {
  std::lock_guard<std::mutex> lock(LayerMutex());
  RestoreAllLocked();
}

void EPDF_CleanupSignatureLayers(
    const std::vector<CPDF_Stream*>& ap_streams) {
  std::lock_guard<std::mutex> lock(LayerMutex());
  static constexpr const char* kLayerNames[] = {"n0", "n1", "n2", "n3", "n4"};
  for (CPDF_Stream* ap : ap_streams) {
    if (!ap) {
      continue;
    }
    for (const char* name : kLayerNames) {
      if (CPDF_Stream* layer = GetLayer(ap, name)) {
        EraseLayerLocked(layer);
      }
    }
  }
}
