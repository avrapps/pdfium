// Copyright 2026 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PUBLIC_EPDF_ACTION_H_
#define PUBLIC_EPDF_ACTION_H_

#include <stdint.h>

// NOLINTNEXTLINE(build/include)
#include "fpdfview.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Experimental EmbedPDF Extension API.
//
// Detached PDF action model. A model contains one root action and its
// normalized /Next descendants. Type, subtype, script, chain, destination,
// URI, file path, and named-action payloads are copied at build time and stay
// valid after the document is closed or mutated. The getters that take an
// FPDF_DOCUMENT still require the originating document to be open: it
// validates destination page identity and is used to resolve a named
// destination when EPDFAction_LoadModel() had no document owner available.
//
// The API extracts action data only. It never executes JavaScript.
typedef struct epdf_action_model_t__* EPDF_ACTION_MODEL;
typedef uint32_t EPDF_ACTION_NODE_ID;

#define EPDF_ACTION_NODE_INVALID UINT32_MAX

// Normalized values of an action dictionary's /S name. The raw /S name is
// also available so unknown future action types are preserved.
#define EPDF_ACTION_TYPE_UNKNOWN 0
#define EPDF_ACTION_TYPE_GOTO 1
#define EPDF_ACTION_TYPE_GOTO_REMOTE 2
#define EPDF_ACTION_TYPE_GOTO_EMBEDDED 3
#define EPDF_ACTION_TYPE_LAUNCH 4
#define EPDF_ACTION_TYPE_THREAD 5
#define EPDF_ACTION_TYPE_URI 6
#define EPDF_ACTION_TYPE_SOUND 7
#define EPDF_ACTION_TYPE_MOVIE 8
#define EPDF_ACTION_TYPE_HIDE 9
#define EPDF_ACTION_TYPE_NAMED 10
#define EPDF_ACTION_TYPE_SUBMIT_FORM 11
#define EPDF_ACTION_TYPE_RESET_FORM 12
#define EPDF_ACTION_TYPE_IMPORT_DATA 13
#define EPDF_ACTION_TYPE_JAVASCRIPT 14
#define EPDF_ACTION_TYPE_SET_OCG_STATE 15
#define EPDF_ACTION_TYPE_RENDITION 16
#define EPDF_ACTION_TYPE_TRANSITION 17
#define EPDF_ACTION_TYPE_GOTO_3D_VIEW 18

// Non-fatal normalization warnings. Models marked INCOMPLETE must not be
// executed: a safety bound prevented the complete action sequence from being
// represented. Cyclic back-edges and malformed /Next entries are dropped;
// their other well-formed siblings remain available.
#define EPDF_ACTION_WARNING_CYCLE_DROPPED 0x1
#define EPDF_ACTION_WARNING_MALFORMED_NEXT 0x2
#define EPDF_ACTION_WARNING_INCOMPLETE 0x4

// Release a model returned by any EPDF*ActionModel() function below.
FPDF_EXPORT void FPDF_CALLCONV EPDFAction_CloseModel(EPDF_ACTION_MODEL model);

// Build a detached model from an existing borrowed FPDF_ACTION. This lets
// callers normalize actions returned by APIs such as FPDFBookmark_GetAction().
FPDF_EXPORT EPDF_ACTION_MODEL FPDF_CALLCONV
EPDFAction_LoadModel(FPDF_ACTION action);

// Return the root node id, or EPDF_ACTION_NODE_INVALID for an invalid model.
FPDF_EXPORT EPDF_ACTION_NODE_ID FPDF_CALLCONV
EPDFAction_GetRootNode(EPDF_ACTION_MODEL model);

FPDF_EXPORT int FPDF_CALLCONV EPDFAction_GetNodeCount(EPDF_ACTION_MODEL model);

// Return an EPDF_ACTION_TYPE_* value for |node|.
FPDF_EXPORT int FPDF_CALLCONV EPDFAction_GetNodeType(EPDF_ACTION_MODEL model,
                                                     EPDF_ACTION_NODE_ID node);

// Copy the raw PDF /S name as UTF-8, including the trailing NUL. Returns the
// required byte length, or 0 on error. |buffer| may be NULL to query length.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFAction_GetNodeSubtype(EPDF_ACTION_MODEL model,
                          EPDF_ACTION_NODE_ID node,
                          char* buffer,
                          unsigned long buflen);

// Return whether |node| contains a string or stream /JS entry that belongs to
// either a /JavaScript or /Rendition action. This distinguishes an empty
// script from a missing or malformed /JS entry.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFAction_NodeHasJavaScript(EPDF_ACTION_MODEL model, EPDF_ACTION_NODE_ID node);

// Copy decoded /JS source as UTF-16LE, including the trailing NUL. Returns the
// required byte length, or 0 when absent/malformed. Rendition /JS is exposed
// through this same getter.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFAction_GetNodeJavaScript(EPDF_ACTION_MODEL model,
                             EPDF_ACTION_NODE_ID node,
                             FPDF_WCHAR* buffer,
                             unsigned long buflen);

// Get the destination of a goto / goto-remote / goto-embedded |node| as an
// explicit FPDF_DEST. Named destinations resolve through |document|'s
// catalog — same normalization as FPDFLink_GetDest. Returns NULL when the
// node carries no destination, has a different type, or |document| is
// invalid. |document| must be the document the model was built from.
FPDF_EXPORT FPDF_DEST FPDF_CALLCONV
EPDFAction_GetNodeDest(FPDF_DOCUMENT document,
                       EPDF_ACTION_MODEL model,
                       EPDF_ACTION_NODE_ID node);

// Copy the /URI of a uri-type |node| as a NUL-terminated byte string.
// Returns the required byte length including the NUL, or 0 when the node
// is not a uri action. |buffer| may be NULL to query the length.
// |document| must be the document the model was built from.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFAction_GetNodeURI(FPDF_DOCUMENT document,
                      EPDF_ACTION_MODEL model,
                      EPDF_ACTION_NODE_ID node,
                      void* buffer,
                      unsigned long buflen);

// Copy the file spec of a goto-remote / goto-embedded / launch |node| as
// UTF-8, including the trailing NUL. Returns the required byte length, or
// 0 for other node types. |buffer| may be NULL to query the length.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFAction_GetNodeFilePath(EPDF_ACTION_MODEL model,
                           EPDF_ACTION_NODE_ID node,
                           void* buffer,
                           unsigned long buflen);

// Copy the /N name of a named-type |node| (NextPage, PrevPage, ...) as
// UTF-8, including the trailing NUL. Returns the required byte length, or
// 0 for other node types. |buffer| may be NULL to query the length.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFAction_GetNodeName(EPDF_ACTION_MODEL model,
                       EPDF_ACTION_NODE_ID node,
                       void* buffer,
                       unsigned long buflen);

FPDF_EXPORT int FPDF_CALLCONV EPDFAction_GetNextCount(EPDF_ACTION_MODEL model,
                                                      EPDF_ACTION_NODE_ID node);

// Return the normalized child node at |index| in PDF /Next order.
FPDF_EXPORT EPDF_ACTION_NODE_ID FPDF_CALLCONV
EPDFAction_GetNextAt(EPDF_ACTION_MODEL model,
                     EPDF_ACTION_NODE_ID node,
                     int index);

FPDF_EXPORT uint32_t FPDF_CALLCONV
EPDFAction_GetWarningFlags(EPDF_ACTION_MODEL model);

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFAction_IsComplete(EPDF_ACTION_MODEL model);

// Document-owned actions ----------------------------------------------------

#define EPDF_DOCUMENT_ACTION_WILL_CLOSE 0
#define EPDF_DOCUMENT_ACTION_WILL_SAVE 1
#define EPDF_DOCUMENT_ACTION_DID_SAVE 2
#define EPDF_DOCUMENT_ACTION_WILL_PRINT 3
#define EPDF_DOCUMENT_ACTION_DID_PRINT 4

// Return the action model for /Names /JavaScript entry |index|. Index pairing
// is guaranteed with FPDFDoc_GetJavaScriptAction(document, index): both calls
// resolve the same name-tree entry and therefore preserve boot order.
FPDF_EXPORT EPDF_ACTION_MODEL FPDF_CALLCONV
EPDFDoc_GetNamedJavaScriptActionModel(FPDF_DOCUMENT document, int index);

// Return the action form of catalog /OpenAction. Returns NULL when absent,
// malformed, or when /OpenAction is a destination rather than an action.
FPDF_EXPORT EPDF_ACTION_MODEL FPDF_CALLCONV
EPDFDoc_GetOpenActionModel(FPDF_DOCUMENT document);

// Return one catalog /AA action selected by EPDF_DOCUMENT_ACTION_*.
FPDF_EXPORT EPDF_ACTION_MODEL FPDF_CALLCONV
EPDFDoc_GetAdditionalActionModel(FPDF_DOCUMENT document, int event);

// Page-owned actions --------------------------------------------------------

#define EPDF_PAGE_ACTION_OPEN 0
#define EPDF_PAGE_ACTION_CLOSE 1

// Read page /AA without loading or rendering the page.
FPDF_EXPORT EPDF_ACTION_MODEL FPDF_CALLCONV
EPDFDoc_GetPageActionModel(FPDF_DOCUMENT document,
                           uint32_t page_object_number,
                           int event);

// Annotation-owned actions --------------------------------------------------

#define EPDF_ANNOT_ACTION_ACTIVATE 0
#define EPDF_ANNOT_ACTION_CURSOR_ENTER 1
#define EPDF_ANNOT_ACTION_CURSOR_EXIT 2
#define EPDF_ANNOT_ACTION_MOUSE_DOWN 3
#define EPDF_ANNOT_ACTION_MOUSE_UP 4
#define EPDF_ANNOT_ACTION_FOCUS 5
#define EPDF_ANNOT_ACTION_BLUR 6
#define EPDF_ANNOT_ACTION_PAGE_OPEN 7
#define EPDF_ANNOT_ACTION_PAGE_CLOSE 8
#define EPDF_ANNOT_ACTION_PAGE_VISIBLE 9
#define EPDF_ANNOT_ACTION_PAGE_INVISIBLE 10

// Return annotation /A (ACTIVATE) or an annotation /AA action. Field events
// K/F/V/C are deliberately not accepted here, including for merged
// field/widget dictionaries.
FPDF_EXPORT EPDF_ACTION_MODEL FPDF_CALLCONV
EPDFAnnot_GetActionModel(FPDF_ANNOTATION annotation, int event);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_EPDF_ACTION_H_
