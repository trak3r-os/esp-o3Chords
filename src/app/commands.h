#pragma once

#include "app/contracts/contracts.h"
#include "engine/engine.h"

namespace app {

class CommandController {
 public:
  CommandController(contract::Config& config, engine::Engine& engine)
      : config_(config), engine_(engine) {}

  contract::ApplyResult apply(const contract::Command& command);

 private:
  contract::ApplyResult apply_parameter(const contract::Command& command);
  contract::Config& config_;
  engine::Engine& engine_;
};

}  // namespace app
