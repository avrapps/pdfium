// Copyright 2025 The EmbedPDF Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// EmbedPDF: thread-confined runtime support.
//
// EPDF_TLS expands to `thread_local` when the build is configured with the
// `embedpdf_thread_local_globals` GN arg (which defines
// EPDF_THREAD_LOCAL_GLOBALS), and to nothing otherwise. It is used to give
// each worker thread its own copy of PDFium's process-global singletons so
// that N threads can run shared-nothing in a single process.
//
// Contract: a thread that touches any EPDF_TLS-backed global must initialize
// PDFium on that thread (EPDF_InitThread), use only handles created on that
// thread, and tear down on the same thread (EPDF_ShutdownThread). PDFium
// handles must never cross threads.
//
// Default (flag off) keeps every global as an ordinary process-global, so
// targets that do not opt in and upstream rebases are byte-for-byte unchanged.

#ifndef CORE_FXCRT_EPDF_TLS_H_
#define CORE_FXCRT_EPDF_TLS_H_

#if defined(EPDF_THREAD_LOCAL_GLOBALS)
#define EPDF_TLS thread_local
#else
#define EPDF_TLS
#endif

#endif  // CORE_FXCRT_EPDF_TLS_H_
