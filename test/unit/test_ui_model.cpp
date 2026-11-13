#include "../framework.h"
#include "app/ui_model.h"

static void test_play_select_edit_back_preserves_config() { app::contract::Config c{}; c.velocity = 77; auto m = app::ui_model::project(c); app::ui_model::enter_select(m); ASSERT_EQ(m.navigation.mode, app::contract::NavigationMode::Select); app::ui_model::enter_edit(m, app::contract::ParamId::Velocity); ASSERT_EQ(m.navigation.parameter, app::contract::ParamId::Velocity); app::ui_model::back(m); ASSERT_EQ(m.navigation.mode, app::contract::NavigationMode::Play); ASSERT_EQ(m.config.velocity, 77); }
static void test_projection_does_not_mutate_runtime() { app::contract::Config c{}; auto m = app::ui_model::project(c, {}, true); ASSERT_TRUE(m.restart_required); ASSERT_EQ(m.config.arp_octaves, 1); }
int main() { TEST_SUITE("UiModel navigation"); RUN_TEST(test_play_select_edit_back_preserves_config); RUN_TEST(test_projection_does_not_mutate_runtime); test_print_result(); return g_fail ? 1 : 0; }
