// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef FPDFSDK_EPDF_ACTION_HELPERS_H_
#define FPDFSDK_EPDF_ACTION_HELPERS_H_

#include <memory>

#include "public/epdf_action.h"

class CPDF_Action;
class CPDF_Document;

namespace epdf {

struct ActionModelData;
using ActionModelDataPtr = std::shared_ptr<const ActionModelData>;

ActionModelDataPtr BuildActionModel(const CPDF_Action& action,
                                    CPDF_Document* document = nullptr);
EPDF_ACTION_MODEL MakeActionModelHandle(ActionModelDataPtr data);

}  // namespace epdf

#endif  // FPDFSDK_EPDF_ACTION_HELPERS_H_
