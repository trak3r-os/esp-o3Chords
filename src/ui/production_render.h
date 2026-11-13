#pragma once

#include "app/input_context.h"
#include "app/production_ui.h"
#include "drivers/oled_ssd13xx.h"

namespace ui {

struct ProductionRenderCtx {
  const app::InputContext& input;
  const app::prod::UiState& ui;
};

bool render_boot_screen(drivers::OledSsd13xx& oled);

bool render_production_screen(drivers::OledSsd13xx& oled,
                              const ProductionRenderCtx& ctx);

}  // namespace ui
