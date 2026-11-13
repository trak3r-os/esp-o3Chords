#include "../framework.h"
#include "app/commands.h"

static void test_parameter_is_atomic_and_clamped() { app::contract::Config c{}; engine::Engine e; app::CommandController ctl(c, e); auto r = ctl.apply({app::contract::CommandKind::SetParameter, app::contract::ParamId::ArpOctaves, 99, {}}); ASSERT_TRUE(r.changed); ASSERT_EQ(c.arp_octaves, 4); }
static void test_invalid_joystick_does_not_mutate() { app::contract::Config c{}; engine::Engine e; app::CommandController ctl(c, e); auto old = c.joystick; app::contract::Command cmd{app::contract::CommandKind::SetJoystick}; cmd.joystick.x_min = 5; cmd.joystick.x_max = 5; auto r = ctl.apply(cmd); ASSERT_FALSE(r.changed); ASSERT_EQ(c.joystick.x_min, old.x_min); }
static void test_panic_is_explicit() { app::contract::Config c{}; engine::Engine e; app::CommandController ctl(c, e); auto r = ctl.apply({app::contract::CommandKind::Panic}); ASSERT_TRUE(r.changed); }
int main() { TEST_SUITE("Command boundary"); RUN_TEST(test_parameter_is_atomic_and_clamped); RUN_TEST(test_invalid_joystick_does_not_mutate); RUN_TEST(test_panic_is_explicit); test_print_result(); return g_fail ? 1 : 0; }
