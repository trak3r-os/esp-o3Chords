#include "app/ui_model.h"

namespace app::ui_model {

contract::UiModel project(const contract::Config& config,
                          contract::NavigationState navigation,
                          bool restart_required) {
  contract::UiModel model{};
  model.config = config;
  model.navigation = navigation;
  model.restart_required = restart_required;
  return model;
}

void enter_select(contract::UiModel& model) {
  model.navigation.mode = contract::NavigationMode::Select;
}

void enter_edit(contract::UiModel& model, contract::ParamId parameter) {
  model.navigation.mode = contract::NavigationMode::Edit;
  model.navigation.parameter = parameter;
}

void back(contract::UiModel& model) {
  // Applied edits are live; leaving EDIT never restores an old snapshot.
  model.navigation.mode = contract::NavigationMode::Play;
  model.confirmation = {};
}

}  // namespace app::ui_model
