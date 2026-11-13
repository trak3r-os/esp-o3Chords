// Tests: analog joystick family logic — behaviour that needs X/Y injection
// and must be exercised via process_joystick() rather than
// dispatch_keypad_joy_direction().
//
// Families covered:
//   CHORD/Colors   — 8 discrete directions ARM one of 8 qualities for the
//                    next chord key; the same direction again disarms
//   CHORD/Discover — as Colors, ranked by consonance
//   CHORD/Voicings — a direction writes the Voicing parameter (Config)
//   CONT/Macro     — each axis streams its own target (CC / aftertouch /
//                    bend) from the normalised stick position
// No CHORD direction sounds or retriggers anything, and no preset belongs to
// the PARAM family any more.
//
// stub::joy_x/joy_y are raw centred ADC units (about +/-2000 at the stops);
// process_joystick() normalises them to +/-512 (kRail ADC units = full scale).

#include "../framework.h"
#include "../stubs/joy_stub.h"

#include "app/production_ui.h"
#include "app/config_codec.h"
#include "app/harmony_transform.h"
#include "app/input_context.h"
#include "app/input_joystick.h"
#include "app/parameters.h"
#include "app/settings_bridge.h"
#include "app/progression_modes.h"
#include "app/state.h"
#include "app/joystick_state.h"
#include "app/ui_state.h"
#include "drivers/joystick_analog.h"
#include "engine/engine.h"
#include "midi/router.h"
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace app;
using namespace app::prod;

// ADC units at the end stop: normalises to exactly +/-512.
static constexpr int16_t kRail = 1850;

// ---------------------------------------------------------------------------
// Capture sink

struct CapSink : midi::TransportSink {
    struct Msg { std::string t; uint8_t ch, a, b; };
    std::vector<Msg> msgs;
    void send_note_on (uint8_t ch, uint8_t n, uint8_t v) override
        { msgs.push_back({"on",  ch, n, v}); }
    void send_note_off(uint8_t ch, uint8_t n, uint8_t v) override
        { msgs.push_back({"off", ch, n, v}); }
    void send_pitch_bend(uint8_t ch, int16_t v) override
        { msgs.push_back({"pb",  ch, (uint8_t)(v >> 8), (uint8_t)v}); }
    void send_control_change(uint8_t ch, uint8_t cc, uint8_t v) override
        { msgs.push_back({"cc",  ch, cc, v}); }
    void send_channel_pressure(uint8_t ch, uint8_t v) override
        { msgs.push_back({"at",  ch, v, 0}); }

    int  count(const char* type) const { int c=0; for (auto& m:msgs) if(m.t==type) ++c; return c; }
    int  count_on()  const { return count("on"); }
    bool has_cc(uint8_t cc_num) const
        { for (auto& m:msgs) if(m.t=="cc" && m.a==cc_num) return true; return false; }
    bool has_cc_on_ch(uint8_t cc_num, uint8_t ch) const
        { for (auto& m:msgs) if(m.t=="cc" && m.a==cc_num && m.ch==ch) return true; return false; }
    int  count_cc() const { return count("cc"); }
    // Last value sent for a CC number, or -1 if none.
    int  last_cc(uint8_t cc_num) const
        { int v=-1; for (auto& m:msgs) if(m.t=="cc" && m.a==cc_num) v=m.b; return v; }
    // Last channel-pressure value, or -1 if none.
    int  last_at() const { int v=-1; for (auto& m:msgs) if(m.t=="at") v=m.a; return v; }
    int16_t last_pb() const {
        for (int i = (int)msgs.size()-1; i >= 0; --i)
            if (msgs[i].t == "pb")
                return (int16_t)((uint16_t)((uint16_t)msgs[i].a << 8) | msgs[i].b);
        return 0;
    }
    // Pitch classes of every note-on, as a 12-bit mask.
    uint16_t on_pitch_classes() const
        { uint16_t m=0; for (auto& x:msgs) if(x.t=="on") m |= static_cast<uint16_t>(1u << (x.a % 12)); return m; }
    void clear() { msgs.clear(); }
};

// ---------------------------------------------------------------------------
// Test context with injectable analog joystick

struct Ctx {
    AppState         app_state;
    engine::Engine   engine;
    midi::Router     router;
    midi::BleMidi    ble{router};
    midi::UsbDeviceMidi usb{router};
    JoystickState    js;
    UiNavState       nav;
    std::vector<engine::ProgressionStep> play_prog;
    InputContext     ctx{app_state, engine, router, ble, usb,
                         js, nav, play_prog, 0};
    UiState          ui;
    CapSink          sink;

    drivers::JoystickAnalog    joystick;
    drivers::JoystickAnalogConfig joy_cfg;

    Ctx() {
        app_state.sanitize();
        engine.begin();
        router.register_sink(&sink);
        engine.set_router(&router);
        app_state.settings().voice_leading    = 0;
        app_state.settings().humanize_ms      = 0;
        app_state.settings().midi_channel      = 0;
        app_state.settings().midi_channel_joy  = 3;
        // Config is the authority: every committed edit projects it back onto
        // Settings, so the fixture's choices must live in Config too.
        app_state.set_config(app::config::from_settings(app_state.settings()));
        apply_settings(app_state, engine, ble, usb);
        apply_progression_mode(engine, ProgressionMode::kMajor, app_state.settings());
        prod::begin(ui, ctx);
        js.last_dir = -1;
        stub::joy_reset();
    }

    Settings& s() { return app_state.settings(); }
    const contract::Config& cfg() const { return app_state.config(); }

    void set_mode(JoystickPlayMode m) { apply_joystick_mode(ctx, m); }

    // Edits Config the way the encoder does, through the one commit path.
    template <class F> bool edit(F&& change) {
        auto config = app_state.config();
        change(config);
        return commit_config(ctx, config);
    }

    // Recalls a factory preset exactly as rotating the Recipe slot does.
    bool select(PresetId id) {
        return edit([id](contract::Config& k) {
            k.active_preset = id;
            app::config::recall_preset(k.joy_families, id);
        });
    }

    // Inject X/Y (ADC units) and a direction, then run process_joystick.
    // A direction only acts on a change (dc): the initial -1 differs from
    // every 0..7, and flick() returns to centre afterwards.
    void joy(int16_t x, int16_t y, int8_t dir = -1) {
        stub::joy_x   = x;
        stub::joy_y   = y;
        stub::joy_dir = dir;
        process_joystick(ctx, joystick, joy_cfg);
    }
    void flick(int8_t dir) { joy(0, 0, dir); joy(0, 0, -1); }

    void key(uint8_t index, bool down) { process_keypad_events(ctx, ui, {{index, down}}); }
    const char* armed() { return engine.modifier().quality_name(); }
};

// ===========================================================================
// Suite 1 — CHORD/Colors: 8 discrete directions, each a different quality
// from the CLSC palette. A direction ARMS that quality in the chord modifier;
// nothing sounds until the next chord key, which applies it. dc (direction
// change) gates the update.
// ===========================================================================

static void test_quality_center_no_change() {
    Ctx c; c.set_mode(JoystickPlayMode::kQuality);
    c.nav.dirty = false;
    c.joy(0, 0, -1);  // center: handle_chord returns immediately on dir<0
    ASSERT_FALSE(c.nav.dirty);
    ASSERT_STREQ(c.armed(), "");
}

static void test_quality_direction_sets_browse_index_and_arms_quality() {
    Ctx c; c.set_mode(JoystickPlayMode::kQuality);
    c.joy(0, 0, 2);  // N -> kClockMap[2] = 0 -> CLSC[0]
    ASSERT_EQ(c.js.browse_index, 2);
    ASSERT_TRUE(c.nav.dirty);
    ASSERT_STREQ(c.armed(), "maj");
    c.joy(0, 0, -1);
    c.joy(0, 0, 0);  // E -> kClockMap[0] = 2 -> CLSC[2]
    ASSERT_STREQ(c.armed(), "maj7");
}

static void test_quality_all_8_directions_produce_distinct_qualities() {
    // The CLSC palette's 8 qualities are already distinct by construction;
    // this is CHORD's analogue of the voicing-styles distinctness test
    // (test_chord_building.cpp) — each of the 8 points must be a different
    // musical result.
    std::string seen[8];
    for (int8_t d = 0; d < 8; ++d) {
        Ctx c; c.set_mode(JoystickPlayMode::kQuality);
        c.joy(0, 0, d);
        seen[d] = c.armed();
        ASSERT_FALSE(seen[d].empty());
    }
    for (int i = 0; i < 8; ++i)
        for (int j = i + 1; j < 8; ++j)
            ASSERT_TRUE(seen[i] != seen[j]);
}

static void test_quality_same_direction_no_redundant_update() {
    Ctx c; c.set_mode(JoystickPlayMode::kQuality);
    c.joy(0, 0, 2);
    c.nav.dirty = false;
    c.joy(0, 0, 2);  // still held: dc=false -> no-op, and it stays armed
    ASSERT_FALSE(c.nav.dirty);
    ASSERT_STREQ(c.armed(), "maj");
}

static void test_colors_direction_is_silent_even_over_a_held_chord() {
    Ctx c; ASSERT_TRUE(c.select(PresetId::kColors));
    c.key(0, true);  // hold degree I
    ASSERT_GE(c.sink.count_on(), 3);
    c.sink.clear();
    // Sweep the whole rim without passing through centre.
    for (int8_t d = 0; d < 8; ++d) c.joy(0, 0, d);
    ASSERT_EQ(c.sink.count("on"), 0);
    ASSERT_EQ(c.sink.count("off"), 0);
    ASSERT_TRUE(c.engine.held_active());
    ASSERT_STREQ(c.armed(), "sus4");  // SE, the last direction pushed
}

// Arming is silent all the way through the gesture, including the stick's
// return to centre: a chord held on the keypad keeps sounding until its key
// is released.
static void test_chord_flick_and_recentre_leaves_a_held_chord_sounding() {
    for (PresetId id : {PresetId::kColors, PresetId::kDiscover, PresetId::kVoicings}) {
        Ctx c; ASSERT_TRUE(c.select(id));
        c.key(0, true);  // hold degree I
        ASSERT_TRUE(c.engine.held_active());
        c.sink.clear();
        c.flick(0);
        ASSERT_EQ(c.sink.count("on"), 0);
        ASSERT_EQ(c.sink.count("off"), 0);
        ASSERT_TRUE(c.engine.held_active());
    }
}

static void test_colors_arm_survives_recentre_and_next_chord_key_applies_it() {
    Ctx c; ASSERT_TRUE(c.select(PresetId::kColors));
    c.flick(0);  // E -> maj7
    ASSERT_STREQ(c.armed(), "maj7");
    c.sink.clear();
    c.key(1, true);  // degree II in C: D minor by the scale, Dmaj7 when armed
    const uint16_t pcs = c.sink.on_pitch_classes();
    ASSERT_TRUE(pcs & (1u << 2));   // D
    ASSERT_TRUE(pcs & (1u << 6));   // F#
    ASSERT_TRUE(pcs & (1u << 9));   // A
    ASSERT_TRUE(pcs & (1u << 1));   // C#
    ASSERT_FALSE(pcs & (1u << 5));  // no F natural
    // Still armed for the chord after that.
    ASSERT_STREQ(c.armed(), "maj7");
}

static void test_colors_same_direction_again_disarms() {
    Ctx c; ASSERT_TRUE(c.select(PresetId::kColors));
    c.flick(0);
    ASSERT_STREQ(c.armed(), "maj7");
    c.flick(0);
    ASSERT_STREQ(c.armed(), "");
    ASSERT_FALSE(c.engine.modifier().has_modifications());
    // The chords are the scale's own again.
    c.sink.clear();
    c.key(1, true);
    const uint16_t pcs = c.sink.on_pitch_classes();
    ASSERT_TRUE(pcs & (1u << 5));   // F natural: D minor
    ASSERT_FALSE(pcs & (1u << 6));
    ASSERT_FALSE(pcs & (1u << 1));
    // A third flick arms it again.
    c.flick(0);
    ASSERT_STREQ(c.armed(), "maj7");
}

static void test_colors_other_direction_rearms_instead_of_disarming() {
    Ctx c; ASSERT_TRUE(c.select(PresetId::kColors));
    c.flick(0);
    c.flick(6);  // S -> kClockMap[6] = 4 -> "min"
    ASSERT_STREQ(c.armed(), "min");
}

static void test_colors_preset_change_disarms() {
    // Even to another CHORD preset: an armed colour never outlives the preset.
    for (PresetId next : {PresetId::kDiscover, PresetId::kVoicings, PresetId::kLead, PresetId::kMacro}) {
        Ctx c; ASSERT_TRUE(c.select(PresetId::kColors));
        c.flick(0);
        ASSERT_STREQ(c.armed(), "maj7");
        ASSERT_TRUE(c.select(next));
        ASSERT_STREQ(c.armed(), "");
    }
    // And through the real Recipe slot on the joystick screen.
    Ctx c; ASSERT_TRUE(c.select(PresetId::kColors));
    c.flick(0);
    c.ui.joystick_mode = true;
    encoder_turn(c.ctx, c.ui, 1);
    ASSERT_TRUE(c.cfg().active_preset == PresetId::kDiscover);
    ASSERT_STREQ(c.armed(), "");
}

static void test_colors_palette_parameter_changes_what_a_direction_arms() {
    Ctx c; ASSERT_TRUE(c.select(PresetId::kColors));
    ASSERT_TRUE(c.edit([](contract::Config& k) { k.joy_families.chord.palette = 2; }));  // EXT9
    c.flick(2);  // N -> EXT9[0]
    ASSERT_STREQ(c.armed(), "add9");
}

// ===========================================================================
// Suite 2 — CHORD/Discover: 8 directions, ranked by consonance/tension/
// distance (Order=ranking) rather than a fixed palette order.
// ===========================================================================

static void test_discover_direction_sets_browse_index() {
    Ctx c; c.set_mode(JoystickPlayMode::kDiscover);
    c.joy(0, 0, 2);
    ASSERT_EQ(c.js.browse_index, 2);
    ASSERT_TRUE(c.nav.dirty);
}

static void test_discover_center_no_change() {
    Ctx c; c.set_mode(JoystickPlayMode::kDiscover);
    c.nav.dirty = false;
    c.joy(0, 0, -1);
    ASSERT_FALSE(c.nav.dirty);
}

static void test_discover_direction_arms_its_ranked_quality() {
    for (int8_t d = 0; d < 8; ++d) {
        Ctx c; ASSERT_TRUE(c.select(PresetId::kDiscover));
        c.nav.active_degree = 1;
        const char* ranked = c.js.discover_slots[kDiscoverMap[d]];
        ASSERT_TRUE(ranked != nullptr);
        c.joy(0, 0, d);
        ASSERT_STREQ(c.armed(), ranked);
    }
}

static void test_discover_same_direction_again_disarms_and_is_silent() {
    Ctx c; ASSERT_TRUE(c.select(PresetId::kDiscover));
    c.sink.clear();
    c.flick(2);
    ASSERT_TRUE(c.armed()[0] != '\0');
    c.flick(2);
    ASSERT_STREQ(c.armed(), "");
    ASSERT_TRUE(c.sink.msgs.empty());
}

// ===========================================================================
// Suite 3 — CONT/Macro: X/Y -> configurable CC on the joy channel. Default
// x_cc=1 (mod wheel), y_cc=11 (expression). The deadzone is ContParams::
// deadzone (percent), the remaining travel is rescaled to the full range,
// and up/right increase.
// ===========================================================================

static void test_cccontrol_x_positive_sends_cc() {
    Ctx c; c.set_mode(JoystickPlayMode::kCCControl);
    c.joy(1000, 0);
    ASSERT_TRUE(c.sink.has_cc(1));   // CC 1 (mod wheel)
    ASSERT_GT(c.sink.last_cc(1), 63);
}

static void test_cccontrol_y_up_raises_the_value() {
    // Raw Y grows downward, so "up" is a negative ADC reading.
    Ctx c; c.set_mode(JoystickPlayMode::kCCControl);
    c.joy(0, -1000);
    ASSERT_GT(c.sink.last_cc(11), 63);  // CC 11 (expression)
    c.joy(0, -kRail);
    ASSERT_EQ(c.sink.last_cc(11), 127);
    c.joy(0, 1000);
    ASSERT_LT(c.sink.last_cc(11), 63);
    c.joy(0, kRail);
    ASSERT_EQ(c.sink.last_cc(11), 0);
}

static void test_cccontrol_sends_on_joy_channel() {
    Ctx c;
    ASSERT_TRUE(c.edit([](contract::Config& k) { k.lead_channel = 5; }));
    ASSERT_TRUE(c.select(PresetId::kMacro));
    c.sink.clear();
    c.joy(1000, -1000);
    ASSERT_TRUE(c.sink.has_cc_on_ch(1, 5));
    ASSERT_TRUE(c.sink.has_cc_on_ch(11, 5));
    for (auto& m : c.sink.msgs) if (m.t == "cc") ASSERT_EQ(m.ch, 5);
}

static void test_cccontrol_center_maps_to_midpoint() {
    Ctx c; c.set_mode(JoystickPlayMode::kCCControl);
    c.joy(0, 0);
    // cc_last starts at -1, so the first reading always sends.
    ASSERT_EQ(c.sink.last_cc(1), 63);
    ASSERT_EQ(c.sink.last_cc(11), 63);
}

static void test_cccontrol_min_x_maps_to_0() {
    Ctx c; c.set_mode(JoystickPlayMode::kCCControl);
    c.joy(-kRail, 0);
    ASSERT_EQ(c.sink.last_cc(1), 0);
}

static void test_cccontrol_max_x_maps_to_127() {
    Ctx c; c.set_mode(JoystickPlayMode::kCCControl);
    c.joy(kRail, 0);
    ASSERT_EQ(c.sink.last_cc(1), 127);
}

static void test_cccontrol_custom_cc_numbers() {
    // CC numbers are Config fields (ContParams::x_cc/y_cc).
    Ctx c; ASSERT_TRUE(c.select(PresetId::kMacro));
    ASSERT_TRUE(c.edit([](contract::Config& k) {
        k.joy_families.cont.x_cc = 74;
        k.joy_families.cont.y_cc = 71;
    }));
    c.sink.clear();
    c.joy(1000, 1000);
    ASSERT_TRUE(c.sink.has_cc(74));
    ASSERT_TRUE(c.sink.has_cc(71));
    ASSERT_FALSE(c.sink.has_cc(1));
    ASSERT_FALSE(c.sink.has_cc(11));
}

static void test_cccontrol_no_repeat_when_value_unchanged() {
    // After cc_last_x is set, same value must NOT resend.
    Ctx c; c.set_mode(JoystickPlayMode::kCCControl);
    c.joy(1000, 0);      // first call — sends CC
    c.sink.clear();
    c.joy(1000, 0);      // same value — should NOT resend
    ASSERT_EQ(c.sink.count_cc(), 0);
}

static void test_cont_deadzone_parameter_sets_the_neutral_zone() {
    // 800 ADC units is 43% of the travel.
    Ctx c; ASSERT_TRUE(c.select(PresetId::kMacro));
    ASSERT_EQ(c.cfg().joy_families.cont.deadzone, 10);
    c.joy(800, 0);
    const int at_10_percent = c.sink.last_cc(1);
    ASSERT_GT(at_10_percent, 63);

    ASSERT_TRUE(c.edit([](contract::Config& k) { k.joy_families.cont.deadzone = 50; }));
    c.joy(800, 0);
    ASSERT_EQ(c.sink.last_cc(1), 63);   // inside a 50% deadzone: neutral
    c.joy(kRail, 0);
    ASSERT_EQ(c.sink.last_cc(1), 127);  // and the rail is still full scale

    ASSERT_TRUE(c.edit([](contract::Config& k) { k.joy_families.cont.deadzone = 0; }));
    c.joy(800, 0);
    ASSERT_GT(c.sink.last_cc(1), at_10_percent);
}

static void test_cont_curve_shapes_the_cc_value() {
    int value[3] = {};
    for (uint8_t curve = 0; curve < 3; ++curve) {
        Ctx c; ASSERT_TRUE(c.select(PresetId::kMacro));
        ASSERT_TRUE(c.edit([curve](contract::Config& k) { k.joy_families.cont.curve = curve; }));
        c.joy(kRail / 2, 0);
        value[curve] = c.sink.last_cc(1);
    }
    ASSERT_LT(value[1], value[0]);  // Exp
    ASSERT_GT(value[2], value[0]);  // Log
}

static void test_cont_aftertouch_target_sends_channel_pressure() {
    Ctx c; ASSERT_TRUE(c.select(PresetId::kMacro));
    ASSERT_TRUE(c.edit([](contract::Config& k) { k.joy_families.cont.x_target = ContTarget::kAftertouch; }));
    c.sink.clear();
    c.joy(kRail, 0);
    ASSERT_EQ(c.sink.last_at(), 127);
    for (auto& m : c.sink.msgs) if (m.t == "at") ASSERT_EQ(m.ch, 3);  // joy channel
    ASSERT_FALSE(c.sink.has_cc(1));     // X no longer streams its CC
    ASSERT_TRUE(c.sink.has_cc(11));     // Y still follows its own target
    c.joy(-kRail, 0);
    ASSERT_EQ(c.sink.last_at(), 0);
    const int sent = c.sink.count("at");
    c.joy(-kRail, 0);                   // change-only
    ASSERT_EQ(c.sink.count("at"), sent);
}

static void test_cont_each_axis_follows_its_own_target() {
    // X = CC, Y = Bend: the CC goes out on the joy channel, the bend on the
    // chord channel, and neither waits for the other.
    Ctx c; ASSERT_TRUE(c.select(PresetId::kMacro));
    ASSERT_TRUE(c.edit([](contract::Config& k) {
        k.joy_families.cont.y_target = ContTarget::kBend;
        k.joy_families.cont.bend_snap = ContBendSnap::kOff;
    }));
    c.joy(0, 0);  // a preset change asks for neutral before bending
    c.sink.clear();
    c.joy(kRail, -kRail);
    ASSERT_EQ(c.sink.last_cc(1), 127);
    ASSERT_FALSE(c.sink.has_cc(11));
    ASSERT_EQ(c.sink.count("pb"), 1);
    ASSERT_EQ(c.sink.last_pb(), 8191);  // up bends up
    for (auto& m : c.sink.msgs) if (m.t == "pb") ASSERT_EQ(m.ch, 0);
    c.joy(kRail, kRail);
    ASSERT_EQ(c.sink.last_pb(), -8192);
}

static void test_cont_ignores_keypad_as_joy_directions() {
    // CONT reads a position; a keypad direction has none.
    for (PresetId id : {PresetId::kMacro, PresetId::kBend}) {
        Ctx c; ASSERT_TRUE(c.select(id));
        c.sink.clear();
        for (int8_t d = 0; d < 8; ++d) {
            dispatch_keypad_joy_direction(c.ctx, d);
            dispatch_keypad_joy_direction(c.ctx, -1);
        }
        ASSERT_EQ(c.sink.count("cc"), 0);
        ASSERT_EQ(c.sink.count("pb"), 0);
        ASSERT_EQ(c.sink.count("at"), 0);
        ASSERT_EQ(c.sink.count("on"), 0);
    }
}

// ===========================================================================
// Suite 4 — the old PARAM recipes (Rhythm, StrumFeel, ArpControl) have no
// preset left: Flip is disabled, so they land on CONT/Macro and a flick no
// longer edits strum feel or arp settings.
// ===========================================================================

static void test_legacy_param_recipes_land_on_macro_and_edit_nothing() {
    for (JoystickPlayMode mode : {JoystickPlayMode::kRhythm, JoystickPlayMode::kStrumFeel,
                                  JoystickPlayMode::kArpControl}) {
        Ctx c; c.set_mode(mode);
        ASSERT_TRUE(c.cfg().active_preset == PresetId::kMacro);
        const Settings before = c.s();
        for (int8_t d = 0; d < 8; ++d) { c.joy(kRail, 0, d); c.joy(0, 0, -1); }
        ASSERT_EQ(c.s().strum_ms, before.strum_ms);
        ASSERT_EQ(c.s().humanize_ms, before.humanize_ms);
        ASSERT_EQ(c.s().arp_octaves, before.arp_octaves);
        ASSERT_EQ(c.s().arp_rate, before.arp_rate);
        ASSERT_EQ(c.s().arp_pattern, before.arp_pattern);
        ASSERT_EQ(c.s().arp_gate_percent, before.arp_gate_percent);
        ASSERT_TRUE(c.sink.has_cc_on_ch(1, 3));  // it streams Macro's CCs instead
    }
}

// ===========================================================================
// Suite 5 — CHORD/Voicings: a direction is a second way to set the Voicing
// parameter. It is written to Config through commit_config(), so Config,
// Settings and the chord modifier agree; nothing is strummed or retriggered.
// ===========================================================================

static void test_voicing_direction_updates_config_settings_and_modifier() {
    for (int8_t d = 0; d < 8; ++d) {
        Ctx c; ASSERT_TRUE(c.select(PresetId::kVoicings));
        const uint8_t expected = harmony::voicing_for_direction(d);
        c.joy(0, 0, d);
        ASSERT_EQ(c.cfg().voicing_spread, expected);
        ASSERT_EQ(c.s().voicing_spread, expected);
        ASSERT_EQ(static_cast<uint8_t>(c.engine.modifier().voicing_spread()), expected);
        ASSERT_EQ(c.js.voicing_selection, expected);
    }
}

static void test_voicing_eight_directions_are_eight_voicings() {
    bool seen[8] = {};
    for (int8_t d = 0; d < 8; ++d) {
        Ctx c; ASSERT_TRUE(c.select(PresetId::kVoicings));
        c.joy(0, 0, d);
        ASSERT_LT(c.cfg().voicing_spread, 8);
        ASSERT_FALSE(seen[c.cfg().voicing_spread]);
        seen[c.cfg().voicing_spread] = true;
    }
}

static void test_voicing_direction_does_not_strum_or_retrigger() {
    Ctx c; ASSERT_TRUE(c.select(PresetId::kVoicings));
    c.key(0, true);  // a chord is held
    c.sink.clear();
    c.joy(kRail, 0, 0);
    ASSERT_EQ(c.cfg().voicing_spread, static_cast<uint8_t>(engine::VoicingSpread::kDrop2));
    c.joy(-kRail, 0, 4);
    ASSERT_EQ(c.cfg().voicing_spread, static_cast<uint8_t>(engine::VoicingSpread::kRootless));
    ASSERT_EQ(c.sink.count("on"), 0);
    ASSERT_EQ(c.sink.count("off"), 0);
    ASSERT_TRUE(c.engine.held_active());
}

static void test_voicing_direction_is_a_real_settings_edit() {
    Ctx c; ASSERT_TRUE(c.select(PresetId::kVoicings));
    c.nav.dirty = false; c.nav.settings_dirty = false;
    const uint32_t revision = c.nav.settings_revision;
    c.joy(0, 0, 6);  // S -> Open
    ASSERT_TRUE(c.nav.dirty);
    ASSERT_TRUE(c.nav.settings_dirty);
    ASSERT_EQ(c.nav.settings_revision, revision + 1);
    ASSERT_EQ(c.cfg().voicing_spread, static_cast<uint8_t>(engine::VoicingSpread::kOpen));
    // The value stays after the stick recentres and after an unrelated edit:
    // no later settings pass reverts it.
    c.joy(0, 0, -1);
    ASSERT_TRUE(c.edit([](contract::Config& k) { k.velocity = 90; }));
    ASSERT_EQ(c.cfg().voicing_spread, static_cast<uint8_t>(engine::VoicingSpread::kOpen));
    ASSERT_EQ(c.s().voicing_spread, static_cast<uint8_t>(engine::VoicingSpread::kOpen));
    ASSERT_TRUE(c.engine.modifier().voicing_spread() == engine::VoicingSpread::kOpen);
    // And the encoder edits the very same value.
    char label[24], value[24];
    ASSERT_TRUE(parameters::format(c.cfg(), contract::ParamId::Voicing, label, sizeof(label), value, sizeof(value)));
    ASSERT_STREQ(value, "Open");
}

static void test_voicing_does_not_arm_a_colour() {
    Ctx c; ASSERT_TRUE(c.select(PresetId::kVoicings));
    for (int8_t d = 0; d < 8; ++d) c.flick(d);
    ASSERT_STREQ(c.armed(), "");
}

// service_joystick_ticks() is a no-op hook now: it used to reset octave and
// inversion when the chord changed under the Voicings preset.
static void test_service_ticks_leave_octave_and_inversion_alone() {
    Ctx c; ASSERT_TRUE(c.select(PresetId::kVoicings));
    c.engine.modifier().adjust_octave(1);
    c.engine.modifier().set_manual_inversion(2);
    c.key(0, true); c.key(0, false);
    c.js.voicing_last_degree = 0;
    c.key(1, true);  // the chord changes
    c.sink.clear();
    for (int i = 0; i < 8; ++i) service_joystick_ticks(c.ctx, 24);
    ASSERT_EQ(c.engine.modifier().octave_shift(), 1);
    ASSERT_EQ(c.engine.modifier().inversion_override(), 2);
    ASSERT_TRUE(c.sink.msgs.empty());
}

// ===========================================================================
// main
// ===========================================================================

int main() {
    TEST_SUITE("CHORD/Colors — 8 directions arm 8 qualities");
    RUN_TEST(test_quality_center_no_change);
    RUN_TEST(test_quality_direction_sets_browse_index_and_arms_quality);
    RUN_TEST(test_quality_all_8_directions_produce_distinct_qualities);
    RUN_TEST(test_quality_same_direction_no_redundant_update);
    RUN_TEST(test_colors_direction_is_silent_even_over_a_held_chord);
    RUN_TEST(test_chord_flick_and_recentre_leaves_a_held_chord_sounding);
    RUN_TEST(test_colors_arm_survives_recentre_and_next_chord_key_applies_it);
    RUN_TEST(test_colors_same_direction_again_disarms);
    RUN_TEST(test_colors_other_direction_rearms_instead_of_disarming);
    RUN_TEST(test_colors_preset_change_disarms);
    RUN_TEST(test_colors_palette_parameter_changes_what_a_direction_arms);

    TEST_SUITE("CHORD/Discover — ranked directions");
    RUN_TEST(test_discover_direction_sets_browse_index);
    RUN_TEST(test_discover_center_no_change);
    RUN_TEST(test_discover_direction_arms_its_ranked_quality);
    RUN_TEST(test_discover_same_direction_again_disarms_and_is_silent);

    TEST_SUITE("CONT/Macro — each axis streams its own target");
    RUN_TEST(test_cccontrol_x_positive_sends_cc);
    RUN_TEST(test_cccontrol_y_up_raises_the_value);
    RUN_TEST(test_cccontrol_sends_on_joy_channel);
    RUN_TEST(test_cccontrol_center_maps_to_midpoint);
    RUN_TEST(test_cccontrol_min_x_maps_to_0);
    RUN_TEST(test_cccontrol_max_x_maps_to_127);
    RUN_TEST(test_cccontrol_custom_cc_numbers);
    RUN_TEST(test_cccontrol_no_repeat_when_value_unchanged);
    RUN_TEST(test_cont_deadzone_parameter_sets_the_neutral_zone);
    RUN_TEST(test_cont_curve_shapes_the_cc_value);
    RUN_TEST(test_cont_aftertouch_target_sends_channel_pressure);
    RUN_TEST(test_cont_each_axis_follows_its_own_target);
    RUN_TEST(test_cont_ignores_keypad_as_joy_directions);

    TEST_SUITE("Legacy PARAM recipes — no preset left");
    RUN_TEST(test_legacy_param_recipes_land_on_macro_and_edit_nothing);

    TEST_SUITE("CHORD/Voicings — a direction writes the Voicing parameter");
    RUN_TEST(test_voicing_direction_updates_config_settings_and_modifier);
    RUN_TEST(test_voicing_eight_directions_are_eight_voicings);
    RUN_TEST(test_voicing_direction_does_not_strum_or_retrigger);
    RUN_TEST(test_voicing_direction_is_a_real_settings_edit);
    RUN_TEST(test_voicing_does_not_arm_a_colour);
    RUN_TEST(test_service_ticks_leave_octave_and_inversion_alone);

    test_print_result();
    return (g_fail > 0) ? 1 : 0;
}
