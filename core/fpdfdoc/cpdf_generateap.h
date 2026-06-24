// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_FPDFDOC_CPDF_GENERATEAP_H_
#define CORE_FPDFDOC_CPDF_GENERATEAP_H_

#include <optional>

#include "core/fpdfdoc/cpdf_annot.h"
#include "core/fxge/cfx_fontregistry.h"

class CPDF_Dictionary;
class CPDF_Document;
struct CFX_Color;
enum class BlendMode;

class CPDF_GenerateAP {
 public:
  enum FormType { kTextField, kComboBox, kListBox };

  static void GenerateFormAP(CPDF_Document* doc,
                             CPDF_Dictionary* pAnnotDict,
                             FormType type);

  static void GenerateCheckboxFormAP(CPDF_Document* doc,
                                     CPDF_Dictionary* annot_dict);

  static void GenerateRadioButtonFormAP(CPDF_Document* doc,
                                        CPDF_Dictionary* annot_dict);

  static void GenerateEmptyAP(CPDF_Document* doc, CPDF_Dictionary* pAnnotDict);

  static bool GenerateAnnotAP(CPDF_Document* doc,
                              CPDF_Dictionary* pAnnotDict,
                              CPDF_Annot::Subtype subtype);

  static bool GenerateAnnotAP(CPDF_Document* doc,
                              CPDF_Dictionary* annot_dict,
                              CPDF_Annot::Subtype subtype,
                              BlendMode blend_mode);

  struct GeneratedAP {
    RetainPtr<CPDF_Stream> normal_stream;
  };

  static std::optional<GeneratedAP> GenerateEphemeralAnnotAP(
      CPDF_Document* doc,
      const CPDF_Dictionary* annot_dict,
      CPDF_Annot::Subtype subtype);

  static std::optional<GeneratedAP> GenerateEphemeralAnnotAP(
      CPDF_Document* doc,
      const CPDF_Dictionary* annot_dict,
      CPDF_Annot::Subtype subtype,
      BlendMode blend_mode);

  static std::optional<GeneratedAP> GenerateEphemeralFormAP(
      CPDF_Document* doc,
      const CPDF_Dictionary* annot_dict,
      FormType type);

  static bool CanGenerateEphemeralAnnotAP(CPDF_Annot::Subtype subtype);

  static bool GenerateDefaultAppearanceWithColor(CPDF_Document* doc,
                                                 CPDF_Dictionary* annot_dict,
                                                 const CFX_Color& color);

  static bool UpdateDefaultAppearance(CPDF_Document* doc,
                                      CPDF_Dictionary* annot_dict,
                                      CPDF_Annot::StandardFont font,
                                      float font_size,
                                      const CFX_Color& color);

  // EmbedPDF: Set FreeText DA to a registered runtime font. The actual AP path
  // later embeds a subset for only the characters used by the annotation/layer.
  static bool UpdateDefaultAppearanceRegisteredFont(
      CPDF_Document* doc,
      CPDF_Dictionary* annot_dict,
      CFX_FontRegistry::FontId font_id,
      float font_size,
      const CFX_Color& color);

  CPDF_GenerateAP() = delete;
  CPDF_GenerateAP(const CPDF_GenerateAP&) = delete;
  CPDF_GenerateAP& operator=(const CPDF_GenerateAP&) = delete;
};

#endif  // CORE_FPDFDOC_CPDF_GENERATEAP_H_
