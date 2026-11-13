// Tests: Joystick mode internal logic.
//
// Covers (by priority):
//   critical  kDrone bitshift safety
//   high      kArpControl param dispatch, kHarmony cleanup, kScaleExplore switching,
//               kEuclidean pattern selection, kInterval sub-modes, center-release
//               exhaustiveness, mode transition correctness
//   medium    kPentatonic walk, vibrato clamp formula, all 17 modes stable init

#include "../framework.h"

#include "app/production_ui.h"
#include "app/config_codec.h"
#include "app/input_context.h"
#include "app/input_joystick.h"
#include "app/settings_bridge.h"
#include "app/progression_modes.h"
#include "app/state.h"
#include "app/joystick_state.h"
#include "app/ui_state.h"
#include "engine/engine.h"
#include "midi/router.h"
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"
#include "../stubs/esp_timer.h"

#include <algorithm>
#include <cstring>
#include <vector>
#include <string>

using namespace app;
using namespace app::prod;

// ---------------------------------------------------------------------------
// Infrastructure
// ---------------------------------------------------------------------------

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

    int  count_on()  const { int c=0; for (auto& m:msgs) if(m.t=="on")  ++c; return c; }
    int  count_off() const { int c=0; for (auto& m:msgs) if(m.t=="off") ++c; return c; }
    bool has_on_ch(uint8_t ch) const
        { for (auto& m:msgs) if(m.t=="on"  && m.ch==ch) return true; return false; }
    bool has_off_note_ch(uint8_t n, uint8_t ch) const
        { for (auto& m:msgs) if(m.t=="off" && m.a==n && m.ch==ch) return true; return false; }
    void clear() { msgs.clear(); }
};

struct Ctx {
    AppState         app_state;
    engine::Engine   engine;
    midi::Router     router;
    midi::BleMidi    ble{router};
    midi::UsbDeviceMidi usb{router};
    JoystickState    js;
    UiNavState       nav;
    std::vector<engine::ProgressionStep> play_prog;
    struct LedgerSink : engine::NoteLedgerSink {
        midi::Router& r; explicit LedgerSink(midi::Router& x) : r(x) {}
        void note_on(uint8_t route, uint8_t ch, uint8_t n, uint8_t v) override { r.route_note_on(static_cast<uint8_t>(1u << route), ch, n, v); }
        void note_off(uint8_t route, uint8_t ch, uint8_t n) override { r.route_note_off(static_cast<uint8_t>(1u << route), ch, n, 0); }
    };
    LedgerSink ledger_sink{router};
    engine::NoteLedger ledger{&ledger_sink};
    InputContext     ctx{app_state, engine, router, ble, usb,
                         js, nav, play_prog, 0, &ledger};
    UiState          ui;
    CapSink          sink;

    Ctx() {
        app_state.sanitize();
        engine.begin();
        router.register_sink(&sink);
        engine.set_router(&router);
        app_state.settings().voice_leading    = 0;
        app_state.settings().humanize_ms      = 0;
        app_state.settings().midi_channel      = 0;
        app_state.settings().midi_channel_joy  = 3;
        // Config is the authority: every committed edit (walk left/right, a
        // Voicings direction) projects it back onto Settings, so the
        // fixture's choices must live in Config too.
        app_state.set_config(app::config::from_settings(app_state.settings()));
        apply_settings(app_state, engine, ble, usb);
        apply_progression_mode(engine, ProgressionMode::kMajor, app_state.settings());
        prod::begin(ui, ctx);
        // Start with last_dir=-1 so first dispatch always has dc=true
        js.last_dir = -1;
    }

    Settings& s() { return app_state.settings(); }

    // Dispatch a direction; forces dc=true by resetting keypad_dir first —
    // that's the field dispatch_keypad_joy_direction()'s dc actually reads
    // (pre-existing latent bug: this used to reset last_dir, which the
    // keypad path never consults, so two identical directions in a row with
    // no intervening center release silently no-op'd instead of toggling).
    void joy(int8_t dir) {
        js.keypad_dir = static_cast<int8_t>(dir - 1);  // ensure != dir
        dispatch_keypad_joy_direction(ctx, dir);
    }

    void set_mode(JoystickPlayMode m) { apply_joystick_mode(ctx, m); }

    // Edits Config the way the encoder does, through the one commit path.
    template <class F> bool edit(F&& change) {
        auto config = app_state.config();
        change(config);
        return commit_config(ctx, config);
    }

    // The old Harmony preset no longer exists as a PresetId: it is Drone plus
    // the legacy override (mono latched single notes on the lead channel),
    // over Source=intervals - the row config migration builds for it.
    void set_harmony() {
        app::NotesParams p = app::preset_defaults(app::PresetId::kDrone).notes;
        p.source = app::NoteSource::kIntervals;
        app::apply_legacy_harmony_override(p);
        set_notes(p);
    }
    void fire() { esp_timer_fire_pending(); }

    // Builds an exact row of the config migration table (wire v2 -> v3)
    // for cases where the nearest-preset mapping
    // apply_joystick_mode() uses for old firmware upgrades isn't a faithful
    // stand-in for the specific recipe under test.
    void set_notes(const app::NotesParams& p) {
        auto config = app_state.config();
        config.active_preset = app::PresetId::kLead;  // any NOTES preset; only the family matters
        config.joy_families.notes = p;
        app_state.set_config(config);
        app::reset_joystick_gesture_state(ctx);
    }
};

// ===========================================================================
// Suite 1 — kArpControl: the PARAM-family live arp tweaker has no preset any
// more (Flip, the last PARAM preset, is disabled). The legacy mode now lands
// on CONT/Macro, where keypad directions do nothing at all.
// ===========================================================================

static void test_arpcontrol_lands_on_macro_and_directions_edit_nothing() {
    Ctx c; c.set_mode(JoystickPlayMode::kArpControl);
    ASSERT_TRUE(c.app_state.config().active_preset == app::PresetId::kMacro);
    ASSERT_TRUE(kPresets[static_cast<size_t>(c.app_state.config().active_preset)].family == JoyFamily::kCont);
    const Settings before = c.s();
    c.sink.clear();
    for (int8_t d = 0; d < 8; ++d) { c.joy(d); c.joy(-1); }
    ASSERT_EQ(c.s().arp_pattern, before.arp_pattern);
    ASSERT_EQ(c.s().arp_rate, before.arp_rate);
    ASSERT_EQ(c.s().arp_octaves, before.arp_octaves);
    ASSERT_EQ(c.s().arp_gate_percent, before.arp_gate_percent);
    ASSERT_TRUE(c.sink.msgs.empty());  // CONT reads a position; a keypad direction has none
}

// ===========================================================================
// Suite 2 — kDrone: toggle, mask, channel, bitshift safety
// ===========================================================================

static void test_drone_first_press_note_on() {
    Ctx c; c.set_mode(JoystickPlayMode::kDrone);
    c.joy(0);
    ASSERT_GT(c.sink.count_on(), 0);
}

static void test_drone_first_press_sets_mask() {
    Ctx c; c.set_mode(JoystickPlayMode::kDrone);
    c.joy(0);
    ASSERT_TRUE(c.js.drone_active_mask & (1u << 0));
}

static void test_drone_second_press_note_off() {
    Ctx c; c.set_mode(JoystickPlayMode::kDrone);
    c.joy(0);
    c.sink.clear();
    c.joy(0);
    ASSERT_GT(c.sink.count_off(), 0);
}

static void test_drone_second_press_clears_mask() {
    Ctx c; c.set_mode(JoystickPlayMode::kDrone);
    c.joy(0);
    c.joy(0);
    ASSERT_FALSE(c.js.drone_active_mask & (1u << 0));
}

static void test_drone_uses_chord_channel() {
    // NotesParams::channel defaults to kChord, and the Drone preset doesn't
    // override it — Drone now blends into the chord channel by default,
    // rather than always using the separate joy channel like the old
    // hardwired Drone recipe did (Channel is a real per-preset NOTES
    // parameter).
    Ctx c;
    c.s().midi_channel = 6;
    c.set_mode(JoystickPlayMode::kDrone);
    c.joy(0);
    ASSERT_TRUE(c.sink.has_on_ch(6));
}

static void test_drone_all_8_dirs_safe() {
    // Critical: verifies 1u<<dir is UB-free for dir 0-7
    for (int8_t d = 0; d < 8; ++d) {
        Ctx c; c.set_mode(JoystickPlayMode::kDrone);
        c.joy(d);  // must not crash
    }
}

static void test_drone_dirs_set_independent_mask_bits() {
    Ctx c; c.set_mode(JoystickPlayMode::kDrone);
    c.joy(0);
    c.joy(3);
    ASSERT_TRUE(c.js.drone_active_mask & (1u << 0));
    ASSERT_TRUE(c.js.drone_active_mask & (1u << 3));
    ASSERT_FALSE(c.js.drone_active_mask & (1u << 1));
}

static void test_drone_center_is_noop_does_not_note_off() {
    // kDrone excluded from both cleanup paths; center release does nothing
    Ctx c; c.set_mode(JoystickPlayMode::kDrone);
    c.joy(0);
    c.sink.clear();
    c.joy(-1);  // center
    ASSERT_EQ(c.sink.count_off(), 0);
    ASSERT_TRUE(c.js.drone_active_mask & (1u << 0));  // still droning
}

static void test_drone_mode_transition_kills_active_drones() {
    Ctx c;
    c.s().midi_channel_joy = 5;
    c.set_mode(JoystickPlayMode::kDrone);
    c.joy(0);
    ASSERT_NE(c.js.drone_active_mask, 0);
    c.sink.clear();
    c.set_mode(JoystickPlayMode::kQuality);
    ASSERT_EQ(c.js.drone_active_mask, 0);
    ASSERT_GT(c.sink.count_off(), 0);  // note_off sent on mode switch
}

static void test_drone_toggled_off_correct_note_sent() {
    Ctx c;
    c.s().midi_channel = 6;
    c.set_mode(JoystickPlayMode::kDrone);
    c.joy(0);
    uint8_t drone_note = c.js.drone_notes[0];
    c.sink.clear();
    c.joy(0);  // toggle off
    ASSERT_TRUE(c.sink.has_off_note_ch(drone_note, 6));
}

// ===========================================================================
// Suite 3 — kHarmony: Harmony is now NOTES/source=intervals with Sustain=latch
// (config migration table), so it shares the per-direction latch
// array (drone_active_mask/drone_notes) with old Drone rather than a
// dedicated harmony_note field. Mono=on means only one direction can be
// latched at a time. Center release no longer stops it — the latch
// release rule is "same direction again, panic, or a preset/family
// change", and center release isn't on that list.
// ===========================================================================

static void test_harmony_dir_sends_note_on_joy_ch() {
    Ctx c;
    c.s().midi_channel_joy = 4;
    c.set_harmony();
    c.joy(2);  // N direction
    ASSERT_TRUE(c.sink.has_on_ch(4));
}

static void test_harmony_dir_latches_note() {
    Ctx c; c.set_harmony();
    ASSERT_EQ(c.js.drone_active_mask, 0);
    c.joy(2);
    ASSERT_NE(c.js.drone_active_mask, 0);
}

static void test_harmony_same_dir_again_unlatches() {
    Ctx c;
    c.s().midi_channel_joy = 4;
    c.set_harmony();
    c.joy(2);
    uint8_t played = c.js.drone_notes[2];
    ASSERT_NE(c.js.drone_active_mask, 0);
    c.sink.clear();
    c.joy(-1);  // release the gesture so the next press is a fresh dc=true
    c.joy(2);   // same direction again -> un-latches
    ASSERT_TRUE(c.sink.has_off_note_ch(played, 4));
    ASSERT_EQ(c.js.drone_active_mask, 0);
}

static void test_harmony_center_is_noop_latch_persists() {
    Ctx c;
    c.s().midi_channel_joy = 4;
    c.set_harmony();
    c.joy(2);
    ASSERT_NE(c.js.drone_active_mask, 0);
    c.sink.clear();
    c.joy(-1);  // center
    ASSERT_EQ(c.sink.count_off(), 0);
    ASSERT_NE(c.js.drone_active_mask, 0);
}

static void test_harmony_new_dir_mono_stops_previous() {
    Ctx c;
    c.s().midi_channel_joy = 4;
    c.set_harmony();
    c.joy(0);
    uint8_t first = c.js.drone_notes[0];
    ASSERT_TRUE(c.js.drone_active_mask & (1u << 0));
    c.sink.clear();
    c.joy(2);  // new direction, Mono=on -> stops direction 0, latches direction 2
    ASSERT_TRUE(c.sink.has_off_note_ch(first, 4));
    ASSERT_FALSE(c.js.drone_active_mask & (1u << 0));
    ASSERT_TRUE(c.js.drone_active_mask & (1u << 2));
}

static void test_harmony_center_noop_when_no_active_note() {
    Ctx c;
    c.s().midi_channel_joy = 4;
    c.set_harmony();
    ASSERT_EQ(c.js.drone_active_mask, 0);
    c.joy(-1);  // center with no note active — no crash, no note_off
    ASSERT_EQ(c.sink.count_off(), 0);
}

static void test_harmony_mode_transition_kills_latched_note() {
    Ctx c;
    c.s().midi_channel_joy = 4;
    c.set_harmony();
    c.joy(2);
    uint8_t h = c.js.drone_notes[2];
    c.sink.clear();
    c.set_mode(JoystickPlayMode::kQuality);
    ASSERT_EQ(c.js.drone_active_mask, 0);
    ASSERT_TRUE(c.sink.has_off_note_ch(h, 4));
}

// ===========================================================================
// Suite 6 — center-release exhaustiveness
// ===========================================================================

static void test_center_release_bassclock_calls_notes_off() {
    // kBassClock NOT in the exclusion list → joy_notes_off() must be called
    Ctx c;
    c.s().midi_channel_joy = 3;
    c.set_mode(JoystickPlayMode::kBassClock);
    c.joy(0);   // play a note
    c.sink.clear();
    c.joy(-1);  // center release
    ASSERT_GT(c.sink.count_off(), 0);
}

static void test_center_release_drone_preserves_notes() {
    // kDrone excluded → center release does NOT call joy_notes_off
    Ctx c; c.set_mode(JoystickPlayMode::kDrone);
    c.joy(0);
    c.sink.clear();
    c.joy(-1);
    ASSERT_EQ(c.sink.count_off(), 0);
    ASSERT_NE(c.js.drone_active_mask, 0);  // still active
}

static void test_center_release_soloist_calls_release_held() {
    // kSoloist is an engine mode → ctx.engine.release_held() called; no crash
    Ctx c; c.set_mode(JoystickPlayMode::kSoloist);
    c.joy(2);  // play a note
    c.joy(-1); // center
    // Just verify no crash and mode is still kSoloist
    ASSERT_EQ(c.js.mode, JoystickPlayMode::kSoloist);
}

static void test_center_release_harmony_latch_persists() {
    // Harmony is now Sustain=latch; center release isn't one of the release
    // triggers (same direction again, panic, preset change).
    Ctx c;
    c.s().midi_channel_joy = 4;
    c.set_harmony();
    c.joy(0);
    c.sink.clear();
    c.joy(-1);
    ASSERT_EQ(c.sink.count_off(), 0);
    ASSERT_NE(c.js.drone_active_mask, 0);
}

static void test_center_release_arpcontrol_excluded_no_notes_off() {
    // kArpControl (now CONT/Macro) has no direct notes → center does nothing
    Ctx c; c.set_mode(JoystickPlayMode::kArpControl);
    c.joy(0);
    c.sink.clear();
    c.joy(-1);
    ASSERT_EQ(c.sink.count_off(), 0);
}

// ===========================================================================
// Suite 7 — producer safety guards
// ===========================================================================











// ===========================================================================
// Suite 8 — kInterval: NOTES/source=intervals, gesture=clock (config
// migration table). The old scale/chord+/power sub-mode toggle is retired —
// nothing in the new NOTES parameter table names it — so this now tests the
// single Source=intervals behavior directly.
// ===========================================================================

static app::NotesParams interval_params() {
    app::NotesParams p;
    p.source = app::NoteSource::kIntervals;
    p.gesture = app::NoteGesture::kClock;
    return p;
}

static void test_interval_all_dirs_no_crash() {
    Ctx c;
    c.set_notes(interval_params());
    c.nav.active_degree = 1;
    for (int8_t d = 0; d < 8; ++d) {
        c.joy(d);
    }
}

static void test_interval_sends_notes() {
    Ctx c;
    c.set_notes(interval_params());
    c.nav.active_degree = 1;
    c.joy(2);
    ASSERT_GT(c.sink.count_on(), 0);
}

// ===========================================================================
// Suite 9 — kPentatonic: NOTES/source=pentatonic, gesture=walk. The old
// major/minor/blues sub-mode toggle is retired — major vs. minor is now
// inferred from the current scale, and blues is dropped (NoteSource has one
// pentatonic value, not three).
// ===========================================================================

static app::NotesParams pentatonic_params() {
    app::NotesParams p;
    p.source = app::NoteSource::kPentatonic;
    p.gesture = app::NoteGesture::kWalk;
    return p;
}

static void test_pentatonic_north_moves_walk_up() {
    Ctx c;
    c.set_notes(pentatonic_params());
    uint8_t before = c.js.walk_index;
    c.joy(2);  // N = vstep
    // Either a note was played OR just the index moved — both are valid
    ASSERT_TRUE(c.sink.count_on() > 0 || c.js.walk_index != before);
}

static void test_pentatonic_no_crash_all_dirs() {
    Ctx c;
    c.set_notes(pentatonic_params());
    for (int8_t d = 0; d < 8; ++d) c.joy(d);
    ASSERT_GT(c.sink.count_on(), 0);
}

static void test_pentatonic_walk_index_wraps_at_8() {
    Ctx c;
    c.set_notes(pentatonic_params());
    c.js.walk_index = 7;
    c.joy(2);  // vstep=+1 → wrap_index(8, 8) = 0
    ASSERT_EQ(c.js.walk_index, 0);
}

static void test_pentatonic_east_adjusts_velocity_min_1() {
    Ctx c;
    c.set_notes(pentatonic_params());
    ASSERT_TRUE(c.edit([](contract::Config& k) { k.solo_velocity = 1; }));
    c.joy(0);  // E = hstep=+1 → vel + 10
    ASSERT_EQ(c.s().solo_velocity, 11);
    ASSERT_EQ(c.app_state.config().solo_velocity, 11);
    c.joy(4); c.joy(4);  // W twice: floors at 1, never 0 (a vel-0 note-on is a note-off)
    ASSERT_EQ(c.s().solo_velocity, 1);
}

static void test_pentatonic_west_adjusts_velocity_max_127() {
    Ctx c;
    c.set_notes(pentatonic_params());
    ASSERT_TRUE(c.edit([](contract::Config& k) { k.solo_velocity = 127; }));
    c.joy(4);  // W = hstep=-1 → vel - 10
    ASSERT_EQ(c.s().solo_velocity, 117);
    ASSERT_EQ(c.app_state.config().solo_velocity, 117);
    c.joy(0); c.joy(0);  // E twice: caps at 127
    ASSERT_EQ(c.s().solo_velocity, 127);
}

// ===========================================================================
// Suite 10 — vibrato clamp formula (MIDI pitch bend range -8192..8191)
// ===========================================================================

static void test_vibrato_clamp_upper_bound_8191() {
    // Production: max(-8192, min(8191, (int)bend + vib))
    int bend = 8100, vib = 500;
    int result = std::max(-8192, std::min(8191, bend + vib));
    ASSERT_LE(result, 8191);
    ASSERT_EQ(result, 8191);
}

static void test_vibrato_clamp_lower_bound_neg8192() {
    int bend = -8100, vib = -500;
    int result = std::max(-8192, std::min(8191, bend + vib));
    ASSERT_GE(result, -8192);
    ASSERT_EQ(result, -8192);
}

static void test_vibrato_clamp_passes_through_in_range() {
    int bend = 1000, vib = 200;
    int result = std::max(-8192, std::min(8191, bend + vib));
    ASSERT_EQ(result, 1200);
}

static void test_vibrato_off_by_one_fixed_lower_is_8192_not_8191() {
    // MIDI pitch bend's lower limit is -8192, not -8191.
    ASSERT_EQ(std::max(-8192, -9999), -8192);
    ASSERT_NE(std::max(-8191, -9999), -8192);  // old buggy value
}

// ===========================================================================
// Suite 11 — Mode transition stability
// ===========================================================================

static void test_apply_mode_sets_mode_field() {
    Ctx c;
    c.set_mode(JoystickPlayMode::kArpControl);
    ASSERT_EQ(c.js.mode, JoystickPlayMode::kArpControl);
    c.set_mode(JoystickPlayMode::kDrone);
    ASSERT_EQ(c.js.mode, JoystickPlayMode::kDrone);
}

static void test_apply_mode_resets_last_dir_to_center() {
    Ctx c;
    c.js.last_dir = 3;
    c.set_mode(JoystickPlayMode::kQuality);
    ASSERT_EQ(c.js.last_dir, -1);  // apply_joystick_mode always resets to -1
}

static void test_all_17_modes_can_be_initialized() {
    for (uint8_t m = 0; m < static_cast<uint8_t>(JoystickPlayMode::kCOUNT); ++m) {
        Ctx c;
        c.set_mode(static_cast<JoystickPlayMode>(m));
        ASSERT_EQ(c.js.mode, static_cast<JoystickPlayMode>(m));
    }
}

static void test_mode_switch_from_bassclock_stops_notes() {
    Ctx c;
    c.set_mode(JoystickPlayMode::kBassClock);
    c.joy(0);
    int ons = c.sink.count_on();
    c.sink.clear();
    c.set_mode(JoystickPlayMode::kQuality);
    // If notes were playing, they must be stopped
    ASSERT_TRUE(ons == 0 || c.sink.count_off() > 0);
}

static void test_mode_switch_resets_pitch_bend() {
    Ctx c;
    c.js.last_bend = 2000;
    c.set_mode(JoystickPlayMode::kQuality);
    ASSERT_EQ(c.js.last_bend, 0);
}

// ===========================================================================
// main
// ===========================================================================

int main() {
    TEST_SUITE("kArpControl — no PARAM preset left");
    RUN_TEST(test_arpcontrol_lands_on_macro_and_directions_edit_nothing);

    TEST_SUITE("kDrone — toggle, mask bits, bitshift safety");
    RUN_TEST(test_drone_first_press_note_on);
    RUN_TEST(test_drone_first_press_sets_mask);
    RUN_TEST(test_drone_second_press_note_off);
    RUN_TEST(test_drone_second_press_clears_mask);
    RUN_TEST(test_drone_uses_chord_channel);
    RUN_TEST(test_drone_all_8_dirs_safe);
    RUN_TEST(test_drone_dirs_set_independent_mask_bits);
    RUN_TEST(test_drone_center_is_noop_does_not_note_off);
    RUN_TEST(test_drone_mode_transition_kills_active_drones);
    RUN_TEST(test_drone_toggled_off_correct_note_sent);

    TEST_SUITE("kHarmony — note-on, latch persistence, center cleanup");
    RUN_TEST(test_harmony_dir_sends_note_on_joy_ch);
    RUN_TEST(test_harmony_dir_latches_note);
    RUN_TEST(test_harmony_same_dir_again_unlatches);
    RUN_TEST(test_harmony_center_is_noop_latch_persists);
    RUN_TEST(test_harmony_new_dir_mono_stops_previous);
    RUN_TEST(test_harmony_center_noop_when_no_active_note);
    RUN_TEST(test_harmony_mode_transition_kills_latched_note);

    // kScaleExplore and kEuclidean were removed entirely: ScaleExplore doesn't return as an Explore source,
    // and Euclidean has no replacement — the MIDI clock still drives the
    // arpeggiator, untouched.

    TEST_SUITE("center-release exhaustiveness");
    RUN_TEST(test_center_release_bassclock_calls_notes_off);
    RUN_TEST(test_center_release_drone_preserves_notes);
    RUN_TEST(test_center_release_soloist_calls_release_held);
    RUN_TEST(test_center_release_harmony_latch_persists);
    RUN_TEST(test_center_release_arpcontrol_excluded_no_notes_off);






    TEST_SUITE("kInterval — NOTES/source=intervals");
    RUN_TEST(test_interval_all_dirs_no_crash);
    RUN_TEST(test_interval_sends_notes);

    TEST_SUITE("kPentatonic walk");
    RUN_TEST(test_pentatonic_north_moves_walk_up);
    RUN_TEST(test_pentatonic_no_crash_all_dirs);
    RUN_TEST(test_pentatonic_walk_index_wraps_at_8);
    RUN_TEST(test_pentatonic_east_adjusts_velocity_min_1);
    RUN_TEST(test_pentatonic_west_adjusts_velocity_max_127);

    TEST_SUITE("vibrato clamp formula (full -8192..8191 range)");
    RUN_TEST(test_vibrato_clamp_upper_bound_8191);
    RUN_TEST(test_vibrato_clamp_lower_bound_neg8192);
    RUN_TEST(test_vibrato_clamp_passes_through_in_range);
    RUN_TEST(test_vibrato_off_by_one_fixed_lower_is_8192_not_8191);

    TEST_SUITE("Mode transition stability");
    RUN_TEST(test_apply_mode_sets_mode_field);
    RUN_TEST(test_apply_mode_resets_last_dir_to_center);
    RUN_TEST(test_all_17_modes_can_be_initialized);
    RUN_TEST(test_mode_switch_from_bassclock_stops_notes);
    RUN_TEST(test_mode_switch_resets_pitch_bend);

    test_print_result();
    return (g_fail > 0) ? 1 : 0;
}
