#include "../framework.h"

#include "app/config_codec.h"
#include "app/input_joystick.h"
#include "app/parameters.h"
#include "app/production_ui.h"
#include "app/progression_modes.h"
#include "app/settings_bridge.h"
#include "app/state.h"
#include "app/joystick_state.h"
#include "drivers/joystick_analog.h"
#include "engine/engine.h"
#include "midi/router.h"
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"
#include "ui/production_render.h"
#include "golden.h"
#include "joy_stub.h"

#include <cstdio>
#include <string>
#include <vector>

struct UiContext {
  app::AppState state; engine::Engine engine; midi::Router router;
  midi::BleMidi ble{router}; midi::UsbDeviceMidi usb{router}; app::JoystickState js;
  app::UiNavState nav; std::vector<engine::ProgressionStep> steps;
  app::InputContext input{state, engine, router, ble, usb, js, nav, steps, 0};
  app::prod::UiState ui; drivers::JoystickAnalog joystick; drivers::OledSsd13xx oled;

  UiContext() {
    state.sanitize(); engine.begin(); engine.set_router(&router);
    app::apply_settings(state, engine, ble, usb); app::prod::begin(ui, input);
    app::apply_progression_mode(engine, app::ProgressionMode::kMajor, state.settings());
    stub::oled_reset();
  }

  void edit(app::contract::ParamId id, int8_t delta = 1) {
    ui.navigation_mode = app::contract::NavigationMode::Edit;
    ui.selected_parameter = id;
    app::prod::encoder_turn(input, ui, delta);
  }

  // Recalls a preset through the Recipe row, as rotating the encoder does.
  void preset(app::PresetId id) {
    for (int i = 0; i < 8 && state.config().active_preset != id; ++i)
      edit(app::contract::ParamId::JoystickRecipe, 1);
    ui.navigation_mode = app::contract::NavigationMode::Play;
    ui.joystick_mode = true;
  }

  void render() {
    stub::oled_reset();
    ASSERT_TRUE(ui::render_production_screen(oled, {input, ui}));
    ASSERT_TRUE(stub::oled_flushed);
  }
};

static bool line_contains(const char* needle) {
  return stub::screen_contains(needle);
}

static bool has_selected_value(const app::contract::UiModel& model,
                               app::contract::ParamId id, const char* value) {
  for (uint8_t i = 0; i < model.row_count; ++i)
    if (model.rows[i].parameter == id && model.rows[i].selected &&
        std::string(model.rows[i].value) == value) return true;
  return false;
}

static void navigation_filters_and_renderer_follow_selection() {
  UiContext c;
  c.input.nav.active_degree = 1;
  app::prod::button_menu(c.input, c.ui);  // Play -> Select (`#`; encoder push now steps the PLAY cursor instead).
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Select);
  ASSERT_EQ(c.ui.parameter_filter, app::contract::ParameterFilter::Chord);

  app::prod::encoder_turn(c.input, c.ui, 12);  // BPM in the chord list (Voicing now precedes it).
  ASSERT_EQ(c.ui.selected_parameter, app::contract::ParamId::Bpm);
  auto model = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_TRUE(has_selected_value(model, app::contract::ParamId::Bpm, "120 BPM"));
  c.render();
  ASSERT_TRUE(line_contains("BPM"));

  app::prod::button_system(c.input, c.ui);  // Jumps straight to Select + System.
  ASSERT_EQ(c.ui.parameter_filter, app::contract::ParameterFilter::System);
  model = app::prod::model_snapshot(c.input, c.ui);
  bool panic = false;
  for (uint8_t i = 0; i < model.row_count; ++i)
    panic |= model.rows[i].action == app::contract::ConfirmationAction::Panic;
  ASSERT_TRUE(panic);

  // Actions are actual selectable rows and require a second confirmation click.
  app::prod::encoder_turn(c.input, c.ui, 100);
  app::prod::encoder_turn(c.input, c.ui, -4);  // Reset row (last is Calibrate, then Info, DFU, Reboot, Reset).
  ASSERT_EQ(c.ui.selected_action, app::contract::ConfirmationAction::Reset);
  app::prod::encoder_click(c.input, c.ui);  // arm the confirmation
  c.render();
  ASSERT_TRUE(line_contains("CONFIRM"));
  ASSERT_TRUE(line_contains("Restore defaults?"));
  app::prod::encoder_turn(c.input, c.ui, 1);  // turn right: select YES
  app::prod::encoder_click(c.input, c.ui);  // execute reset
  ASSERT_EQ(c.state.config().joystick.recipe_id, app::contract::RecipeId::Expression);
  // Reset restores the factory defaults, not a bare Config{}.
  ASSERT_EQ(c.state.config().velocity, 110);
  ASSERT_EQ(c.state.config().usb_role, app::contract::UsbRole::Device);
  app::prod::encoder_turn(c.input, c.ui, 2);  // past Reboot to the DFU row.
  model = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_EQ(c.ui.selected_action, app::contract::ConfirmationAction::Dfu);
  bool dfu_row_selected = false;
  for (uint8_t i = 0; i < model.row_count; ++i)
    if (model.rows[i].selected) dfu_row_selected = model.rows[i].action == app::contract::ConfirmationAction::Dfu;
  ASSERT_TRUE(dfu_row_selected);
  app::prod::encoder_click(c.input, c.ui);  // arm the confirmation
  model = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_EQ(model.confirmation.action, app::contract::ConfirmationAction::Dfu);
  c.render();
  ASSERT_TRUE(line_contains("CONFIRM"));
  app::prod::encoder_turn(c.input, c.ui, 1);  // turn right: select YES
  app::prod::encoder_click(c.input, c.ui);  // execute DFU request
  ASSERT_TRUE(c.ui.dfu_reboot_pending);
}

static void paramid_edits_reach_config_settings_and_engine() {
  UiContext c;
  c.edit(app::contract::ParamId::Bpm, 1);
  c.edit(app::contract::ParamId::Key, 1);
  c.edit(app::contract::ParamId::Scale, 1);
  c.edit(app::contract::ParamId::ChordChannel, 2);
  c.edit(app::contract::ParamId::LeadChannel, 3);
  ASSERT_EQ(c.state.config().bpm, 121.0f);
  ASSERT_EQ(c.state.config().key, 1u);
  ASSERT_EQ(c.state.config().scale, 1u);
  ASSERT_EQ(c.state.config().chord_channel, 2u);
  ASSERT_EQ(c.state.config().lead_channel, 3u);
  ASSERT_STREQ(c.state.config().key_name, "C#");
  ASSERT_STREQ(c.state.config().scale_name, "Natural Minor");
  ASSERT_STREQ(c.state.settings().key, "C#");
  ASSERT_STREQ(c.state.settings().scale, "Natural Minor");
  ASSERT_STREQ(c.engine.progression().steps()[0].chord_quality.c_str(), "min");
  ASSERT_EQ(c.engine.channel(), 2u);
  ASSERT_EQ(c.engine.lead_channel(), 3u);
}

// Regression: apply_progression_mode() was once only called at boot with the
// hardcoded kMajor default, so 21 of the 22 ProgressionMode presets were
// unreachable from the UI. Scale walks all 22, and each one both retunes the engine's 7
// degree qualities and updates the display name.
static void scale_param_reaches_every_genre_mode() {
  UiContext c;
  for (int i = 0; i < 21; ++i) c.edit(app::contract::ParamId::Scale, 1);
  ASSERT_EQ(c.state.config().scale, 21u);
  ASSERT_STREQ(c.state.config().scale_name, "Flamenco");
  // Genre presets run on the major scale for chord-tone resolution
  // (progression_modes.cpp) — only the display name changes, not the
  // engine-facing scale string.
  ASSERT_STREQ(c.state.settings().scale, "Major");
  const auto& steps = c.engine.progression().steps();
  ASSERT_EQ(static_cast<int>(steps.size()), 7);
  ASSERT_EQ(steps[0].scale_degree, 1);
  ASSERT_STREQ(steps[0].chord_quality.c_str(), "min");
  // Scale does not wrap; one more step forward stays clamped at the last mode.
  c.edit(app::contract::ParamId::Scale, 1);
  ASSERT_EQ(c.state.config().scale, 21u);
}

static void recipe_overrides_keypad_and_home_are_real() {
  // "Recipe" walks the factory presets; default is
  // kLead(0), so one step forward lands on kBass(1). JoystickXTarget/YTarget/
  // Curve are vestigial wire-compat fields no preset touches any more, so
  // each just starts from its own default and takes the delta directly.
  UiContext c;
  c.edit(app::contract::ParamId::JoystickRecipe, 1);  // Lead -> Bass.
  c.edit(app::contract::ParamId::JoystickXTarget, 10);
  c.edit(app::contract::ParamId::JoystickYTarget, 5);
  c.edit(app::contract::ParamId::JoystickCurve, 1);
  c.edit(app::contract::ParamId::KeypadAsJoy, 1);
  auto model = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_EQ(model.config.active_preset, app::PresetId::kBass);
  ASSERT_EQ(model.config.joystick.x_target, 11u);
  ASSERT_EQ(model.config.joystick.y_target, 16u);
  ASSERT_EQ(model.config.joystick.curve, 1u);
  ASSERT_TRUE(model.config.joystick.keypad_as_joy);
  ASSERT_TRUE(std::string(model.joystick_legend).find("Bass") != std::string::npos);

  c.ui.navigation_mode = app::contract::NavigationMode::Play;
  c.render();
  ASSERT_TRUE(line_contains("C Major"));
}

// Golden framebuffer snapshot for the play screen: key/scale header, the sounding chord with its degree, tension
// and notes, the register readout, the chord-name pads with the 8/9
// inversion keys, and the focused-setting footer. Degree I held and sounding.
static void play_screen_matches_golden() {
  UiContext c;
  auto cfg = c.state.config();
  cfg.play_trigger = 1;  // Hold.
  cfg.roll_ms = 18;
  // Explicit, not assumed: a fresh Config comes from Settings' defaults
  // (velocity 110), not Config{}'s own member default.
  cfg.velocity = 100;
  ASSERT_TRUE(app::commit_config(c.input, cfg));
  app::prod::process_keypad_events(c.input, c.ui, {{0, true}});  // Degree I, held.
  auto model = app::prod::model_snapshot(c.input, c.ui);
  // Pad names at the model level: direct and unambiguous, unlike searching
  // rendered pixels for a non-sounding pad's text, whose digit is knocked
  // out separately from its name.
  ASSERT_STREQ(model.pad_symbols[0], "C");
  ASSERT_STREQ(model.pad_symbols[1], "Dm");
  ASSERT_STREQ(model.pad_symbols[2], "Em");
  ASSERT_STREQ(model.pad_symbols[3], "F");
  ASSERT_STREQ(model.pad_symbols[4], "G");
  ASSERT_STREQ(model.pad_symbols[5], "Am");
  ASSERT_STREQ(model.pad_symbols[6], "Bo");
  ASSERT_STREQ(model.inversion_name, "AUTO");
  ASSERT_STREQ(model.inversion_code, "AR");  // voice-led; the first chord is root position

  c.render();
  // Content checks independent of exact pixel position, so a golden update
  // still has something catching a wrong VALUE (not just a moved pixel).
  ASSERT_TRUE(line_contains("C Major"));     // row 0: key and scale
  ASSERT_TRUE(line_contains("I T0"));        // degree and tension
  ASSERT_TRUE(line_contains("O+0 I:AR"));    // register readout
  ASSERT_TRUE(line_contains("C E G"));       // sounding notes
  ASSERT_TRUE(line_contains("1C"));          // pad 1, sounding: knocked out whole
  ASSERT_TRUE(line_contains("Dm"));
  ASSERT_TRUE(line_contains("8 Inv-"));
  ASSERT_TRUE(line_contains("9 Inv+"));
  ASSERT_TRUE(line_contains("Velocity"));    // footer: the PLAY cursor's default slot
  ASSERT_TRUE(line_contains("100"));
  ASSERT_FALSE(line_contains("READY"));

  std::string message;
  const bool ok = golden::check("play_screen", golden::dump_screen(c.oled), message);
  if (!ok) std::fprintf(stderr, "  %s\n", message.c_str());
  ASSERT_TRUE(ok);
}

// The footer follows the PLAY cursor: it names the focused setting and shows
// its live value, so every Config-backed value is reachable without a menu.
static void play_screen_footer_follows_the_cursor() {
  UiContext c;
  auto cfg = c.state.config();
  cfg.roll_ms = 18;
  ASSERT_TRUE(app::commit_config(c.input, cfg));
  c.render();
  ASSERT_TRUE(line_contains("READY"));
  ASSERT_TRUE(line_contains("Press a chord key"));
  ASSERT_TRUE(line_contains("O+0 I:A"));
  ASSERT_TRUE(line_contains("Velocity"));
  ASSERT_TRUE(line_contains("110"));

  app::prod::button_bak(c.input, c.ui);  // Velocity -> Roll
  c.render();
  ASSERT_TRUE(line_contains("Roll"));
  ASSERT_TRUE(line_contains("18ms"));
  ASSERT_FALSE(line_contains("Velocity"));

  app::prod::encoder_click(c.input, c.ui);  // -> Velocity
  app::prod::encoder_click(c.input, c.ui);  // -> Trigger
  app::prod::encoder_click(c.input, c.ui);  // -> Duration
  app::prod::encoder_turn(c.input, c.ui, 1);
  c.render();
  ASSERT_TRUE(line_contains("Duration"));
  ASSERT_TRUE(line_contains("250ms"));
}

// An armed colour is announced on the play screen, right-aligned on row 0,
// and the next chord key plays with it.
static void play_screen_shows_an_armed_colour() {
  UiContext c;
  c.preset(app::PresetId::kColors);
  app::dispatch_keypad_joy_direction(c.input, 1);  // NE -> add9
  app::dispatch_keypad_joy_direction(c.input, -1);
  ASSERT_STREQ(c.engine.modifier().quality_name(), "add9");
  c.ui.joystick_mode = false;
  c.render();
  ASSERT_TRUE(line_contains("+add9"));
  app::prod::process_keypad_events(c.input, c.ui, {{1, true}});  // Degree II.
  auto model = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_STREQ(model.active_chord_symbol, "Dadd9");
  ASSERT_STREQ(model.pad_symbols[1], "Dm");  // the pads keep showing the scale's own chords
  c.render();
  ASSERT_TRUE(line_contains("+add9"));
  ASSERT_TRUE(line_contains("Dadd9"));
  ASSERT_TRUE(line_contains("D F# A E"));

  // Disarming removes the tag.
  app::dispatch_keypad_joy_direction(c.input, 1);
  app::dispatch_keypad_joy_direction(c.input, -1);
  c.render();
  ASSERT_FALSE(line_contains("+add9"));
}

// Row 0 is "key scale" left-aligned against the roman-numeral/tension token
// right-aligned, inside a 21-character budget (production_render.cpp's
// pair()). "Harmonic Minor" (14 chars) is now the longest reachable name,
// one longer than "Natural Minor". pair() clips the left string rather than
// overflowing, so with a chord held (the tightest case — the right side
// takes its own space) the tail of the name is what gives, not the layout.
static void play_screen_row0_survives_the_longest_scale_name() {
  UiContext c;
  for (int i = 0; i < 2; ++i) c.edit(app::contract::ParamId::Scale, 1);  // -> Harmonic Minor
  ASSERT_STREQ(c.state.config().scale_name, "Harmonic Minor");
  c.ui.navigation_mode = app::contract::NavigationMode::Play;  // edit() above left it in Edit.
  app::prod::process_keypad_events(c.input, c.ui, {{0, true}});  // Degree I, held.
  c.render();
  ASSERT_TRUE(line_contains("C"));         // key
  ASSERT_TRUE(line_contains("Harmonic"));  // scale name is not dropped outright
  ASSERT_TRUE(line_contains("C Harmonic Minor"));  // whole name fits on row 0
  ASSERT_TRUE(line_contains("i T0"));      // degree and tension sit on their own row
}

// Golden framebuffer snapshots for the
// two screens draw_model() renders generically for every ParamId rather than
// per-family: the Select-mode row list (production_render.cpp's `listScreen`
// block, the PLAY EDIT/SYSTEM row picker) and the Edit-mode single-value
// editor (`valueEditor`, the large centered value with a min/max bar).
static void select_list_screen_matches_golden() {
  UiContext c;
  c.input.nav.active_degree = 1;
  app::prod::button_menu(c.input, c.ui);  // Play -> Select, Chord filter, default row = Velocity.
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Select);
  ASSERT_EQ(c.ui.parameter_filter, app::contract::ParameterFilter::Chord);
  c.render();
  ASSERT_TRUE(line_contains("PLAY EDIT"));
  ASSERT_TRUE(line_contains("Velocity"));
  ASSERT_TRUE(line_contains("1/"));  // row counter, top-right of the header

  std::string message;
  const bool ok = golden::check("select_list_screen", golden::dump_screen(c.oled), message);
  if (!ok) std::fprintf(stderr, "  %s\n", message.c_str());
  ASSERT_TRUE(ok);
}

static void edit_value_screen_matches_golden() {
  UiContext c;
  c.edit(app::contract::ParamId::Velocity, 5);  // Select -> Edit leaves navigation_mode = Edit.
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Edit);
  c.render();
  ASSERT_TRUE(line_contains("PLAY EDIT"));
  ASSERT_TRUE(line_contains("Velocity"));

  std::string message;
  const bool ok = golden::check("edit_value_screen", golden::dump_screen(c.oled), message);
  if (!ok) std::fprintf(stderr, "  %s\n", message.c_str());
  ASSERT_TRUE(ok);
}

// Golden framebuffer snapshots for the
// two NOTES joystick bodies — kBass (gesture=clock, the compass) and kLead
// (gesture=walk, the scale strip).
static void notes_clock_screen_matches_golden() {
  UiContext c;
  c.edit(app::contract::ParamId::JoystickRecipe, 1);  // Lead -> Bass (clock).
  ASSERT_EQ(c.state.config().active_preset, app::PresetId::kBass);
  ASSERT_TRUE(c.state.config().joy_families.notes.gesture == app::NoteGesture::kClock);
  c.ui.joystick_mode = true;
  c.ui.navigation_mode = app::contract::NavigationMode::Play;  // edit() above left it in Edit.
  c.render();
  ASSERT_TRUE(line_contains("NOTES"));
  ASSERT_TRUE(line_contains("Bass"));   // Recipe, knocked out on row 1
  ASSERT_TRUE(line_contains("Scale"));  // the push-reachable parameter: Source
  ASSERT_TRUE(line_contains("C2"));     // N: the root, two octaves down (octave 4 = MIDI 60)
  ASSERT_TRUE(line_contains("E2"));     // E
  ASSERT_TRUE(line_contains("Mono O-2 ch1"));
  ASSERT_TRUE(line_contains("v100"));   // Vel mode = Fixed: the fixed velocity
  ASSERT_FALSE(line_contains("vDist"));
  ASSERT_TRUE(line_contains("C Major"));
  ASSERT_TRUE(line_contains("O+0 I:A"));

  std::string message;
  const bool ok = golden::check("notes_clock_screen", golden::dump_screen(c.oled), message);
  if (!ok) std::fprintf(stderr, "  %s\n", message.c_str());
  ASSERT_TRUE(ok);

  // Vel mode = Distance replaces the fixed velocity with "vDist".
  c.edit(app::contract::ParamId::NotesVelMode, 1);
  c.ui.navigation_mode = app::contract::NavigationMode::Play;
  c.render();
  ASSERT_TRUE(line_contains("vDist"));
  ASSERT_FALSE(line_contains("v100"));
}

static void notes_walk_screen_matches_golden() {
  UiContext c;
  // A fresh Config already has Lead recalled (source=scale, gesture=walk):
  // no Recipe round trip is needed to get the preset's own parameters.
  ASSERT_EQ(c.state.config().active_preset, app::PresetId::kLead);
  ASSERT_TRUE(c.state.config().joy_families.notes.source == app::NoteSource::kScale);
  ASSERT_TRUE(c.state.config().joy_families.notes.gesture == app::NoteGesture::kWalk);
  ASSERT_TRUE(c.state.config().joy_families.notes.velocity_mode == app::NoteVelocityMode::kByDistance);
  ASSERT_TRUE(c.state.config().joy_families.notes.channel == app::NoteChannel::kLead);
  c.ui.joystick_mode = true;
  c.render();
  ASSERT_TRUE(line_contains("NOTES"));
  ASSERT_TRUE(line_contains("Lead"));   // Recipe, knocked out on row 1
  ASSERT_TRUE(line_contains("Scale"));
  ASSERT_TRUE(line_contains("C4"));     // the walk starts on middle C
  ASSERT_TRUE(line_contains("^ D4"));
  ASSERT_TRUE(line_contains("v C5"));
  ASSERT_TRUE(line_contains("B4"));     // the strip is one octave, C4..C5
  ASSERT_TRUE(line_contains("< Vel >"));
  ASSERT_TRUE(line_contains("v100"));

  std::string message;
  const bool ok = golden::check("notes_walk_screen", golden::dump_screen(c.oled), message);
  if (!ok) std::fprintf(stderr, "  %s\n", message.c_str());
  ASSERT_TRUE(ok);
}

// Golden framebuffer snapshot for CONT (kMacro: the 2D pad with one readout
// per axis).
static void cont_screen_matches_golden() {
  UiContext c;
  c.edit(app::contract::ParamId::JoystickRecipe, 7);  // Lead -> Macro (CONT).
  ASSERT_EQ(c.state.config().active_preset, app::PresetId::kMacro);
  c.ui.joystick_mode = true;
  c.ui.navigation_mode = app::contract::NavigationMode::Play;
  c.render();
  ASSERT_TRUE(line_contains("CONT"));
  ASSERT_TRUE(line_contains("Macro"));  // Recipe, knocked out on row 1
  ASSERT_TRUE(line_contains("X CC1"));
  ASSERT_TRUE(line_contains("Y CC11"));
  ASSERT_TRUE(line_contains("Curve lin"));
  ASSERT_TRUE(line_contains("DZ 10%"));  // the ContDeadzone parameter

  std::string message;
  const bool ok = golden::check("cont_screen", golden::dump_screen(c.oled), message);
  if (!ok) std::fprintf(stderr, "  %s\n", message.c_str());
  ASSERT_TRUE(ok);

  // The row follows the parameters it names.
  c.edit(app::contract::ParamId::ContDeadzone, 15);
  c.edit(app::contract::ParamId::ContCurve, 1);
  c.edit(app::contract::ParamId::ContXTarget, 1);  // CC -> Aftertouch
  c.ui.navigation_mode = app::contract::NavigationMode::Play;
  c.render();
  ASSERT_TRUE(line_contains("DZ 25%"));
  ASSERT_TRUE(line_contains("Curve exp"));
  ASSERT_FALSE(line_contains("X CC1"));
}

// Golden framebuffer snapshot for CHORD/Colors: the 8-way compass of
// qualities. A direction arms its quality for the next chord key; the armed
// cell is underlined and row 6 names what the next chord will get.
static void chord_colors_screen_matches_golden() {
  UiContext c;
  c.preset(app::PresetId::kColors);
  ASSERT_EQ(c.state.config().active_preset, app::PresetId::kColors);
  c.render();
  ASSERT_TRUE(line_contains("CHORD"));
  ASSERT_TRUE(line_contains("Colors"));  // Recipe, knocked out on row 1
  ASSERT_TRUE(line_contains("CLSC"));    // the push-reachable parameter: Palette
  ASSERT_TRUE(line_contains("Next chord"));
  ASSERT_TRUE(line_contains("as scale"));   // nothing armed
  ASSERT_FALSE(line_contains("Retrig"));

  app::prod::process_keypad_events(c.input, c.ui, {{0, true}});  // Degree I, held.
  app::dispatch_keypad_joy_direction(c.input, 1);   // NE -> add9, armed...
  app::dispatch_keypad_joy_direction(c.input, -1);  // ...and it stays armed at centre.
  ASSERT_STREQ(c.engine.modifier().quality_name(), "add9");
  c.render();
  ASSERT_TRUE(line_contains("Next chord"));
  ASSERT_FALSE(line_contains("as scale"));
  ASSERT_TRUE(line_contains("maj7"));
  ASSERT_TRUE(line_contains("sus4"));
  ASSERT_TRUE(line_contains("O+0 I:AR"));

  std::string message;
  const bool ok = golden::check("chord_colors_screen", golden::dump_screen(c.oled), message);
  if (!ok) std::fprintf(stderr, "  %s\n", message.c_str());
  ASSERT_TRUE(ok);
}

// CHORD/Voicings shares the compass, with voicing names in the cells and
// row 6 reading the Voicing parameter the stick writes.
static void chord_voicings_screen_names_the_voicing() {
  UiContext c;
  c.preset(app::PresetId::kVoicings);
  c.render();
  ASSERT_TRUE(line_contains("CHORD"));
  ASSERT_TRUE(line_contains("Voicings"));
  ASSERT_TRUE(line_contains("Voicing"));
  ASSERT_TRUE(line_contains("Default"));
  ASSERT_TRUE(line_contains("Drop2"));
  ASSERT_TRUE(line_contains("Guitar"));
  ASSERT_FALSE(line_contains("Next chord"));
  ASSERT_FALSE(line_contains("Retrig"));

  app::dispatch_keypad_joy_direction(c.input, 6);   // S -> Open
  app::dispatch_keypad_joy_direction(c.input, -1);
  ASSERT_EQ(c.state.config().voicing_spread, static_cast<uint8_t>(engine::VoicingSpread::kOpen));
  c.render();
  ASSERT_TRUE(line_contains("Voicing"));
  ASSERT_TRUE(line_contains("Open"));
  // The play screen's Voicing slot reads the same value.
  c.ui.joystick_mode = false;
  while (app::prod::model_snapshot(c.input, c.ui).navigation.parameter != app::contract::ParamId::Voicing)
    app::prod::encoder_click(c.input, c.ui);
  c.render();
  ASSERT_TRUE(line_contains("Voicing"));
  ASSERT_TRUE(line_contains("Open"));
}

// CHORD/Discover: the ranked compass, on the 7THS palette.
static void chord_discover_screen_shows_the_ranked_palette() {
  UiContext c;
  c.preset(app::PresetId::kDiscover);
  app::prod::process_keypad_events(c.input, c.ui, {{0, true}});
  c.render();
  ASSERT_TRUE(line_contains("CHORD"));
  ASSERT_TRUE(line_contains("Discover"));
  ASSERT_TRUE(line_contains("7THS"));
  ASSERT_TRUE(line_contains("Next chord"));
  ASSERT_TRUE(line_contains("as scale"));
}

// The wheel: ParamId::Key's own editor, in place of the generic bar-graph
// value screen. Degree I made dominant (a foreign Bb in C major) gives
// suggest_modulation() a real destination to suggest, and pivot_chord_mask()
// a real pivot chord to name.
static void key_wheel_shows_modulation_hint_for_a_foreign_chord() {
  UiContext c;
  c.engine.progression().steps_mutable()[0].chord_quality = "dom7";  // I7: C E G Bb.
  c.input.nav.active_degree = 1;
  c.ui.navigation_mode = app::contract::NavigationMode::Edit;
  c.ui.selected_parameter = app::contract::ParamId::Key;
  c.render();
  ASSERT_TRUE(line_contains("KEY"));
  ASSERT_TRUE(line_contains("C"));       // current key, knocked out on row 0
  ASSERT_TRUE(line_contains("->"));      // a real destination was found
  ASSERT_TRUE(line_contains("via"));     // ... with a named pivot chord
  ASSERT_FALSE(line_contains("Hold chord for hints"));  // not the no-suggestion fallback
}

// With nothing sounding there is nothing foreign, so suggest_modulation()
// gets no input and the wheel says so rather than showing a stale hint.
static void key_wheel_falls_back_without_a_sounding_chord() {
  UiContext c;
  c.input.nav.active_degree = -1;
  c.ui.navigation_mode = app::contract::NavigationMode::Edit;
  c.ui.selected_parameter = app::contract::ParamId::Key;
  c.render();
  ASSERT_TRUE(line_contains("KEY"));
  ASSERT_TRUE(line_contains("circle of fifths"));
  ASSERT_TRUE(line_contains("Hold chord for hints"));
  ASSERT_FALSE(line_contains("via"));
}

// End-to-end: the joystick edit menu (# from a JOY-mode play screen) must
// list the active family's own parameters, not the old fixed 4-row
// Joystick block — this is the real UiState -> model_snapshot() path the
// OLED renders from, not just parameters::visible() in isolation.
static void joystick_edit_menu_lists_the_active_familys_rows() {
  UiContext c;
  c.ui.joystick_mode = true;
  app::prod::button_menu(c.input, c.ui);  // Play -> Select, filter = Joystick
  auto model = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_EQ(model.config.active_preset, app::PresetId::kLead);  // NOTES family

  bool saw_notes_source = false, saw_joystick_x = false, saw_chord_palette = false;
  for (uint8_t i = 0; i < model.row_count; ++i) {
    if (model.rows[i].parameter == app::contract::ParamId::NotesSource) saw_notes_source = true;
    if (model.rows[i].parameter == app::contract::ParamId::JoystickXTarget) saw_joystick_x = true;
    if (model.rows[i].parameter == app::contract::ParamId::ChordPalette) saw_chord_palette = true;
  }
  ASSERT_TRUE(saw_notes_source);
  ASSERT_FALSE(saw_joystick_x);   // vestigial, hidden from every menu now
  ASSERT_FALSE(saw_chord_palette);  // wrong family — NOTES is active, not CHORD

  // Recipe stays first every time — the encoder always starts on it.
  ASSERT_TRUE(model.row_count > 0);
  ASSERT_EQ(model.rows[0].parameter, app::contract::ParamId::JoystickRecipe);

  // Switching Recipe to a CHORD preset must swap the whole block, not
  // accumulate NOTES rows alongside CHORD ones. kLead(0) -> kColors(3).
  c.edit(app::contract::ParamId::JoystickRecipe, 3);
  ASSERT_EQ(c.state.config().active_preset, app::PresetId::kColors);
  c.ui.navigation_mode = app::contract::NavigationMode::Select;
  c.ui.parameter_filter = app::contract::ParameterFilter::Joystick;
  model = app::prod::model_snapshot(c.input, c.ui);
  saw_notes_source = false; saw_chord_palette = false;
  for (uint8_t i = 0; i < model.row_count; ++i) {
    if (model.rows[i].parameter == app::contract::ParamId::NotesSource) saw_notes_source = true;
    if (model.rows[i].parameter == app::contract::ParamId::ChordPalette) saw_chord_palette = true;
  }
  ASSERT_FALSE(saw_notes_source);
  ASSERT_TRUE(saw_chord_palette);
  // CHORD's menu block is Palette alone: Order/Retrigger are per-preset
  // constants, and the removed Explore/Moves/Apply/OnCenter rows are gone.
  for (uint8_t i = 0; i < model.row_count; ++i) {
    ASSERT_TRUE(model.rows[i].parameter != app::contract::ParamId::ChordOrder);
    ASSERT_TRUE(model.rows[i].parameter != app::contract::ParamId::ChordRetrigger);
  }
}

// Channel is a bare 1-16 number (the row label already says "channel");
// this also guards draw_value()'s double-size centering, which used to
// draw at a fixed x=32 and could run values past the 128px display.
static void channel_edit_screen_shows_the_full_number() {
  UiContext c;
  c.edit(app::contract::ParamId::ChordChannel, 15);  // 0 -> 15 (displayed "16").
  c.render();
  ASSERT_TRUE(line_contains("16"));
  ASSERT_FALSE(line_contains("Channel"));

  c.edit(app::contract::ParamId::LeadChannel, 9);  // 0 -> 9 (displayed "10").
  c.render();
  ASSERT_TRUE(line_contains("10"));
  ASSERT_FALSE(line_contains("Channel"));
}

static void quick_id_feedback_save_pending_and_input_release_are_observable() {
  UiContext c;
  app::prod::button_menu(c.input, c.ui);  // Play -> Select (`#`; encoder push now steps the PLAY cursor instead).
  app::prod::encoder_turn(c.input, c.ui, 12);
  app::prod::encoder_click(c.input, c.ui);
  app::prod::encoder_turn(c.input, c.ui, 1);  // BPM 120 -> 121.
  auto edit_model = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_EQ(edit_model.navigation.mode, app::contract::NavigationMode::Edit);
  ASSERT_EQ(edit_model.feedback.parameter, app::contract::ParamId::Bpm);
  ASSERT_EQ(edit_model.feedback.value, 121);
  ASSERT_TRUE(edit_model.settings_dirty);
  c.render();
  ASSERT_TRUE(line_contains("EDIT"));
  ASSERT_TRUE(line_contains("121 BPM"));

  app::prod::button_back(c.input, c.ui);
  app::prod::encoder_turn(c.input, c.ui, -10); // Explore Key without editing.
  app::prod::button_back(c.input, c.ui);
  const auto velocity_before = c.state.settings().velocity;
  app::prod::encoder_turn(c.input, c.ui, 1);
  // The PLAY cursor is independent of what SELECT/EDIT last touched: BPM and
  // Key stay put, velocity (play_cursor's default slot) is what moved.
  ASSERT_EQ(c.state.config().bpm, 121.0f);
  ASSERT_EQ(c.state.config().key, 0u);
  ASSERT_EQ(c.state.settings().velocity, velocity_before + 1);
  auto dirty_model = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_TRUE(dirty_model.settings_dirty);

  c.edit(app::contract::ParamId::UsbRole, 1);
  auto pending = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_TRUE(pending.restart_required);
  c.ui.navigation_mode = app::contract::NavigationMode::Play;

  // Config/ParamId is the authority; centered ADC must not release keypad joy.
  c.edit(app::contract::ParamId::KeypadAsJoy, 1);
  c.ui.navigation_mode = app::contract::NavigationMode::Play;
  app::prod::process_keypad_events(c.input, c.ui, {{0, true}});
  ASSERT_EQ(c.js.keypad_dir, 5);
  stub::joy_reset();
  app::process_joystick(c.input, c.joystick, drivers::JoystickAnalogConfig{});
  ASSERT_EQ(c.js.keypad_dir, 5);
  c.ui.navigation_mode = app::contract::NavigationMode::Select;
  app::prod::process_keypad_events(c.input, c.ui, {{0, false}});
  ASSERT_EQ(c.js.keypad_dir, -1);
}

static void play_screen_exposes_chord_key_octave_and_joystick_movement() {
  UiContext c;
  app::prod::process_keypad_events(c.input, c.ui, {{0, true}});  // Degree I.
  auto model = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_STREQ(model.active_chord_name, "C Major");
  ASSERT_STREQ(model.active_chord_degree, "I T0");  // tension appended
  ASSERT_STREQ(model.active_chord_notes, "C E G");
  ASSERT_STREQ(model.octave_name, "NORMAL");

  c.nav.dirty = false;
  c.nav.key_nav_mode = app::KeyNavMode::kFifths;
  app::prod::process_keypad_events(c.input, c.ui, {{3, true}});  // A: key up a fifth.
  model = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_STREQ(model.config.key_name, "G");
  ASSERT_STREQ(model.active_chord_name, "G Major");
  ASSERT_TRUE(c.nav.dirty);
  ASSERT_TRUE(model.settings_dirty);

  c.nav.dirty = false;
  app::prod::process_keypad_events(c.input, c.ui, {{11, true}}); // C: octave up.
  model = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_STREQ(model.octave_name, "UP 1");
  ASSERT_TRUE(c.nav.dirty);

  ASSERT_STREQ(model.inversion_name, "AUTO");                    // voice leading picks it
  app::prod::process_keypad_events(c.input, c.ui, {{10, true}}); // 9: AUTO -> manual root position.
  model = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_STREQ(model.inversion_name, "ROOT");
  ASSERT_STREQ(model.inversion_code, "R");
  app::prod::process_keypad_events(c.input, c.ui, {{10, true}}); // 9: first inversion.
  model = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_STREQ(model.inversion_name, "1ST");
  ASSERT_STREQ(model.active_chord_notes, "B D G");
  app::prod::process_keypad_events(c.input, c.ui, {{9, true}});  // 8: back to root.
  model = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_STREQ(model.inversion_name, "ROOT");

  stub::joy_dir = 1;
  stub::joy_x = 1500;
  stub::joy_y = -1500;
  app::process_joystick(c.input, c.joystick, drivers::JoystickAnalogConfig{});
  model = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_STREQ(model.joystick_status, "UP-R");

  c.render();
  // Octave and inversion share the compact register readout next to the
  // chord name ("O+1 I:R"), the same one the joystick screens end row 7 with.
  ASSERT_TRUE(line_contains("G B D"));
  ASSERT_TRUE(line_contains("G Major"));
  ASSERT_TRUE(line_contains("O+1 I:R"));
}

int main() {
  TEST_SUITE("Production UI end to end and golden screens");
  RUN_TEST(navigation_filters_and_renderer_follow_selection);
  RUN_TEST(paramid_edits_reach_config_settings_and_engine);
  RUN_TEST(scale_param_reaches_every_genre_mode);
  RUN_TEST(recipe_overrides_keypad_and_home_are_real);
  RUN_TEST(channel_edit_screen_shows_the_full_number);
  RUN_TEST(quick_id_feedback_save_pending_and_input_release_are_observable);
  RUN_TEST(play_screen_exposes_chord_key_octave_and_joystick_movement);
  RUN_TEST(play_screen_matches_golden);
  RUN_TEST(play_screen_footer_follows_the_cursor);
  RUN_TEST(play_screen_shows_an_armed_colour);
  RUN_TEST(play_screen_row0_survives_the_longest_scale_name);
  RUN_TEST(select_list_screen_matches_golden);
  RUN_TEST(edit_value_screen_matches_golden);
  RUN_TEST(notes_clock_screen_matches_golden);
  RUN_TEST(notes_walk_screen_matches_golden);
  RUN_TEST(cont_screen_matches_golden);
  RUN_TEST(chord_colors_screen_matches_golden);
  RUN_TEST(chord_voicings_screen_names_the_voicing);
  RUN_TEST(chord_discover_screen_shows_the_ranked_palette);
  RUN_TEST(key_wheel_shows_modulation_hint_for_a_foreign_chord);
  RUN_TEST(key_wheel_falls_back_without_a_sounding_chord);
  RUN_TEST(joystick_edit_menu_lists_the_active_familys_rows);
  test_print_result();
  return g_fail ? 1 : 0;
}
