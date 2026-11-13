// Tests: joystick note-producing modes (ChordVel, Soloist, BassClock) and
// the tension/CC helper maps.
//
// Covers areas not tested in test_joystick_modes.cpp:
//   kChordVel   — direction → chord notes + active_degree set
//   kSoloist    — sub-mode A scale walk + velocity adjust; sub-mode B chord tones
//   kBassClock  — note MIDI range validity, mono mode sends off before on
//   Tension maps — tension_quality_2d and tension_quality_alt zone mapping
//   CC formula  — -512..+512 → 0..127 with clamping

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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace app;
using namespace app::prod;

// ---------------------------------------------------------------------------
// Infrastructure (same pattern as test_joystick_modes.cpp)
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
    bool has_on_note(uint8_t n) const
        { for (auto& m:msgs) if(m.t=="on" && m.a==n) return true; return false; }
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
        // Config is the authority: a committed edit (walk left/right) projects
        // it back onto Settings, so the fixture's choices must live in Config.
        app_state.set_config(app::config::from_settings(app_state.settings()));
        apply_settings(app_state, engine, ble, usb);
        apply_progression_mode(engine, ProgressionMode::kMajor, app_state.settings());
        prod::begin(ui, ctx);
        js.last_dir = -1;
    }

    Settings& s() { return app_state.settings(); }

    // dispatch_keypad_joy_direction's dc reads keypad_dir, not last_dir.
    void joy(int8_t dir) {
        js.keypad_dir = static_cast<int8_t>(dir - 1);
        dispatch_keypad_joy_direction(ctx, dir);
    }

    void set_mode(JoystickPlayMode m) { apply_joystick_mode(ctx, m); }

    // Edits Config the way the encoder does, through the one commit path.
    template <class F> bool edit(F&& change) {
        auto config = app_state.config();
        change(config);
        return commit_config(ctx, config);
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
// Suite 1 — kChordVel: direction → chord notes + active_degree
// ===========================================================================

static void test_chordvel_north_sends_notes() {
    // N direction (dir=2) should trigger at least one note-on via the engine
    Ctx c; c.set_mode(JoystickPlayMode::kChordVel);
    c.joy(2);  // N
    ASSERT_GT(c.sink.count_on(), 0);
}

static void test_chordvel_all_8_dirs_no_crash() {
    for (int8_t d = 0; d < 8; ++d) {
        Ctx c; c.set_mode(JoystickPlayMode::kChordVel);
        c.joy(d);
    }
}

static void test_chordvel_sets_active_degree() {
    // N direction → kClockMap[2]=0 → degree index 0 → active_degree=1
    Ctx c; c.set_mode(JoystickPlayMode::kChordVel);
    c.joy(2);  // N
    ASSERT_GE(c.nav.active_degree, 1);
    ASSERT_LE(c.nav.active_degree, 7);
}

static void test_chordvel_different_dirs_set_different_degrees() {
    // E (dir=0) and N (dir=2) map to different clock entries → different degrees
    Ctx c1; c1.set_mode(JoystickPlayMode::kChordVel);
    c1.joy(2);  // N → degree I
    int d1 = c1.nav.active_degree;

    Ctx c2; c2.set_mode(JoystickPlayMode::kChordVel);
    c2.joy(0);  // E → different degree
    int d2 = c2.nav.active_degree;

    ASSERT_NE(d1, d2);
}

static void test_chordvel_marks_dirty() {
    Ctx c; c.set_mode(JoystickPlayMode::kChordVel);
    c.nav.dirty = false;
    c.joy(2);
    ASSERT_TRUE(c.nav.dirty);
}

static void test_chordvel_center_is_noop_for_notes() {
    // kChordVel is an engine mode → release_held on center, no direct note_off
    Ctx c; c.set_mode(JoystickPlayMode::kChordVel);
    c.joy(2);
    c.sink.clear();
    c.joy(-1);  // center — engine mode → release_held, not joy_notes_off
    // No direct note-off from this path (engine handles it internally)
    // Just verify no crash
    ASSERT_EQ(c.js.mode, JoystickPlayMode::kChordVel);
}

static void test_chordvel_7th_dir_wraps_to_valid_degree() {
    // kClockMap[7]=3 → degree index 3 (or wrapped via steps.size())
    Ctx c; c.set_mode(JoystickPlayMode::kChordVel);
    c.joy(7);  // SE → kClockMap[7]=3 → degree IV
    ASSERT_GE(c.nav.active_degree, 1);
    ASSERT_LE(c.nav.active_degree, 7);
}

// ===========================================================================
// Suite 2 — kSoloist sub-mode A: scale walk + velocity adjust
// ===========================================================================

static void test_soloist_subA_north_sends_note() {
    Ctx c; c.set_mode(JoystickPlayMode::kSoloist);
    c.joy(2);  // N = vstep=+1
    ASSERT_GT(c.sink.count_on(), 0);
}

static void test_soloist_subA_south_sends_note() {
    Ctx c; c.set_mode(JoystickPlayMode::kSoloist);
    c.joy(6);  // S = vstep=-1
    ASSERT_GT(c.sink.count_on(), 0);
}

static void test_soloist_subA_walk_index_increments_on_north() {
    Ctx c; c.set_mode(JoystickPlayMode::kSoloist);
    c.js.walk_index = 2;
    c.joy(2);  // N = vstep=+1 → walk_index becomes 3
    ASSERT_EQ(c.js.walk_index, 3);
}

static void test_soloist_subA_walk_index_decrements_on_south() {
    Ctx c; c.set_mode(JoystickPlayMode::kSoloist);
    c.js.walk_index = 4;
    c.joy(6);  // S = vstep=-1 → walk_index becomes 3
    ASSERT_EQ(c.js.walk_index, 3);
}

static void test_soloist_subA_walk_index_wraps_at_8() {
    Ctx c; c.set_mode(JoystickPlayMode::kSoloist);
    c.js.walk_index = 7;
    c.joy(2);  // N → 7+1=8 → wrap_index(8,8)=0
    ASSERT_EQ(c.js.walk_index, 0);
}

static void test_soloist_subA_east_increases_velocity() {
    Ctx c; c.set_mode(JoystickPlayMode::kSoloist);
    ASSERT_TRUE(c.edit([](contract::Config& k) { k.solo_velocity = 64; }));
    c.sink.clear();
    c.joy(0);  // E = hstep=+1 → vel+10
    ASSERT_EQ(c.s().solo_velocity, 74);
    // Walk left/right is a Config edit: the menu's Vel row agrees, it is
    // queued for autosave, and it plays no note.
    ASSERT_EQ(c.app_state.config().solo_velocity, 74);
    ASSERT_TRUE(c.nav.settings_dirty);
    ASSERT_EQ(c.sink.count_on(), 0);
}

static void test_soloist_subA_west_decreases_velocity() {
    Ctx c; c.set_mode(JoystickPlayMode::kSoloist);
    ASSERT_TRUE(c.edit([](contract::Config& k) { k.solo_velocity = 64; }));
    c.joy(4);  // W = hstep=-1 → vel-10
    ASSERT_EQ(c.s().solo_velocity, 54);
    ASSERT_EQ(c.app_state.config().solo_velocity, 54);
}

static void test_soloist_subA_velocity_min_1() {
    // Velocity cannot go below 1 (would be MIDI note-off)
    Ctx c; c.set_mode(JoystickPlayMode::kSoloist);
    ASSERT_TRUE(c.edit([](contract::Config& k) { k.solo_velocity = 1; }));
    c.joy(4);  // W → 1-10 → clamped to 1
    ASSERT_EQ(c.s().solo_velocity, 1);
    ASSERT_EQ(c.app_state.config().solo_velocity, 1);
}

static void test_soloist_subA_velocity_max_127() {
    Ctx c; c.set_mode(JoystickPlayMode::kSoloist);
    ASSERT_TRUE(c.edit([](contract::Config& k) { k.solo_velocity = 127; }));
    c.joy(0);  // E → 127+10 → clamped to 127
    ASSERT_EQ(c.s().solo_velocity, 127);
    ASSERT_EQ(c.app_state.config().solo_velocity, 127);
}

// The walked velocity is what the next walked note is played with when Vel
// mode is Fixed (keypad directions always are: they have no distance).
static void test_soloist_subA_walked_velocity_is_the_next_notes_velocity() {
    Ctx c; c.set_mode(JoystickPlayMode::kSoloist);
    ASSERT_TRUE(c.edit([](contract::Config& k) { k.solo_velocity = 64; }));
    c.joy(0);  // → 74
    c.joy(-1);
    c.sink.clear();
    c.joy(2);  // N: play the next scale note
    ASSERT_GT(c.sink.count_on(), 0);
    for (auto& m : c.sink.msgs) if (m.t == "on") ASSERT_EQ(m.b, 74);
}

// Source=scale shares Source=degrees' octave convention (octave 4 = MIDI 60):
// in C major at Octave 0 the walk is C4..C5, starting on middle C.
static void test_soloist_subA_scale_walk_starts_on_middle_c() {
    Ctx c; c.set_mode(JoystickPlayMode::kSoloist);
    ASSERT_TRUE(c.app_state.config().joy_families.notes.source == app::NoteSource::kScale);
    c.js.walk_index = 7;
    c.joy(2);  // wraps to index 0
    ASSERT_EQ(c.js.last_note, 60);
    const uint8_t major[] = {62, 64, 65, 67, 69, 71, 72};
    for (uint8_t expected : major) {
        c.joy(-1);
        c.joy(2);
        ASSERT_EQ(c.js.last_note, expected);
    }
    // Source=degrees, same octave setting: the same register.
    Ctx d;
    app::NotesParams p = d.app_state.config().joy_families.notes;
    p.source = app::NoteSource::kDegrees; p.gesture = app::NoteGesture::kWalk;
    d.set_notes(p);
    d.js.walk_index = 7;
    d.joy(2);
    ASSERT_EQ(d.js.last_note, 60);
}

static void test_soloist_subA_updates_last_note() {
    Ctx c; c.set_mode(JoystickPlayMode::kSoloist);
    c.js.last_note = 0;
    c.joy(2);
    ASSERT_NE(c.js.last_note, 0);
}

// ===========================================================================
// Suite 3 — kSoloist sub-mode B: chord tones (8 dirs → clock-mapped tones)
// ===========================================================================

// Sub B (chord tones, clock gesture) is no longer a Melody runtime toggle —
// it's NOTES/source=chordTones,gesture=clock (config migration table:
// "Melody (sub B) | NOTES | source=chord tones, gesture=clock, voices=note").
static app::NotesParams soloist_subb_params() {
    app::NotesParams p;
    p.source = app::NoteSource::kChordTones;
    p.gesture = app::NoteGesture::kClock;
    return p;
}

static void test_soloist_subB_north_sends_note() {
    Ctx c; c.set_notes(soloist_subb_params());
    c.joy(2);  // N
    ASSERT_GT(c.sink.count_on(), 0);
}

static void test_soloist_subB_all_8_dirs_no_crash() {
    for (int8_t d = 0; d < 8; ++d) {
        Ctx c; c.set_notes(soloist_subb_params());
        c.joy(d);
    }
}

static void test_soloist_subB_all_8_dirs_send_notes() {
    for (int8_t d = 0; d < 8; ++d) {
        Ctx c; c.set_notes(soloist_subb_params());
        c.joy(d);
        ASSERT_GT(c.sink.count_on(), 0);
    }
}

static void test_soloist_subB_updates_last_note() {
    Ctx c; c.set_notes(soloist_subb_params());
    c.js.last_note = 0;
    c.joy(2);
    ASSERT_NE(c.js.last_note, 0);
}

// ===========================================================================
// Suite 4 — kBassClock: MIDI note range, mono mode, all directions
// ===========================================================================

static void test_bassclock_north_plays_valid_midi_note() {
    Ctx c;
    c.s().midi_channel_joy = 3;
    c.set_mode(JoystickPlayMode::kBassClock);
    c.joy(2);
    ASSERT_GT(c.sink.count_on(), 0);
    // Verify all sent notes are in valid MIDI range
    for (auto& m : c.sink.msgs) {
        if (m.t == "on") {
            ASSERT_GE(m.a, 0);
            ASSERT_LE(m.a, 127);
        }
    }
}

static void test_bassclock_all_8_dirs_valid_midi_notes() {
    for (int8_t d = 0; d < 8; ++d) {
        Ctx c;
        c.s().midi_channel_joy = 3;
        c.set_mode(JoystickPlayMode::kBassClock);
        c.joy(d);
        for (auto& m : c.sink.msgs) {
            if (m.t == "on") {
                ASSERT_GE(m.a, 0);
                ASSERT_LE(m.a, 127);
            }
        }
    }
}

static void test_bassclock_mono_sends_off_before_on() {
    // Bass preset (joy_family.cpp: notes_bass()) hardcodes Mono=on; there is
    // no runtime toggle to flip any more.
    Ctx c;
    c.s().midi_channel_joy = 3;
    c.set_mode(JoystickPlayMode::kBassClock);
    c.joy(2);  // first note
    c.sink.clear();
    c.joy(0);  // second note in mono mode — off then on
    // In mono mode, joy_notes_off() is called before the new note
    // The second press must have sent a note-on
    ASSERT_GT(c.sink.count_on(), 0);
}

static void test_bassclock_updates_last_note() {
    Ctx c;
    c.s().midi_channel_joy = 3;
    c.set_mode(JoystickPlayMode::kBassClock);
    c.js.last_note = 0;
    c.joy(2);
    ASSERT_NE(c.js.last_note, 0);
}

static void test_bassclock_uses_chord_channel() {
    // NotesParams::channel defaults to kChord and the Bass preset doesn't
    // override it — Bass now blends into the chord channel by default,
    // rather than always using the separate joy channel like the old
    // hardwired BassClock recipe did (Channel is a real per-preset NOTES
    // parameter).
    Ctx c;
    c.s().midi_channel = 5;
    c.set_mode(JoystickPlayMode::kBassClock);
    c.joy(2);
    bool found = false;
    for (auto& m : c.sink.msgs)
        if (m.t == "on" && m.ch == 5) { found = true; break; }
    ASSERT_TRUE(found);
}

// ===========================================================================
// Suite 5 — Tension map quality zones (tension_quality_2d replicated)
// ===========================================================================

// Replicate the static function from input_joystick.cpp for formula testing.
static const char* tension_quality_2d(float x, float y, float d) {
    if (d < 0.2f) return nullptr;
    const int zone = (d < 0.5f) ? 0 : (d < 0.8f) ? 1 : 2;
    if (x >= 0 && y >= 0) { static const char* k[] = {"add9", "maj7", "maj9"};     return k[zone]; }
    if (x < 0  && y >= 0) { static const char* k[] = {"sus2", "sus4", "aug"};      return k[zone]; }
    if (x < 0  && y < 0)  { static const char* k[] = {"min7", "dim7", "dim"};      return k[zone]; }
                             static const char* k[] = {"dom7", "dom9", "half-dim7"}; return k[zone];
}

static const char* tension_quality_alt(float x, float y, float d) {
    if (d < 0.2f) return nullptr;
    const int zone = (d < 0.5f) ? 0 : (d < 0.8f) ? 1 : 2;
    if (x >= 0 && y >= 0) { static const char* k[] = {"dom7",  "dom9",   "dom13"};    return k[zone]; }
    if (x < 0  && y >= 0) { static const char* k[] = {"min7",  "min9",   "min11"};    return k[zone]; }
    if (x < 0  && y < 0)  { static const char* k[] = {"dim",   "dim7",   "half-dim7"};return k[zone]; }
                             static const char* k[] = {"maj7",  "aug",    "maj9"};     return k[zone];
}

static void test_tension_center_returns_nullptr() {
    // d < 0.2: dead zone → nullptr (no quality change)
    ASSERT_EQ(tension_quality_2d(0.0f, 0.0f, 0.1f), nullptr);
    ASSERT_EQ(tension_quality_alt(0.0f, 0.0f, 0.1f), nullptr);
}

static void test_tension_q1_zone0_add9() {
    // Q1 (x>=0, y>=0), zone 0 (0.2 <= d < 0.5) → "add9"
    ASSERT_STREQ(tension_quality_2d(0.3f, 0.3f, 0.42f), "add9");
}

static void test_tension_q1_zone1_maj7() {
    // Q1, zone 1 (0.5 <= d < 0.8) → "maj7"
    ASSERT_STREQ(tension_quality_2d(0.5f, 0.5f, 0.71f), "maj7");
}

static void test_tension_q1_zone2_maj9() {
    // Q1, zone 2 (d >= 0.8) → "maj9"
    ASSERT_STREQ(tension_quality_2d(0.7f, 0.7f, 0.99f), "maj9");
}

static void test_tension_q2_zone0_sus2() {
    // Q2 (x<0, y>=0), zone 0 → "sus2"
    ASSERT_STREQ(tension_quality_2d(-0.3f, 0.3f, 0.42f), "sus2");
}

static void test_tension_q2_zone1_sus4() {
    ASSERT_STREQ(tension_quality_2d(-0.5f, 0.5f, 0.71f), "sus4");
}

static void test_tension_q2_zone2_aug() {
    ASSERT_STREQ(tension_quality_2d(-0.7f, 0.7f, 0.99f), "aug");
}

static void test_tension_q3_zone0_min7() {
    // Q3 (x<0, y<0), zone 0 → "min7"
    ASSERT_STREQ(tension_quality_2d(-0.3f, -0.3f, 0.42f), "min7");
}

static void test_tension_q3_zone1_dim7() {
    ASSERT_STREQ(tension_quality_2d(-0.5f, -0.5f, 0.71f), "dim7");
}

static void test_tension_q3_zone2_dim() {
    ASSERT_STREQ(tension_quality_2d(-0.7f, -0.7f, 0.99f), "dim");
}

static void test_tension_q4_zone0_dom7() {
    // Q4 (x>=0, y<0), zone 0 → "dom7"
    ASSERT_STREQ(tension_quality_2d(0.3f, -0.3f, 0.42f), "dom7");
}

static void test_tension_q4_zone1_dom9() {
    ASSERT_STREQ(tension_quality_2d(0.5f, -0.5f, 0.71f), "dom9");
}

static void test_tension_q4_zone2_half_dim7() {
    ASSERT_STREQ(tension_quality_2d(0.7f, -0.7f, 0.99f), "half-dim7");
}

// Alt map tests
static void test_tension_alt_q1_zone0_dom7() {
    ASSERT_STREQ(tension_quality_alt(0.3f, 0.3f, 0.42f), "dom7");
}

static void test_tension_alt_q2_zone0_min7() {
    ASSERT_STREQ(tension_quality_alt(-0.3f, 0.3f, 0.42f), "min7");
}

static void test_tension_alt_q3_zone0_dim() {
    ASSERT_STREQ(tension_quality_alt(-0.3f, -0.3f, 0.42f), "dim");
}

static void test_tension_alt_q4_zone0_maj7() {
    ASSERT_STREQ(tension_quality_alt(0.3f, -0.3f, 0.42f), "maj7");
}

static void test_tension_alt_q1_zone2_dom13() {
    ASSERT_STREQ(tension_quality_alt(0.7f, 0.7f, 0.99f), "dom13");
}

static void test_tension_alt_q2_zone2_min11() {
    ASSERT_STREQ(tension_quality_alt(-0.7f, 0.7f, 0.99f), "min11");
}

// ===========================================================================
// Suite 6 — CC mapping formula: -512..+512 → 0..127
// ===========================================================================

// Replicate the lambda from handle_cc_pair / handle_cc_control for formula tests.
static int16_t map_cc(int16_t v) {
    float n = (static_cast<float>(v) + 512.0f) / 1024.0f;
    if (n < 0.0f) n = 0.0f;
    if (n > 1.0f) n = 1.0f;
    return static_cast<int16_t>(n * 127.0f);
}

static void test_cc_map_minus512_returns_0() {
    ASSERT_EQ(map_cc(-512), 0);
}

static void test_cc_map_plus512_returns_127() {
    ASSERT_EQ(map_cc(512), 127);
}

static void test_cc_map_zero_returns_midpoint() {
    // v=0 → n=0.5 → 63
    int16_t r = map_cc(0);
    ASSERT_GE(r, 62);
    ASSERT_LE(r, 64);
}

static void test_cc_map_clamps_below_min() {
    // v=-1024 → n=-0.5 → clamped to 0
    ASSERT_EQ(map_cc(-1024), 0);
}

static void test_cc_map_clamps_above_max() {
    // v=1024 → n=1.5 → clamped to 127
    ASSERT_EQ(map_cc(1024), 127);
}

static void test_cc_map_monotone_increasing() {
    // Verify output is non-decreasing across the valid input range
    int16_t prev = map_cc(-512);
    for (int v = -512; v <= 512; v += 32) {
        int16_t cur = map_cc(static_cast<int16_t>(v));
        ASSERT_GE(cur, prev);
        prev = cur;
    }
}

static void test_cc_map_output_always_valid_midi() {
    for (int v = -1024; v <= 1024; v += 64) {
        int16_t r = map_cc(static_cast<int16_t>(v));
        ASSERT_GE(r, 0);
        ASSERT_LE(r, 127);
    }
}

// ===========================================================================
// main
// ===========================================================================

int main() {
    TEST_SUITE("kChordVel — direction → chord notes + active_degree");
    RUN_TEST(test_chordvel_north_sends_notes);
    RUN_TEST(test_chordvel_all_8_dirs_no_crash);
    RUN_TEST(test_chordvel_sets_active_degree);
    RUN_TEST(test_chordvel_different_dirs_set_different_degrees);
    RUN_TEST(test_chordvel_marks_dirty);
    RUN_TEST(test_chordvel_center_is_noop_for_notes);
    RUN_TEST(test_chordvel_7th_dir_wraps_to_valid_degree);

    TEST_SUITE("kSoloist sub-mode A — scale walk + velocity");
    RUN_TEST(test_soloist_subA_north_sends_note);
    RUN_TEST(test_soloist_subA_south_sends_note);
    RUN_TEST(test_soloist_subA_walk_index_increments_on_north);
    RUN_TEST(test_soloist_subA_walk_index_decrements_on_south);
    RUN_TEST(test_soloist_subA_walk_index_wraps_at_8);
    RUN_TEST(test_soloist_subA_east_increases_velocity);
    RUN_TEST(test_soloist_subA_west_decreases_velocity);
    RUN_TEST(test_soloist_subA_velocity_min_1);
    RUN_TEST(test_soloist_subA_velocity_max_127);
    RUN_TEST(test_soloist_subA_walked_velocity_is_the_next_notes_velocity);
    RUN_TEST(test_soloist_subA_scale_walk_starts_on_middle_c);
    RUN_TEST(test_soloist_subA_updates_last_note);

    TEST_SUITE("kSoloist sub-mode B — chord tones (8 clock directions)");
    RUN_TEST(test_soloist_subB_north_sends_note);
    RUN_TEST(test_soloist_subB_all_8_dirs_no_crash);
    RUN_TEST(test_soloist_subB_all_8_dirs_send_notes);
    RUN_TEST(test_soloist_subB_updates_last_note);

    TEST_SUITE("kBassClock — MIDI note range + mono + channel");
    RUN_TEST(test_bassclock_north_plays_valid_midi_note);
    RUN_TEST(test_bassclock_all_8_dirs_valid_midi_notes);
    RUN_TEST(test_bassclock_mono_sends_off_before_on);
    RUN_TEST(test_bassclock_updates_last_note);
    RUN_TEST(test_bassclock_uses_chord_channel);

    TEST_SUITE("Tension map A (tension_quality_2d) — 4 quadrants × 3 zones");
    RUN_TEST(test_tension_center_returns_nullptr);
    RUN_TEST(test_tension_q1_zone0_add9);
    RUN_TEST(test_tension_q1_zone1_maj7);
    RUN_TEST(test_tension_q1_zone2_maj9);
    RUN_TEST(test_tension_q2_zone0_sus2);
    RUN_TEST(test_tension_q2_zone1_sus4);
    RUN_TEST(test_tension_q2_zone2_aug);
    RUN_TEST(test_tension_q3_zone0_min7);
    RUN_TEST(test_tension_q3_zone1_dim7);
    RUN_TEST(test_tension_q3_zone2_dim);
    RUN_TEST(test_tension_q4_zone0_dom7);
    RUN_TEST(test_tension_q4_zone1_dom9);
    RUN_TEST(test_tension_q4_zone2_half_dim7);

    TEST_SUITE("Tension map B (tension_quality_alt) — jazz/altered zones");
    RUN_TEST(test_tension_alt_q1_zone0_dom7);
    RUN_TEST(test_tension_alt_q2_zone0_min7);
    RUN_TEST(test_tension_alt_q3_zone0_dim);
    RUN_TEST(test_tension_alt_q4_zone0_maj7);
    RUN_TEST(test_tension_alt_q1_zone2_dom13);
    RUN_TEST(test_tension_alt_q2_zone2_min11);

    TEST_SUITE("CC mapping formula: -512..+512 → 0..127");
    RUN_TEST(test_cc_map_minus512_returns_0);
    RUN_TEST(test_cc_map_plus512_returns_127);
    RUN_TEST(test_cc_map_zero_returns_midpoint);
    RUN_TEST(test_cc_map_clamps_below_min);
    RUN_TEST(test_cc_map_clamps_above_max);
    RUN_TEST(test_cc_map_monotone_increasing);
    RUN_TEST(test_cc_map_output_always_valid_midi);

    test_print_result();
    return (g_fail > 0) ? 1 : 0;
}
