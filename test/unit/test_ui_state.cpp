#include "../framework.h"
#include "app/production_ui.h"
#include "app/state.h"
#include "engine/engine.h"
#include "midi/router.h"
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"
#include <vector>

struct UiStateContext {
  app::AppState state; engine::Engine engine; midi::Router router;
  midi::BleMidi ble{router}; midi::UsbDeviceMidi usb{router}; app::JoystickState js;
  app::UiNavState nav; std::vector<engine::ProgressionStep> steps;
  app::InputContext input{state, engine, router, ble, usb, js, nav, steps, 0}; app::prod::UiState ui;
  UiStateContext() { state.sanitize(); engine.begin(); engine.set_router(&router); app::apply_settings(state, engine, ble, usb); app::prod::begin(ui, input); }
};

static void test_only_canonical_navigation_states_exist() {
  UiStateContext c; ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Play);
  app::prod::encoder_click(c.input, c.ui); ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Select);
  app::prod::encoder_click(c.input, c.ui); ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Edit);
  app::prod::button_back(c.input, c.ui); ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Select);
  app::prod::button_back(c.input, c.ui); ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Play);
}

static void test_advanced_is_inline_and_edit_is_live() {
  UiStateContext c; app::prod::encoder_click(c.input, c.ui); const auto old = c.ui.advanced_expanded;
  app::prod::encoder_long_click(c.input, c.ui); ASSERT_NE(c.ui.advanced_expanded, old);
  app::prod::encoder_click(c.input, c.ui); app::prod::encoder_turn(c.input, c.ui, 1);
  ASSERT_TRUE(c.input.nav.dirty);
}

int main() { TEST_SUITE("Canonical UI state"); RUN_TEST(test_only_canonical_navigation_states_exist); RUN_TEST(test_advanced_is_inline_and_edit_is_live); test_print_result(); return g_fail ? 1 : 0; }
