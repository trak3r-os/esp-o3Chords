// Renders the production OLED screens through the real renderer and prints
// them to the terminal, two pixel rows per text line (half-block glyphs).
// A development aid, not a test: `./screen_dump` lists the screens,
// `./screen_dump all` or `./screen_dump <name>...` prints them; add `--calls`
// to list the draw calls behind each screen instead of its pixels.
// `./screen_dump --voicing` prints how chords connect per voice-leading mode.
#include "app/config_codec.h"
#include "app/input_joystick.h"
#include "app/production_ui.h"
#include "app/progression_modes.h"
#include "app/settings_bridge.h"
#include "golden.h"
#include "joy_stub.h"
#include "ui/production_render.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {
using P = app::contract::ParamId;
using Nav = app::contract::NavigationMode;

struct Rig {
  app::AppState state; engine::Engine engine; midi::Router router;
  midi::BleMidi ble{router}; midi::UsbDeviceMidi usb{router}; app::JoystickState js;
  app::UiNavState nav; std::vector<engine::ProgressionStep> steps;
  app::InputContext input{state, engine, router, ble, usb, js, nav, steps, 0};
  app::prod::UiState ui; drivers::OledSsd13xx oled;

  Rig() {
    state.sanitize(); engine.begin(); engine.set_router(&router);
    app::apply_settings(state, engine, ble, usb); app::prod::begin(ui, input);
    app::apply_progression_mode(engine, app::ProgressionMode::kMajor, state.settings());
  }
  void key(uint8_t index, bool down = true) { app::prod::process_keypad_events(input, ui, {{index, down}}); }
  void tap(uint8_t index) { key(index, true); key(index, false); }
  void turn(int8_t d) { app::prod::encoder_turn(input, ui, d); }
  void click() { app::prod::encoder_click(input, ui); }
  void set(P id, int8_t delta) {
    const auto mode = ui.navigation_mode; const auto sel = ui.selected_parameter;
    ui.navigation_mode = Nav::Edit; ui.selected_parameter = id; turn(delta);
    ui.navigation_mode = mode; ui.selected_parameter = sel;
  }
  void preset(app::PresetId id) {
    while (state.config().active_preset != id) set(P::JoystickRecipe, 1);
    ui.joystick_mode = true;
  }
};

void print(drivers::OledSsd13xx& oled) {
  const uint8_t* b = oled.framebuffer();
  const int w = oled.framebuffer_width(), h = oled.framebuffer_height();
  auto px = [&](int x, int y) { return (b[(y >> 3) * w + x] >> (y & 7)) & 1; };
  std::string out = "+" + std::string(w, '-') + "+\n";
  for (int y = 0; y < h; y += 2) {
    out += '|';
    for (int x = 0; x < w; ++x) {
      const int top = px(x, y), bottom = px(x, y + 1);
      out += top && bottom ? "█" : top ? "▀" : bottom ? "▄" : " ";
    }
    out += "|\n";
  }
  out += "+" + std::string(w, '-') + "+\n";
  std::fputs(out.c_str(), stdout);
}

struct Screen { const char* name; std::function<void(Rig&)> setup; };

const Screen kScreens[] = {
    {"boot", nullptr},
    {"play_idle", [](Rig&) {}},
    {"play_held", [](Rig& r) { r.key(0); }},
    {"play_shot", [](Rig& r) { r.set(P::PlayTrigger, -1); r.tap(4); }},
    {"play_jazz", [](Rig& r) { r.set(P::Scale, 11); r.key(8); }},
    {"play_cursor_key", [](Rig& r) { for (int i = 0; i < 6; ++i) r.click(); r.key(1); }},
    {"select_chord", [](Rig& r) { r.tap(14); r.turn(3); }},
    {"select_joystick", [](Rig& r) { r.tap(13); r.tap(14); r.turn(2); }},
    {"select_system", [](Rig& r) { app::prod::button_system(r.input, r.ui); r.turn(4); }},
    {"select_system_end", [](Rig& r) { app::prod::button_system(r.input, r.ui); r.turn(40); }},
    {"edit_velocity", [](Rig& r) { r.tap(14); r.click(); r.turn(5); }},
    {"edit_scale", [](Rig& r) { r.tap(14); r.turn(3); r.click(); r.turn(8); }},
    {"edit_key", [](Rig& r) { r.key(4); r.tap(14); r.turn(2); r.click(); r.turn(2); }},
    {"edit_system", [](Rig& r) { app::prod::button_system(r.input, r.ui); r.turn(2); r.click(); }},
    {"confirm_reset", [](Rig& r) { app::prod::button_system(r.input, r.ui); r.turn(40); r.turn(-4); r.click(); }},
    {"info_0", [](Rig& r) { r.ui.info_open = true; }},
    {"info_1", [](Rig& r) { r.ui.info_open = true; r.ui.info_page = 1; }},
    {"calibration", [](Rig& r) { r.ui.calibration_open = true; }},
    {"joy_lead", [](Rig& r) { r.preset(app::PresetId::kLead); r.key(0); }},
    {"joy_bass", [](Rig& r) { r.preset(app::PresetId::kBass); r.js.physical_dir = 2; }},
    {"joy_drone", [](Rig& r) { r.preset(app::PresetId::kDrone); }},
    {"joy_colors", [](Rig& r) { r.preset(app::PresetId::kColors); r.key(0); r.js.physical_dir = 1; }},
    {"joy_discover", [](Rig& r) { r.preset(app::PresetId::kDiscover); r.key(0); }},
    {"joy_colors_armed", [](Rig& r) {
       r.preset(app::PresetId::kColors); r.key(0);
       app::dispatch_keypad_joy_direction(r.input, 1); app::dispatch_keypad_joy_direction(r.input, -1); }},
    {"play_armed", [](Rig& r) {
       r.preset(app::PresetId::kColors);
       app::dispatch_keypad_joy_direction(r.input, 1); app::dispatch_keypad_joy_direction(r.input, -1);
       r.ui.joystick_mode = false; r.key(1); }},
    {"joy_voicings", [](Rig& r) { r.preset(app::PresetId::kVoicings); r.js.physical_dir = 4; }},
    {"joy_bend", [](Rig& r) { r.preset(app::PresetId::kBend); r.js.raw_x = 300; r.js.raw_y = -120; }},
    {"joy_macro", [](Rig& r) { r.preset(app::PresetId::kMacro); r.js.raw_x = -200; r.js.raw_y = 350; }},
};

// --calls prints the primitives each screen issued instead of its pixels:
// one line per call, text trimmed of the padding pair() adds.
void show(const Screen& s, bool calls) {
  std::printf("== %s\n", s.name);
  Rig r;
  std::vector<std::string> log;
  if (s.setup) s.setup(r);
  stub::oled_trace = calls ? &log : nullptr;
  if (s.setup) ui::render_production_screen(r.oled, {r.input, r.ui});
  else ui::render_boot_screen(r.oled);
  stub::oled_trace = nullptr;
  if (!calls) { print(r.oled); return; }
  for (const auto& line : log) std::printf("  %s\n", line.c_str());
}
}  // namespace

// --voicing: plays a few progressions through the real engine and prints the
// notes each chord sounded, per voice-leading mode - how the chords connect,
// which is not something a screen shows.
struct NoteTap final : midi::TransportSink {
  std::vector<uint8_t> on;
  void send_note_on(uint8_t, uint8_t note, uint8_t) override { on.push_back(note); }
  void send_note_off(uint8_t, uint8_t, uint8_t) override {}
  void send_pitch_bend(uint8_t, int16_t) override {}
  void send_control_change(uint8_t, uint8_t, uint8_t) override {}
};

void voicing_report() {
  static constexpr const char* kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  static constexpr uint8_t kKeyIndex[] = {0, 1, 2, 4, 5, 6, 8};  // degrees 1-7 on the keypad
  struct Run { const char* title; std::vector<int> degrees; };
  const Run runs[] = {
      {"I IV V I", {1, 4, 5, 1}},
      {"I vi IV V", {1, 6, 4, 5}},
      {"ii V I vi", {2, 5, 1, 6}},
      {"I ii iii IV V vi vii I", {1, 2, 3, 4, 5, 6, 7, 1}},
  };
  for (int mode = 0; mode < 3; ++mode) {
    std::printf("== voice leading %s\n", mode == 0 ? "Off" : mode == 1 ? "Smooth" : "Common");
    for (const auto& run : runs) {
      Rig r;
      NoteTap tap;
      r.router.register_sink(&tap);
      while (r.state.config().voice_leading != mode) r.set(P::VoiceLeading, 1);
      r.set(P::Humanize, -100);  // no timing offsets: every note lands in this pass
      std::printf("  %-24s", run.title);
      for (int degree : run.degrees) {
        tap.on.clear();
        r.key(kKeyIndex[degree - 1], true);
        r.engine.loop();
        std::sort(tap.on.begin(), tap.on.end());
        std::string chord;
        for (uint8_t note : tap.on) chord += std::string(kNames[note % 12]) + std::to_string(note / 12 - 1) + " ";
        std::printf("| %-13s", chord.c_str());
        r.key(kKeyIndex[degree - 1], false);
        r.engine.loop();
      }
      std::printf("\n");
    }
  }
}

int main(int argc, char** argv) {
  if (argc >= 2 && !std::strcmp(argv[1], "--voicing")) { voicing_report(); return 0; }
  if (argc < 2) {
    for (const auto& s : kScreens) std::printf("%s\n", s.name);
    return 0;
  }
  bool calls = false;
  for (int i = 1; i < argc; ++i) calls |= !std::strcmp(argv[i], "--calls");
  for (const auto& s : kScreens)
    for (int i = 1; i < argc; ++i)
      if (!std::strcmp(argv[i], "all") || !std::strcmp(argv[i], s.name)) { show(s, calls); break; }
  return 0;
}
