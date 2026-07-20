// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PUBLIC_EPDF_PIECEINFO_H_
#define PUBLIC_EPDF_PIECEINFO_H_

// NOLINTNEXTLINE(build/include)
#include "fpdfview.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Experimental EmbedPDF Extension API.
//
// Generic document- and page-piece metadata access (ISO 32000-2, 14.5).
// Every entry is addressed by an application name and maps to:
//
//   /PieceInfo <<
//     /<application> <<
//       /LastModified (D:...)
//       /Private << /<key> <value> >>
//     >>
//   >>
//
// This API intentionally supports a constrained set of safe /Private value
// types. It is not a general-purpose PDF object editor. Unknown values are
// preserved by writes to other keys and reported through
// EPDFDoc_GetPieceInfoValueType() or
// EPDFDoc_GetPagePieceInfoValueType().

// Document-level /PieceInfo --------------------------------------------------
//
// These functions operate on /PieceInfo in the document catalog. The
// document revision marker is /ModDate in the document information
// dictionary; setters store |document_last_modified| in both /Info /ModDate
// and the application data dictionary's /LastModified entry.

// Return whether the catalog has a well-formed data dictionary for
// |application| under /PieceInfo.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_HasPieceInfoEntry(FPDF_DOCUMENT document, FPDF_BYTESTRING application);

// Enumerate application names under the catalog's /PieceInfo dictionary.
// Returns 0 when /PieceInfo is absent or malformed. Entry ordering is
// unspecified.
FPDF_EXPORT int FPDF_CALLCONV
EPDFDoc_GetPieceInfoEntryCount(FPDF_DOCUMENT document);
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPieceInfoEntryAt(FPDF_DOCUMENT document,
                            int index,
                            char* buffer,
                            unsigned long buflen);

// Copy /Info /ModDate into |buffer| as UTF-16LE. Returns the required byte
// length including the trailing NUL, or 0 if absent or malformed.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetLastModified(FPDF_DOCUMENT document,
                        FPDF_WCHAR* buffer,
                        unsigned long buflen);

// Copy the application data dictionary's /LastModified date string using the
// same two-call convention as EPDFDoc_GetLastModified().
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPieceInfoLastModified(FPDF_DOCUMENT document,
                                 FPDF_BYTESTRING application,
                                 FPDF_WCHAR* buffer,
                                 unsigned long buflen);

FPDF_EXPORT int FPDF_CALLCONV
EPDFDoc_GetPieceInfoKeyCount(FPDF_DOCUMENT document,
                             FPDF_BYTESTRING application);
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPieceInfoKeyAt(FPDF_DOCUMENT document,
                          FPDF_BYTESTRING application,
                          int index,
                          char* buffer,
                          unsigned long buflen);
FPDF_EXPORT FPDF_OBJECT_TYPE FPDF_CALLCONV
EPDFDoc_GetPieceInfoValueType(FPDF_DOCUMENT document,
                              FPDF_BYTESTRING application,
                              FPDF_BYTESTRING key);

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPieceInfoString(FPDF_DOCUMENT document,
                           FPDF_BYTESTRING application,
                           FPDF_BYTESTRING key,
                           FPDF_WIDESTRING value,
                           FPDF_WIDESTRING document_last_modified);
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPieceInfoString(FPDF_DOCUMENT document,
                           FPDF_BYTESTRING application,
                           FPDF_BYTESTRING key,
                           FPDF_WCHAR* buffer,
                           unsigned long buflen);

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPieceInfoNumber(FPDF_DOCUMENT document,
                           FPDF_BYTESTRING application,
                           FPDF_BYTESTRING key,
                           float value,
                           FPDF_WIDESTRING document_last_modified);
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_GetPieceInfoNumber(FPDF_DOCUMENT document,
                           FPDF_BYTESTRING application,
                           FPDF_BYTESTRING key,
                           float* value);

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPieceInfoBoolean(FPDF_DOCUMENT document,
                            FPDF_BYTESTRING application,
                            FPDF_BYTESTRING key,
                            FPDF_BOOL value,
                            FPDF_WIDESTRING document_last_modified);
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_GetPieceInfoBoolean(FPDF_DOCUMENT document,
                            FPDF_BYTESTRING application,
                            FPDF_BYTESTRING key,
                            FPDF_BOOL* value);

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPieceInfoName(FPDF_DOCUMENT document,
                         FPDF_BYTESTRING application,
                         FPDF_BYTESTRING key,
                         FPDF_BYTESTRING value,
                         FPDF_WIDESTRING document_last_modified);
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPieceInfoName(FPDF_DOCUMENT document,
                         FPDF_BYTESTRING application,
                         FPDF_BYTESTRING key,
                         char* buffer,
                         unsigned long buflen);

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPieceInfoStringArray(FPDF_DOCUMENT document,
                                FPDF_BYTESTRING application,
                                FPDF_BYTESTRING key,
                                const FPDF_WIDESTRING* values,
                                unsigned long value_count,
                                FPDF_WIDESTRING document_last_modified);
FPDF_EXPORT int FPDF_CALLCONV
EPDFDoc_GetPieceInfoStringArrayCount(FPDF_DOCUMENT document,
                                     FPDF_BYTESTRING application,
                                     FPDF_BYTESTRING key);
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPieceInfoStringArrayAt(FPDF_DOCUMENT document,
                                  FPDF_BYTESTRING application,
                                  FPDF_BYTESTRING key,
                                  int index,
                                  FPDF_WCHAR* buffer,
                                  unsigned long buflen);

// Missing keys and entries are successful no-ops. Clearing the final entry
// removes /PieceInfo from the catalog but leaves /Info /ModDate intact.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_ClearPieceInfoKey(FPDF_DOCUMENT document,
                          FPDF_BYTESTRING application,
                          FPDF_BYTESTRING key,
                          FPDF_WIDESTRING document_last_modified);
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_ClearPieceInfoEntry(FPDF_DOCUMENT document,
                            FPDF_BYTESTRING application);

// Page-level /PieceInfo ------------------------------------------------------

// Return whether the page has a well-formed data dictionary for
// |application| under /PieceInfo.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_HasPagePieceInfoEntry(FPDF_DOCUMENT document,
                              unsigned int page_object_number,
                              FPDF_BYTESTRING application);

// Enumerate application names under the page's /PieceInfo dictionary. Returns
// 0 when /PieceInfo is absent or malformed. Entry ordering is unspecified.
FPDF_EXPORT int FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoEntryCount(FPDF_DOCUMENT document,
                                   unsigned int page_object_number);

// Copy the UTF-8 PDF name of the /PieceInfo entry at |index| into |buffer|,
// including a trailing NUL. Returns the required byte length, or 0 on error.
// |buffer| may be NULL to query the length. Malformed entries are enumerated
// so callers can distinguish them with EPDFDoc_HasPagePieceInfoEntry().
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoEntryAt(FPDF_DOCUMENT document,
                                unsigned int page_object_number,
                                int index,
                                char* buffer,
                                unsigned long buflen);

// Copy the page dictionary's /LastModified date string into |buffer| as
// UTF-16LE. Returns the required byte length including the trailing NUL, or 0
// if the page/date is absent or malformed. |buffer| may be NULL to query the
// length.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPageLastModified(FPDF_DOCUMENT document,
                            unsigned int page_object_number,
                            FPDF_WCHAR* buffer,
                            unsigned long buflen);

// Copy the application data dictionary's /LastModified date string into
// |buffer| as UTF-16LE. Uses the same two-call convention as
// EPDFDoc_GetPageLastModified().
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoLastModified(FPDF_DOCUMENT document,
                                     unsigned int page_object_number,
                                     FPDF_BYTESTRING application,
                                     FPDF_WCHAR* buffer,
                                     unsigned long buflen);

// Return the number of keys in the application's /Private dictionary, or 0
// when the entry/private dictionary is absent or malformed.
FPDF_EXPORT int FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoKeyCount(FPDF_DOCUMENT document,
                                 unsigned int page_object_number,
                                 FPDF_BYTESTRING application);

// Copy the UTF-8 PDF name of the /Private key at |index| into |buffer|,
// including a trailing NUL. Returns the required byte length, or 0 on error.
// |buffer| may be NULL to query the length. Key ordering is unspecified.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoKeyAt(FPDF_DOCUMENT document,
                              unsigned int page_object_number,
                              FPDF_BYTESTRING application,
                              int index,
                              char* buffer,
                              unsigned long buflen);

// Return the resolved FPDF_OBJECT_* type of |key| in the application's
// /Private dictionary, or FPDF_OBJECT_UNKNOWN when absent/malformed.
FPDF_EXPORT FPDF_OBJECT_TYPE FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoValueType(FPDF_DOCUMENT document,
                                  unsigned int page_object_number,
                                  FPDF_BYTESTRING application,
                                  FPDF_BYTESTRING key);

// Set/get a PDF text string. |content_last_modified| is a PDF date string
// identifying the page-content revision represented by this application data;
// the setter stores it in both the page and application data dictionaries.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPagePieceInfoString(FPDF_DOCUMENT document,
                               unsigned int page_object_number,
                               FPDF_BYTESTRING application,
                               FPDF_BYTESTRING key,
                               FPDF_WIDESTRING value,
                               FPDF_WIDESTRING content_last_modified);
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoString(FPDF_DOCUMENT document,
                               unsigned int page_object_number,
                               FPDF_BYTESTRING application,
                               FPDF_BYTESTRING key,
                               FPDF_WCHAR* buffer,
                               unsigned long buflen);

// Set/get a PDF number.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPagePieceInfoNumber(FPDF_DOCUMENT document,
                               unsigned int page_object_number,
                               FPDF_BYTESTRING application,
                               FPDF_BYTESTRING key,
                               float value,
                               FPDF_WIDESTRING content_last_modified);
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoNumber(FPDF_DOCUMENT document,
                               unsigned int page_object_number,
                               FPDF_BYTESTRING application,
                               FPDF_BYTESTRING key,
                               float* value);

// Set/get a PDF boolean.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPagePieceInfoBoolean(FPDF_DOCUMENT document,
                                unsigned int page_object_number,
                                FPDF_BYTESTRING application,
                                FPDF_BYTESTRING key,
                                FPDF_BOOL value,
                                FPDF_WIDESTRING content_last_modified);
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoBoolean(FPDF_DOCUMENT document,
                                unsigned int page_object_number,
                                FPDF_BYTESTRING application,
                                FPDF_BYTESTRING key,
                                FPDF_BOOL* value);

// Set/get a PDF name. Names are passed and returned as UTF-8 byte strings.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPagePieceInfoName(FPDF_DOCUMENT document,
                             unsigned int page_object_number,
                             FPDF_BYTESTRING application,
                             FPDF_BYTESTRING key,
                             FPDF_BYTESTRING value,
                             FPDF_WIDESTRING content_last_modified);
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoName(FPDF_DOCUMENT document,
                             unsigned int page_object_number,
                             FPDF_BYTESTRING application,
                             FPDF_BYTESTRING key,
                             char* buffer,
                             unsigned long buflen);

// Replace |key| with an array of PDF text strings. |values| may be NULL only
// when |value_count| is 0, which writes an empty array.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetPagePieceInfoStringArray(FPDF_DOCUMENT document,
                                    unsigned int page_object_number,
                                    FPDF_BYTESTRING application,
                                    FPDF_BYTESTRING key,
                                    const FPDF_WIDESTRING* values,
                                    unsigned long value_count,
                                    FPDF_WIDESTRING content_last_modified);

// Return the number of text strings in the array at |key|, or -1 when the key
// is absent, is not an array, or contains a non-string value.
FPDF_EXPORT int FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoStringArrayCount(FPDF_DOCUMENT document,
                                         unsigned int page_object_number,
                                         FPDF_BYTESTRING application,
                                         FPDF_BYTESTRING key);

// Copy the text string at |index| using the standard UTF-16LE two-call
// convention. Returns 0 on error.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetPagePieceInfoStringArrayAt(FPDF_DOCUMENT document,
                                      unsigned int page_object_number,
                                      FPDF_BYTESTRING application,
                                      FPDF_BYTESTRING key,
                                      int index,
                                      FPDF_WCHAR* buffer,
                                      unsigned long buflen);

// Remove |key| from the application's /Private dictionary. Missing keys are a
// successful no-op. When the entry exists, both /LastModified values are set
// to |content_last_modified|.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_ClearPagePieceInfoKey(FPDF_DOCUMENT document,
                              unsigned int page_object_number,
                              FPDF_BYTESTRING application,
                              FPDF_BYTESTRING key,
                              FPDF_WIDESTRING content_last_modified);

// Remove the complete application entry. Other /PieceInfo applications are
// preserved; an empty /PieceInfo dictionary is removed from the page.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_ClearPagePieceInfoEntry(FPDF_DOCUMENT document,
                                unsigned int page_object_number,
                                FPDF_BYTESTRING application);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_EPDF_PIECEINFO_H_
