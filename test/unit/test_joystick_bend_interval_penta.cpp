// Tests: kBend, kInterval, kPentatonic joystick mode logic.
//
// These are the last three modes without dedicated behavioral tests.
// kBend   — analog: X→pitch-bend (chord-quantised), Y→vibrato depth
//           (CONT/Bend: each axis follows its own ContTarget; the stub's
//           joy_x/joy_y are raw ADC units, normalised to +/-512 on the way in)
// kInterval — digital: 8 dirs → scale/chord/power interval notes
// kPentatonic — digital: N/S walk pentatonic scale, E/W adjust velocity

#include "../framework.h"
#include "../stubs/joy_stub.h"

#include "app/production_ui.h"
#include "app/config_codec.h"
#include "app/input_context.h"
#include "app/input_joystick.h"
#include "app/settings_bridge.h"
#include "app/progression_modes.h"
#include "app/state.h"
#include "app/joystick_state.h"
#include "app/ui_state.h"
#include "app/types.h"
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

// ---------------------------------------------------------------------------
// Shared capture sink

struct CapSink : midi::TransportSink {
    struct Msg { std::string t; uint8_t ch, a, b; };
    std::vector<Msg> msgs;

    void send_note_on (uint8_t ch, uint8_t n, uint8_t v) override
        { msgs.push_back({"on",  ch, n, v}); }
    void send_note_off(uint8_t ch, uint8_t n, uint8_t v) override
        { msgs.push_back({"off", ch, n, v}); }
    void send_pitch_bend(uint8_t ch, int16_t v) override
        { msgs.push_back({"pb", ch, (uint8_t)(v >> 8), (uint8_t)v}); }
    void send_control_change(uint8_t ch, uint8_t cc, uint8_t v) override
        { msgs.push_back({"cc", ch, cc, v}); }

    int  count_on() const { int c=0; for (auto& m:msgs) if(m.t=="on")  ++c; return c; }
    bool has_pb()   const { for (auto& m:msgs) if(m.t=="pb") return true; return false; }
    // Reconstruct the last pitch-bend value from the stored bytes.
    int16_t last_pb() const {
        for (int i = (int)msgs.size()-1; i >= 0; --i)
            if (msgs[i].t == "pb")
                return (int16_t)((uint16_t)((uint16_t)msgs[i].a << 8) | msgs[i].b);
        return 0;
    }
    void clear() { msgs.clear(); }
};

// ---------------------------------------------------------------------------
// Shared test context — injectable analog joystick (for kBend)
// and direct dispatch (for kInterval / kPentatonic).

struct Ctx {
    AppState         app_state;
    engine::Engine   engine;
    midi::Router     router;
    midi::BleMidi    ble{router};
    midi::UsbDeviceMidi usb{router};
    JoystickState    js;
    UiNavState       nav;
    std::vector<engine::ProgressionStep> play_prog;
    // NOTES now manages every note it plays directly through the ledger
    // (input_context.cpp: joy_add_note/joy_note_acquire), rather than
    // Engine::play_notes()'s internal scheduler — without a ledger wired up,
    // joy_note_acquire() always fails and no note reaches the sink.
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
    drivers::JoystickAnalog     joystick;
    drivers::JoystickAnalogConfig joy_cfg;

    Ctx() {
        app_state.sanitize();
        engine.begin();
        router.register_sink(&sink);
        engine.set_router(&router);
        app_state.settings().voice_leading   = 0;
        app_state.settings().humanize_ms     = 0;
        app_state.settings().midi_channel    = 0;
        app_state.settings().midi_channel_joy = 3;
        // Config is the authority: anything that commits an edit (walk
        // left/right, a menu edit) projects Config back onto Settings, so the
        // fixture's channel/voice-leading choices must live in Config too.
        app_state.set_config(app::config::from_settings(app_state.settings()));
        apply_settings(app_state, engine, ble, usb);
        apply_progression_mode(engine, ProgressionMode::kMajor,
                               app_state.settings());
        prod::begin(ui, ctx);
        js.last_dir = -1;
        joy_cfg.deadzone = 0;
        stub::joy_reset();
    }

    Settings& s() { return app_state.settings(); }

    // Edits Config the way the encoder does, through the one commit path.
    template <class F> bool edit(F&& change) {
        auto config = app_state.config();
        change(config);
        return commit_config(ctx, config);
    }

    void set_mode(JoystickPlayMode m) { apply_joystick_mode(ctx, m); }

    // Analog joystick call — used for kBend.
    void joy_analog(int16_t x, int16_t y, int8_t dir = -1) {
        stub::joy_x   = x;
        stub::joy_y   = y;
        stub::joy_dir = dir;
        process_joystick(ctx, joystick, joy_cfg);
    }

    // Digital direction dispatch — used for kInterval / kPentatonic.
    // Forces dc=true by ensuring last_dir ≠ dir.
    void joy_dir(int8_t dir) {
        js.keypad_dir = static_cast<int8_t>(dir == 0 ? -1 : dir - 1);
        dispatch_keypad_joy_direction(ctx, dir);
    }

    // Builds an exact config-migration-table row when the nearest-
    // preset mapping apply_joystick_mode() uses isn't a faithful stand-in.
    void set_notes(const app::NotesParams& p) {
        auto config = app_state.config();
        config.active_preset = app::PresetId::kLead;
        config.joy_families.notes = p;
        app_state.set_config(config);
        app::reset_joystick_gesture_state(ctx);
    }
};

// ===========================================================================
// Suite 1 — kBend (CONT family, Bend preset: X=Bend, Y=Vibrato)
// ===========================================================================
// handle_cont() reads the normalised stick position (stub ADC units are
// scaled by 512/1850 and clamped to +/-512):
//   X → chord-quantised pitch bend  [-8192..+8191], full scale at the rail
//   Y → vibrato: an LFO of up to +/-50 cents added to the same bend stream,
//       driven by ctx.t (not by how often the loop runs)
// The centre deadzone is ContParams::deadzone (percent, default 10).
// A message is only sent when the computed value differs from last_bend.
// Channel: midi_channel (chord screen) or midi_channel_joy (other screens).
// ===========================================================================

// ADC units at the end stop: normalises to exactly +/-512.
static constexpr int16_t kRail = 1850;

static void test_bend_center_no_message() {
    // Centre: inside the deadzone, bend 0 equals last_bend 0 → no send.
    Ctx c; c.set_mode(JoystickPlayMode::kBend); c.sink.clear();
    c.joy_analog(0, 0);
    ASSERT_FALSE(c.sink.has_pb());
}

static void test_bend_inside_deadzone_sends_nothing() {
    // 10% of 512 = 51 axis units ≈ 184 ADC units.
    Ctx c; c.set_mode(JoystickPlayMode::kBend);
    c.nav.active_degree = 1;
    c.sink.clear();
    c.joy_analog(150, 0);
    c.joy_analog(-150, 150);
    ASSERT_FALSE(c.sink.has_pb());
    ASSERT_EQ(c.js.last_bend, 0);
}

static void test_bend_right_sends_positive_bend() {
    // x=+1000 ADC → 276 axis → ~5.8 st of a 12 st range.
    // C major triad intervals {0,4,7}: nearest is 7 → bend > 0.
    Ctx c; c.set_mode(JoystickPlayMode::kBend);
    c.nav.active_degree = 1;
    c.sink.clear();
    c.joy_analog(1000, 0);
    ASSERT_TRUE(c.sink.has_pb());
    ASSERT_GT(c.sink.last_pb(), 0);
}

static void test_bend_large_left_sends_negative_bend() {
    // x=-1500 ADC → -415 axis → ~-9.5 st → snaps to a chord tone below.
    Ctx c; c.set_mode(JoystickPlayMode::kBend);
    c.nav.active_degree = 1;
    c.sink.clear();
    c.joy_analog(-1500, 0);
    ASSERT_TRUE(c.sink.has_pb());
    ASSERT_LT(c.sink.last_pb(), 0);
}

static void test_bend_positive_and_negative_are_different() {
    Ctx cl; cl.set_mode(JoystickPlayMode::kBend);
    cl.nav.active_degree = 1;
    cl.sink.clear();
    cl.joy_analog(-1500, 0);
    int16_t neg = cl.sink.last_pb();

    Ctx cr; cr.set_mode(JoystickPlayMode::kBend);
    cr.nav.active_degree = 1;
    cr.sink.clear();
    cr.joy_analog(1000, 0);
    int16_t pos = cr.sink.last_pb();

    ASSERT_LT(neg, 0);
    ASSERT_GT(pos, 0);
    ASSERT_NE(neg, pos);
}

static void test_bend_full_deflection_reaches_full_scale() {
    // With snapping off the rail is the whole 14-bit range in both directions.
    Ctx c; c.set_mode(JoystickPlayMode::kBend);
    ASSERT_TRUE(c.edit([](contract::Config& k) { k.joy_families.cont.bend_snap = ContBendSnap::kOff; }));
    c.joy_analog(0, 0);  // the commit asks for neutral first
    c.joy_analog(kRail, 0);
    ASSERT_EQ(c.js.last_bend, 8191);
    c.joy_analog(-kRail, 0);
    ASSERT_EQ(c.js.last_bend, -8192);
    // Past the rail (a stick that over-reads) clamps instead of wrapping.
    c.joy_analog(2047, 0);
    ASSERT_EQ(c.js.last_bend, 8191);
}

static void test_bend_y_nonzero_sets_vibrato_depth() {
    // Full Y deflection → depth=1.0 → bend_vibrato_depth=127, either way.
    Ctx c; c.set_mode(JoystickPlayMode::kBend); c.sink.clear();
    c.joy_analog(0, kRail);
    ASSERT_EQ(c.js.bend_vibrato_depth, 127);
    c.joy_analog(0, -kRail);
    ASSERT_EQ(c.js.bend_vibrato_depth, 127);
}

static void test_bend_y_partial_sets_partial_depth() {
    // Half travel → roughly half depth (less the rescaled deadzone).
    Ctx c; c.set_mode(JoystickPlayMode::kBend); c.sink.clear();
    c.joy_analog(0, kRail / 2);
    ASSERT_GT(c.js.bend_vibrato_depth, 30);
    ASSERT_LT(c.js.bend_vibrato_depth, 80);
}

static void test_bend_y_zero_clears_vibrato_depth() {
    // Set vibrato manually, then call with y=0 to clear it.
    Ctx c; c.set_mode(JoystickPlayMode::kBend); c.sink.clear();
    c.js.bend_vibrato_depth = 80;
    c.joy_analog(0, 0);
    ASSERT_EQ(c.js.bend_vibrato_depth, 0);
}

static void test_bend_vibrato_sends_pb_message() {
    // 5.5 Hz LFO: t=45 ms is a quarter cycle, the positive peak. Full depth
    // is +50 cents = 8192 * 0.5 / 12 ≈ 341 at the default 12 st range.
    Ctx c; c.set_mode(JoystickPlayMode::kBend); c.sink.clear();
    c.ctx.t = 45;
    c.joy_analog(0, kRail);
    ASSERT_TRUE(c.sink.has_pb());
    ASSERT_GE(c.sink.last_pb(), 335);
    ASSERT_LE(c.sink.last_pb(), 342);
}

static void test_bend_vibrato_follows_the_clock_not_the_call_count() {
    Ctx c; c.set_mode(JoystickPlayMode::kBend);
    c.ctx.t = 45;
    c.joy_analog(0, kRail);
    const int16_t peak = c.js.last_bend;
    // Same instant, more loop iterations: the LFO has not moved.
    c.sink.clear();
    for (int i = 0; i < 20; ++i) c.joy_analog(0, kRail);
    ASSERT_FALSE(c.sink.has_pb());
    ASSERT_EQ(c.js.last_bend, peak);
    // Half a cycle later (1000 / 5.5 / 2 ≈ 91 ms) it is at the opposite peak.
    c.ctx.t = 45 + 91;
    c.joy_analog(0, kRail);
    ASSERT_LT(c.js.last_bend, -330);
}

static void test_bend_vibrato_stays_within_50_cents() {
    // +/-50 cents whatever the bend range is: 341 of 8192 at 12 st, 2048 at 2.
    for (int range : {12, 2}) {
        Ctx c; c.engine.set_bend_range(range);
        c.set_mode(JoystickPlayMode::kBend);
        const int limit = 8192 * 50 / (100 * range) + 1;
        int lo = 0, hi = 0;
        for (uint32_t t = 0; t < 400; t += 3) {
            c.ctx.t = t;
            c.joy_analog(0, -kRail);
            lo = std::min<int>(lo, c.js.last_bend);
            hi = std::max<int>(hi, c.js.last_bend);
        }
        ASSERT_LE(hi, limit);
        ASSERT_GE(lo, -limit);
        ASSERT_GE(hi, limit - 8);
        ASSERT_LE(lo, -limit + 8);
    }
}

static void test_bend_dedup_no_second_message() {
    // First call sends a message; second call with identical X sends nothing extra.
    Ctx c; c.set_mode(JoystickPlayMode::kBend);
    c.nav.active_degree = 1;
    c.sink.clear();
    c.joy_analog(1000, 0);
    size_t after_first = c.sink.msgs.size();
    c.joy_analog(1000, 0);
    ASSERT_EQ(c.sink.msgs.size(), after_first);
}

static void test_bend_updates_last_bend() {
    Ctx c; c.engine.set_bend_range(12);
    c.set_mode(JoystickPlayMode::kBend);
    c.nav.active_degree = 1;
    c.sink.clear();
    c.joy_analog(1000, 0);
    ASSERT_NE(c.js.last_bend, 0);
}

static void test_bend_marks_dirty() {
    Ctx c; c.engine.set_bend_range(12);
    c.set_mode(JoystickPlayMode::kBend);
    c.nav.active_degree = 1;
    c.nav.dirty = false;
    c.sink.clear();
    c.joy_analog(1000, 0);
    ASSERT_TRUE(c.nav.dirty);
}

static void test_bend_chord_screen_uses_chord_channel() {
    // Screen::kPlay is a chord screen → bend goes on midi_channel (0).
    Ctx c; c.engine.set_bend_range(12);
    c.set_mode(JoystickPlayMode::kBend);
    c.nav.active_degree = 1;
    ASSERT_TRUE(c.nav.chord_context);
    c.sink.clear();
    c.joy_analog(1000, 0);
    ASSERT_TRUE(c.sink.has_pb());
    for (int i = (int)c.sink.msgs.size()-1; i >= 0; --i) {
        if (c.sink.msgs[i].t == "pb") {
            ASSERT_EQ(c.sink.msgs[i].ch, 0);
            break;
        }
    }
}

static void test_bend_non_chord_screen_uses_joy_channel() {
    // Screen::kSoloist is NOT a chord screen → bend goes on midi_channel_joy (3).
    Ctx c; c.engine.set_bend_range(12);
    c.set_mode(JoystickPlayMode::kBend);
    c.nav.chord_context = false;
    c.nav.active_degree = 1;
    c.sink.clear();
    c.joy_analog(1000, 0);
    ASSERT_TRUE(c.sink.has_pb());
    for (int i = (int)c.sink.msgs.size()-1; i >= 0; --i) {
        if (c.sink.msgs[i].t == "pb") {
            ASSERT_EQ(c.sink.msgs[i].ch, 3);
            break;
        }
    }
}

static void test_panic_bend_requires_neutral_before_resume() {
    Ctx c; c.set_mode(JoystickPlayMode::kBend); c.nav.active_degree = 1;
    c.joy_analog(1000, 0);
    ASSERT_TRUE(c.sink.has_pb());
    joy_all_notes_off(c.ctx);
    c.sink.clear();
    c.joy_analog(1000, 0);
    ASSERT_FALSE(c.sink.has_pb());
    c.joy_analog(0, 0);
    c.joy_analog(1000, 0);
    ASSERT_TRUE(c.sink.has_pb());
}

static void test_recipe_change_while_held_requires_neutral() {
    Ctx c;
    c.js.last_dir = 0;  // the joystick is still held when the recipe changes
    c.set_mode(JoystickPlayMode::kPentatonic);
    ASSERT_TRUE(c.js.recipe_neutral_required);
    c.sink.clear();
    c.joy_dir(0);
    ASSERT_EQ(c.sink.count_on(), 0);
    c.joy_dir(-1);
    ASSERT_FALSE(c.js.recipe_neutral_required);
    c.joy_dir(0);
    ASSERT_FALSE(c.js.recipe_neutral_required);
}

// ===========================================================================
// Suite 2 — kInterval: NOTES/source=intervals, gesture=clock (config
// migration table). The old scale/chord+/power sub-mode toggle is retired —
// Source=intervals is now always the fixed harmonic stack (octave, fifth,
// third, sixth and their inversions) in kHarmonyIntervals, the same content
// the Harmony preset uses. Requires dc=true (direction change). Notes are
// self-managed directly (no longer routed through the engine).
// ===========================================================================

static app::NotesParams interval_params() {
    app::NotesParams p;
    p.source = app::NoteSource::kIntervals;
    p.gesture = app::NoteGesture::kClock;
    return p;
}

static void test_interval_no_action_on_center() {
    // dir=-1 (center): guard `if (!dc || dir < 0) return;` prevents note.
    Ctx c; c.set_notes(interval_params());
    c.sink.clear();
    // With last_dir=-1 and dir=-1: dc=false, so guard fires.
    c.js.last_dir = -1;
    dispatch_keypad_joy_direction(c.ctx, -1);
    ASSERT_EQ(c.sink.count_on(), 0);
}

static void test_interval_north_sends_note() {
    Ctx c; c.set_notes(interval_params());
    c.sink.clear();
    c.joy_dir(2);  // N
    ASSERT_GT(c.sink.count_on(), 0);
}

static void test_interval_all_8_dirs_no_crash() {
    for (int8_t d = 0; d < 8; ++d) {
        Ctx c; c.set_notes(interval_params());
        c.sink.clear();
        c.joy_dir(d);
    }
}

static void test_interval_different_dirs_give_different_last_notes() {
    // N and E map to kClockMap[2]=0 and kClockMap[0]=2 respectively.
    // These are different slots in interval_notes, so last_note should differ.
    Ctx c1; c1.set_notes(interval_params());
    c1.nav.active_degree = 1;
    c1.joy_dir(2);  // N → slot 0
    uint8_t note_n = c1.js.last_note;

    Ctx c2; c2.set_notes(interval_params());
    c2.nav.active_degree = 1;
    c2.joy_dir(0);  // E → slot 2
    uint8_t note_e = c2.js.last_note;

    ASSERT_NE(note_n, note_e);
}

static void test_interval_updates_last_note() {
    Ctx c; c.set_notes(interval_params());
    c.js.last_note = 0;
    c.nav.active_degree = 1;
    c.joy_dir(2);
    ASSERT_NE(c.js.last_note, 0);
}

static void test_interval_marks_dirty() {
    Ctx c; c.set_notes(interval_params());
    c.nav.dirty = false;
    c.nav.active_degree = 1;
    c.joy_dir(2);
    ASSERT_TRUE(c.nav.dirty);
}

static void test_interval_n_ne_slots_are_a_fifth_apart() {
    // Source=intervals uses kHarmonyIntervals = {12,7,4,9,-12,-5,-3,-15}.
    // N -> kClockMap[2]=0 -> +12 (octave); NE -> kClockMap[1]=1 -> +7 (fifth).
    Ctx c; c.set_notes(interval_params());
    c.nav.active_degree = 1;

    c.joy_dir(2);  // N -> slot 0 (root+12)
    uint8_t note0 = c.js.last_note;

    c.joy_dir(1);  // NE -> slot 1 (root+7)
    uint8_t note1 = c.js.last_note;

    ASSERT_EQ(note0 - note1, 5);
}

static void test_interval_note_is_valid_midi() {
    Ctx c; c.set_notes(interval_params());
    c.nav.active_degree = 1;
    c.joy_dir(2);
    ASSERT_GE(c.js.last_note, 0);
    ASSERT_LE(c.js.last_note, 127);
}

// ===========================================================================
// Suite 3 — kPentatonic: NOTES/source=pentatonic, gesture=walk
// (config migration table).
//   N/NE/NW: step up (walk_index++, play note)
//   S/SE/SW: step down (walk_index--, play note)
//   E:       velocity +10 (no note)
//   W:       velocity -10 (no note)
// Major vs. minor pentatonic {0,2,4,7,9} / {0,3,5,7,10} is now inferred from
// settings.scale, not a runtime sub-mode toggle; blues is dropped entirely
// (deliberate simplification — NoteSource has one pentatonic value, not
// three). walk_index wraps around modulo 8.
// ===========================================================================

static app::NotesParams pentatonic_params() {
    app::NotesParams p;
    p.source = app::NoteSource::kPentatonic;
    p.gesture = app::NoteGesture::kWalk;
    return p;
}

static void test_penta_north_increments_walk_index() {
    Ctx c; c.set_notes(pentatonic_params());
    c.js.walk_index = 0;
    c.joy_dir(2);  // N → vstep=+1
    ASSERT_EQ(c.js.walk_index, 1);
}

static void test_penta_south_decrements_walk_index() {
    Ctx c; c.set_notes(pentatonic_params());
    c.js.walk_index = 3;
    c.joy_dir(6);  // S → vstep=-1
    ASSERT_EQ(c.js.walk_index, 2);
}

static void test_penta_north_sends_note() {
    Ctx c; c.set_notes(pentatonic_params());
    c.sink.clear();
    c.joy_dir(2);
    ASSERT_GT(c.sink.count_on(), 0);
}

static void test_penta_south_sends_note() {
    Ctx c; c.set_notes(pentatonic_params());
    c.js.walk_index = 4;
    c.sink.clear();
    c.joy_dir(6);
    ASSERT_GT(c.sink.count_on(), 0);
}

static void test_penta_east_no_note_velocity_up() {
    Ctx c; c.set_notes(pentatonic_params());
    c.sink.clear();
    uint8_t vel_before = c.s().solo_velocity;
    c.joy_dir(0);  // E → hstep=+1
    ASSERT_EQ(c.sink.count_on(), 0);
    ASSERT_EQ(c.s().solo_velocity, vel_before + 10);
    // The edit is a Config edit: the menu's Vel row and the stored settings
    // see the same value, and it is queued for autosave.
    ASSERT_EQ(c.app_state.config().solo_velocity, vel_before + 10);
    ASSERT_TRUE(c.nav.settings_dirty);
}

static void test_penta_west_no_note_velocity_down() {
    Ctx c; c.set_notes(pentatonic_params());
    ASSERT_TRUE(c.edit([](contract::Config& k) { k.solo_velocity = 80; }));
    c.sink.clear();
    c.joy_dir(4);  // W → hstep=-1
    ASSERT_EQ(c.sink.count_on(), 0);
    ASSERT_EQ(c.s().solo_velocity, 70);
    ASSERT_EQ(c.app_state.config().solo_velocity, 70);
}

static void test_penta_velocity_minimum_is_1() {
    // spec: min=1 because MIDI note-on with vel=0 is treated as note-off.
    Ctx c; c.set_notes(pentatonic_params());
    ASSERT_TRUE(c.edit([](contract::Config& k) { k.solo_velocity = 12; }));
    // Many W presses should not drop below 1.
    for (int i = 0; i < 20; ++i) c.joy_dir(4);
    ASSERT_EQ(c.s().solo_velocity, 1);
    ASSERT_EQ(c.app_state.config().solo_velocity, 1);
}

static void test_penta_velocity_maximum_is_127() {
    Ctx c; c.set_notes(pentatonic_params());
    ASSERT_TRUE(c.edit([](contract::Config& k) { k.solo_velocity = 120; }));
    for (int i = 0; i < 20; ++i) c.joy_dir(0);
    ASSERT_EQ(c.s().solo_velocity, 127);
    ASSERT_EQ(c.app_state.config().solo_velocity, 127);
}

static void test_penta_walk_wraps_past_end() {
    // walk_index=7 then N → wrap_index(8,8)=0.
    Ctx c; c.set_notes(pentatonic_params());
    c.js.walk_index = 7;
    c.joy_dir(2);  // N
    ASSERT_EQ(c.js.walk_index, 0);
}

static void test_penta_walk_wraps_past_beginning() {
    // walk_index=0 then S → wrap_index(-1,8)=7.
    Ctx c; c.set_notes(pentatonic_params());
    c.js.walk_index = 0;
    c.joy_dir(6);  // S
    ASSERT_EQ(c.js.walk_index, 7);
}

static void test_penta_updates_last_note() {
    Ctx c; c.set_notes(pentatonic_params());
    c.js.last_note = 0;
    c.joy_dir(2);
    ASSERT_NE(c.js.last_note, 0);
}

static void test_penta_marks_dirty_on_note() {
    Ctx c; c.set_notes(pentatonic_params());
    c.nav.dirty = false;
    c.joy_dir(2);
    ASSERT_TRUE(c.nav.dirty);
}

static void test_penta_major_minor_different_notes_at_same_index() {
    // walk_index=1: major={0,2,4,7,9} -> D, minor={0,3,5,7,10} -> Eb in C.
    // Major/minor is now read from the engine-facing settings.scale (contains
    // "Minor" or not), which the Scale parameter drives.
    Ctx cm;
    cm.set_notes(pentatonic_params());
    cm.js.walk_index = 0;
    cm.joy_dir(2);  // step to index 1
    uint8_t major_note = cm.js.last_note;

    Ctx cmi;
    cmi.set_notes(pentatonic_params());
    ASSERT_TRUE(cmi.edit([](contract::Config& k) { k.scale = static_cast<uint8_t>(ProgressionMode::kNaturalMinor); }));
    ASSERT_STREQ(cmi.s().scale, "Natural Minor");
    cmi.js.walk_index = 0;
    cmi.joy_dir(2);
    uint8_t minor_note = cmi.js.last_note;

    ASSERT_NE(major_note, minor_note);
    ASSERT_EQ(major_note, 62);  // D4
    ASSERT_EQ(minor_note, 63);  // Eb4
}

// Octave 4 is middle C's octave for every NOTES source, so switching Source
// never jumps the register: the pentatonic root in C at Octave 0 is MIDI 60.
static void test_penta_root_is_middle_c() {
    Ctx c; c.set_notes(pentatonic_params());
    c.js.walk_index = 7;
    c.joy_dir(2);  // wraps to index 0, the root
    ASSERT_EQ(c.js.walk_index, 0);
    ASSERT_EQ(c.js.last_note, 60);
    c.joy_dir(2);  // index 1
    c.joy_dir(2);  // index 2
    ASSERT_EQ(c.js.last_note, 64);
}

// Blues is dropped entirely as a pentatonic variant (see suite comment).

static void test_penta_note_is_valid_midi() {
    Ctx c; c.set_notes(pentatonic_params());
    c.joy_dir(2);
    ASSERT_GE(c.js.last_note, 0);
    ASSERT_LE(c.js.last_note, 127);
}

static void test_penta_ne_also_steps_up() {
    // NE (dir=1) also gives vstep=+1.
    Ctx c; c.set_notes(pentatonic_params());
    c.js.walk_index = 2;
    c.joy_dir(1);  // NE
    ASSERT_EQ(c.js.walk_index, 3);
}

static void test_penta_se_also_steps_down() {
    // SE (dir=7) gives vstep=-1.
    Ctx c; c.set_notes(pentatonic_params());
    c.js.walk_index = 5;
    c.joy_dir(7);  // SE
    ASSERT_EQ(c.js.walk_index, 4);
}

// ===========================================================================
// Main
// ===========================================================================

int main() {
    TEST_SUITE("kBend — analog pitch bend + vibrato");
    RUN_TEST(test_bend_center_no_message);
    RUN_TEST(test_bend_inside_deadzone_sends_nothing);
    RUN_TEST(test_bend_full_deflection_reaches_full_scale);
    RUN_TEST(test_bend_vibrato_follows_the_clock_not_the_call_count);
    RUN_TEST(test_bend_vibrato_stays_within_50_cents);
    RUN_TEST(test_bend_right_sends_positive_bend);
    RUN_TEST(test_bend_large_left_sends_negative_bend);
    RUN_TEST(test_bend_positive_and_negative_are_different);
    RUN_TEST(test_bend_y_nonzero_sets_vibrato_depth);
    RUN_TEST(test_bend_y_partial_sets_partial_depth);
    RUN_TEST(test_bend_y_zero_clears_vibrato_depth);
    RUN_TEST(test_bend_vibrato_sends_pb_message);
    RUN_TEST(test_bend_dedup_no_second_message);
    RUN_TEST(test_bend_updates_last_bend);
    RUN_TEST(test_bend_marks_dirty);
    RUN_TEST(test_bend_chord_screen_uses_chord_channel);
    RUN_TEST(test_bend_non_chord_screen_uses_joy_channel);
    RUN_TEST(test_panic_bend_requires_neutral_before_resume);
    RUN_TEST(test_recipe_change_while_held_requires_neutral);

    TEST_SUITE("kInterval — 8-dir interval note selection");
    RUN_TEST(test_interval_no_action_on_center);
    RUN_TEST(test_interval_north_sends_note);
    RUN_TEST(test_interval_all_8_dirs_no_crash);
    RUN_TEST(test_interval_different_dirs_give_different_last_notes);
    RUN_TEST(test_interval_updates_last_note);
    RUN_TEST(test_interval_marks_dirty);
    RUN_TEST(test_interval_n_ne_slots_are_a_fifth_apart);
    RUN_TEST(test_interval_note_is_valid_midi);

    TEST_SUITE("kPentatonic — scale walk + velocity adjust");
    RUN_TEST(test_penta_north_increments_walk_index);
    RUN_TEST(test_penta_south_decrements_walk_index);
    RUN_TEST(test_penta_north_sends_note);
    RUN_TEST(test_penta_south_sends_note);
    RUN_TEST(test_penta_east_no_note_velocity_up);
    RUN_TEST(test_penta_west_no_note_velocity_down);
    RUN_TEST(test_penta_velocity_minimum_is_1);
    RUN_TEST(test_penta_velocity_maximum_is_127);
    RUN_TEST(test_penta_walk_wraps_past_end);
    RUN_TEST(test_penta_walk_wraps_past_beginning);
    RUN_TEST(test_penta_updates_last_note);
    RUN_TEST(test_penta_marks_dirty_on_note);
    RUN_TEST(test_penta_major_minor_different_notes_at_same_index);
    RUN_TEST(test_penta_root_is_middle_c);
    RUN_TEST(test_penta_note_is_valid_midi);
    RUN_TEST(test_penta_ne_also_steps_up);
    RUN_TEST(test_penta_se_also_steps_down);

    test_print_result();
    return g_fail > 0 ? 1 : 0;
}
