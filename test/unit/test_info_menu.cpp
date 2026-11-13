#include "../framework.h"
#include "app/production_ui.h"
#include "app/config_codec.h"
#include "app/settings_bridge.h"
#include "app/state.h"
#include "app/joystick_state.h"
#include "engine/engine.h"
#include "midi/router.h"
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"
#include <vector>

struct InfoMenuContext {
  app::AppState state; engine::Engine engine; midi::Router router;
  midi::BleMidi ble{router}; midi::UsbDeviceMidi usb{router}; app::JoystickState js;
  app::UiNavState nav; std::vector<engine::ProgressionStep> steps;
  app::InputContext input{state, engine, router, ble, usb, js, nav, steps, 0}; app::prod::UiState ui;
  InfoMenuContext() { state.sanitize(); engine.begin(); engine.set_router(&router); app::apply_settings(state, engine, ble, usb); app::prod::begin(ui, input); }
  void enter_system_select() { app::prod::button_system(input, ui); }
  void select_info_row() {
    // Land on the last System action, then step back to Info — robust to
    // however many actions (Calibrate, etc.) come after it in the list.
    app::prod::encoder_turn(input, ui, 100);
    while (ui.selected_action != app::contract::ConfirmationAction::Info &&
           ui.selected_action != app::contract::ConfirmationAction::None) {
      app::prod::encoder_turn(input, ui, -1);
    }
  }
};

static void test_info_row_is_reachable_and_marked_selected() {
  InfoMenuContext c; c.enter_system_select(); c.select_info_row();
  ASSERT_EQ(c.ui.selected_action, app::contract::ConfirmationAction::Info);
  auto m = app::prod::model_snapshot(c.input, c.ui);
  bool found = false;
  for (uint8_t i = 0; i < m.row_count; ++i) {
    if (m.rows[i].action != app::contract::ConfirmationAction::Info) continue;
    found = true;
    ASSERT_TRUE(m.rows[i].selected);
  }
  ASSERT_TRUE(found);
}

static void test_click_opens_banner_then_details_then_closes() {
  InfoMenuContext c; c.enter_system_select(); c.select_info_row();
  app::prod::encoder_click(c.input, c.ui);
  ASSERT_TRUE(c.ui.info_open); ASSERT_EQ(c.ui.info_page, 0u);
  auto m = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_TRUE(m.info_open); ASSERT_EQ(m.info_page, 0u);

  app::prod::encoder_click(c.input, c.ui);
  ASSERT_TRUE(c.ui.info_open); ASSERT_EQ(c.ui.info_page, 1u);

  app::prod::encoder_click(c.input, c.ui);
  ASSERT_FALSE(c.ui.info_open); ASSERT_EQ(c.ui.info_page, 0u);
  // Closing returns to the System row list, not back to Play.
  ASSERT_TRUE(c.ui.navigation_mode == app::contract::NavigationMode::Select);
}

static void test_back_closes_info_from_either_page() {
  InfoMenuContext c; c.enter_system_select(); c.select_info_row();
  app::prod::encoder_click(c.input, c.ui);  // open banner (page 0)
  app::prod::button_back(c.input, c.ui);
  ASSERT_FALSE(c.ui.info_open);
  ASSERT_TRUE(c.ui.navigation_mode == app::contract::NavigationMode::Select);

  c.select_info_row();
  app::prod::encoder_click(c.input, c.ui);  // page 0
  app::prod::encoder_click(c.input, c.ui);  // page 1
  app::prod::button_back(c.input, c.ui);
  ASSERT_FALSE(c.ui.info_open); ASSERT_EQ(c.ui.info_page, 0u);
}

static void test_info_never_enters_confirmation_flow() {
  InfoMenuContext c; c.enter_system_select(); c.select_info_row();
  app::prod::encoder_click(c.input, c.ui);
  ASSERT_TRUE(c.ui.confirmation_action == app::contract::ConfirmationAction::None);
  auto m = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_TRUE(m.confirmation.action == app::contract::ConfirmationAction::None);
}

static void test_encoder_turn_is_ignored_while_info_open() {
  InfoMenuContext c; c.enter_system_select(); c.select_info_row();
  app::prod::encoder_click(c.input, c.ui);
  const auto page_before = c.ui.info_page;
  app::prod::encoder_turn(c.input, c.ui, 3);
  ASSERT_EQ(c.ui.info_page, page_before);
  ASSERT_TRUE(c.ui.info_open);
}

int main() {
  TEST_SUITE("System / Info menu entry");
  RUN_TEST(test_info_row_is_reachable_and_marked_selected);
  RUN_TEST(test_click_opens_banner_then_details_then_closes);
  RUN_TEST(test_back_closes_info_from_either_page);
  RUN_TEST(test_info_never_enters_confirmation_flow);
  RUN_TEST(test_encoder_turn_is_ignored_while_info_open);
  test_print_result();
  return g_fail ? 1 : 0;
}
