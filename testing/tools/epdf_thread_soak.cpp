// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

// EmbedPDF: thread-confined runtime soak harness.
//
// Spawns N worker threads that each run an independent PDFium lifecycle in a
// loop: FPDF_InitLibrary -> load -> render page 0 -> (optional) encrypted save
// -> close -> FPDF_DestroyLibrary. Each worker only ever touches handles it
// created, matching the thread-confined contract.
//
// With embedpdf_thread_local_globals OFF this exercises (and is expected to
// trip) the shared process-global init/render races. With the flag ON every
// thread owns its own PDFium state and the soak should pass cleanly. The
// encrypted-save path is included on purpose: pending security is now stored on
// CPDF_Document, so it must survive concurrent SetEncryption -> save across
// threads. Intended to be run under ThreadSanitizer as the gate before the
// server worker-pool cap is lifted.

#include "testing/tools/epdf_layer_tool_common.h"

#include <stdint.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "public/cpp/fpdf_scopers.h"
#include "public/fpdf_save.h"
#include "public/fpdfview.h"

namespace {

// Fixed render surface keeps per-thread memory bounded while still exercising
// the rasterizer, font cache, and stock colorspaces.
constexpr int kRenderWidth = 300;
constexpr int kRenderHeight = 400;

size_t ParseSizeArg(const std::string& arg,
                    const char* prefix,
                    size_t fallback) {
  const std::string prefix_string(prefix);
  if (arg.rfind(prefix_string, 0) != 0) {
    return fallback;
  }
  return static_cast<size_t>(
      std::strtoull(arg.substr(prefix_string.size()).c_str(), nullptr, 10));
}

struct Options {
  size_t threads = 4;
  size_t iterations = 50;
  bool render = true;
  bool encrypt = true;
};

const char* kUsageExtra =
    "[--threads=4] [--iterations=50] [--no-render] [--no-encrypt]";

bool RenderFirstPage(FPDF_DOCUMENT doc) {
  ScopedFPDFPage page(FPDF_LoadPage(doc, 0));
  if (!page) {
    return false;
  }
  ScopedFPDFBitmap bitmap(FPDFBitmap_Create(kRenderWidth, kRenderHeight, 0));
  if (!bitmap) {
    return false;
  }
  FPDFBitmap_FillRect(bitmap.get(), 0, 0, kRenderWidth, kRenderHeight,
                      0xFFFFFFFF);
  FPDF_RenderPageBitmap(bitmap.get(), page.get(), 0, 0, kRenderWidth,
                        kRenderHeight, 0, FPDF_ANNOT);
  // Touch the buffer so the render isn't optimized away.
  return FPDFBitmap_GetBuffer(bitmap.get()) != nullptr;
}

bool EncryptAndSave(FPDF_DOCUMENT doc) {
  if (!EPDF_SetEncryption(doc, "user", "owner",
                          EPDF_PERM_PRINT | EPDF_PERM_COPY)) {
    return false;
  }
  unsigned long out_size = 0;
  void* buffer = EPDF_SaveDocumentToOwnedBuffer(doc, 0, &out_size);
  const bool ok = buffer != nullptr && out_size > 0;
  EPDF_FreeBuffer(buffer);
  return ok;
}

bool RunWorker(const std::vector<uint8_t>& bytes,
               const Options& opts,
               size_t worker_index) {
  for (size_t i = 0; i < opts.iterations; ++i) {
    FPDF_InitLibrary();
    {
      ScopedFPDFDocument doc(FPDF_LoadMemDocument64(
          bytes.data(), bytes.size(), nullptr));
      if (!doc) {
        std::fprintf(stderr, "worker %zu iter %zu: load failed\n", worker_index,
                     i);
        FPDF_DestroyLibrary();
        return false;
      }
      if (opts.render && !RenderFirstPage(doc.get())) {
        std::fprintf(stderr, "worker %zu iter %zu: render failed\n",
                     worker_index, i);
        FPDF_DestroyLibrary();
        return false;
      }
      if (opts.encrypt && !EncryptAndSave(doc.get())) {
        std::fprintf(stderr, "worker %zu iter %zu: encrypt-save failed\n",
                     worker_index, i);
        FPDF_DestroyLibrary();
        return false;
      }
    }  // Document is closed here, before tearing down the library.
    FPDF_DestroyLibrary();
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    epdf_layer_tool::PrintUsage(argv[0], kUsageExtra);
    return 2;
  }

  std::string path = argv[1];
  Options opts;
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.rfind("--threads=", 0) == 0) {
      opts.threads = ParseSizeArg(arg, "--threads=", opts.threads);
    } else if (arg.rfind("--iterations=", 0) == 0) {
      opts.iterations = ParseSizeArg(arg, "--iterations=", opts.iterations);
    } else if (arg == "--no-render") {
      opts.render = false;
    } else if (arg == "--no-encrypt") {
      opts.encrypt = false;
    } else {
      epdf_layer_tool::PrintUsage(argv[0], kUsageExtra);
      return 2;
    }
  }

  if (opts.threads == 0 || opts.iterations == 0) {
    std::fprintf(stderr, "Thread count and iterations must be positive.\n");
    return 2;
  }

  std::vector<uint8_t> bytes;
  if (!epdf_layer_tool::ReadFile(path, &bytes)) {
    std::fprintf(stderr, "Failed to read %s\n", path.c_str());
    return 1;
  }

  std::atomic<size_t> failures{0};
  std::vector<std::thread> workers;
  workers.reserve(opts.threads);
  for (size_t t = 0; t < opts.threads; ++t) {
    workers.emplace_back([&bytes, &opts, &failures, t]() {
      if (!RunWorker(bytes, opts, t)) {
        failures.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }

  const size_t failed = failures.load(std::memory_order_relaxed);
  if (failed != 0) {
    std::fprintf(stderr, "thread soak FAILED: %zu/%zu workers errored\n", failed,
                 opts.threads);
    return 1;
  }

  std::printf(
      "thread soak OK: threads=%zu iterations=%zu render=%d encrypt=%d\n",
      opts.threads, opts.iterations, opts.render ? 1 : 0,
      opts.encrypt ? 1 : 0);
  return 0;
}
