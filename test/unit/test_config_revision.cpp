#include "../framework.h"
#include "app/production_ui.h"
#include "app/settings_bridge.h"
#include "app/state.h"
#include "app/joystick_state.h"
#include "engine/engine.h"
#include "midi/router.h"
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"

struct RevisionContext {
  app::AppState state; engine::Engine engine; midi::Router router;
  midi::BleMidi ble{router}; midi::UsbDeviceMidi usb{router}; app::JoystickState js;
  app::UiNavState nav; std::vector<engine::ProgressionStep> steps;
  app::InputContext input{state, engine, router, ble, usb, js, nav, steps, 0};
  app::prod::UiState ui;
  RevisionContext() { state.sanitize(); engine.begin(); engine.set_router(&router);
    app::apply_settings(state, engine, ble, usb); app::prod::begin(ui, input); }
  void edit(app::contract::ParamId id, int8_t delta = 1) {
    ui.navigation_mode = app::contract::NavigationMode::Edit;
    ui.selected_parameter = id; app::prod::encoder_turn(input, ui, delta);
  }
};

static void production_edits_update_one_config_and_revision() {
  RevisionContext c;
  c.edit(app::contract::ParamId::Key);
  c.edit(app::contract::ParamId::Scale);
  c.edit(app::contract::ParamId::Velocity);
  ASSERT_STREQ(c.state.config().key_name, "C#");
  ASSERT_STREQ(c.state.config().scale_name, "Natural Minor");
  ASSERT_TRUE(c.nav.settings_dirty);
  ASSERT_EQ(c.nav.settings_revision, 3u);
  ASSERT_EQ(c.state.config().revision, 3u);
}

static void recipe_and_overrides_are_in_snapshot() {
  // "Recipe" walks the factory presets; default is
  // kLead(0), so one step forward lands on kBass(1). JoystickXTarget is a
  // vestigial wire-compat field no preset touches any more, so it just
  // starts from its own default (1) and takes the delta directly.
  RevisionContext c;
  c.edit(app::contract::ParamId::JoystickRecipe);
  c.edit(app::contract::ParamId::JoystickXTarget, 10);
  auto snapshot = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_EQ(snapshot.config.active_preset, app::PresetId::kBass);
  ASSERT_EQ(snapshot.config.joystick.x_target, 11u);
  ASSERT_EQ(snapshot.settings_revision, 2u);
}

int main() {
  TEST_SUITE("Config edits and revision tracking");
  RUN_TEST(production_edits_update_one_config_and_revision);
  RUN_TEST(recipe_and_overrides_are_in_snapshot);
  test_print_result(); return g_fail ? 1 : 0;
}
