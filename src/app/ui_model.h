#pragma once

#include "app/contracts/contracts.h"

namespace app::ui_model {

contract::UiModel project(const contract::Config& config,
                          contract::NavigationState navigation = {},
                          bool restart_required = false);
void enter_select(contract::UiModel& model);
void enter_edit(contract::UiModel& model, contract::ParamId parameter);
void back(contract::UiModel& model);

}  // namespace app::ui_model
