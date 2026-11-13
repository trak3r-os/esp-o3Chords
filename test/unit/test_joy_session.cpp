// Tests: Joystick session simulation — mode transitions and real user workflows.
//
// Two test families:
//
//   1. EXHAUSTIVE TRANSITIONS (17×17 = 289 pairs)
//      Every source mode → every destination mode, with pre-activated "dirty"
//      state (notes playing, drones active, harmony on, pattern running).
//      After each transition: verify no stuck notes, no pitch bend, masks
//      cleared, euclidean stopped — apply_joystick_mode's contract holds.
//
//   2. REAL USER SESSIONS (6 scenario tests)
//      Narrative scripts that mirror what a musician actually does:
//      chord layer → drone layer → harmony melody → euclidean rhythm →
//      scale explore → back to quality.  Verify musical state is coherent
//      at every step and nothing is left hanging at session end.

#include "../framework.h"

#include "app/production_ui.h"
#include "app/config_codec.h"
#include "app/input_context.h"
#include "app/production_ui.h"
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
#include <map>
#include <string>
#include <utility>
#include <vector>

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
    bool has_on()    const { return count_on() > 0; }

    // Returns number of note_on events with no matching note_off (per ch+note).
    int unmatched_note_ons() const {
        std::map<std::pair<uint8_t,uint8_t>, int> bal;
        for (auto& m : msgs) {
            auto key = std::make_pair(m.ch, m.a);
            if      (m.t == "on")  bal[key]++;
            else if (m.t == "off") bal[key]--;
        }
        int total = 0;
        for (auto& p : bal) if (p.second > 0) total += p.second;
        return total;
    }

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
    app::prod::UiState ui{};
    struct LedgerSink : engine::NoteLedgerSink {
        midi::Router& r; explicit LedgerSink(midi::Router& x) : r(x) {}
        void note_on(uint8_t route, uint8_t ch, uint8_t n, uint8_t v) override { r.route_note_on(static_cast<uint8_t>(1u << route), ch, n, v); }
        void note_off(uint8_t route, uint8_t ch, uint8_t n) override { r.route_note_off(static_cast<uint8_t>(1u << route), ch, n, 0); }
    };
    LedgerSink ledger_sink{router};
    engine::NoteLedger ledger{&ledger_sink};
    InputContext     ctx{app_state, engine, router, ble, usb,
                         js, nav, play_prog, 0, &ledger};
    CapSink          sink;

    Ctx() {
        app_state.sanitize();
        engine.begin();
        router.register_sink(&sink);
        engine.set_router(&router);
        app_state.settings().voice_leading   = 0;
        app_state.settings().humanize_ms     = 0;
        app_state.settings().midi_channel    = 0;
        app_state.settings().midi_channel_joy = 3;
        // Config is the authority: a committed edit projects it back onto
        // Settings, so the fixture's choices must live in Config too.
        app_state.set_config(app::config::from_settings(app_state.settings()));
        apply_settings(app_state, engine, ble, usb);
        apply_progression_mode(engine, ProgressionMode::kMajor, app_state.settings());
        prod::begin(ui, ctx);
        js.last_dir = -1;
    }

    Settings& s() { return app_state.settings(); }

    // Dispatch direction, always with dc=true (dispatch_keypad_joy_direction's
    // dc reads keypad_dir, not last_dir — see test_joystick_modes.cpp's Ctx::joy).
    void joy(int8_t dir) {
        js.keypad_dir = static_cast<int8_t>(dir - 1);
        dispatch_keypad_joy_direction(ctx, dir);
    }

    void set_mode(JoystickPlayMode m) { apply_joystick_mode(ctx, m); }
    // The old Harmony preset as config migration rebuilds it: Drone plus the
    // legacy override (mono latched single notes on the lead channel) over
    // Source=intervals. PresetId::kHarmony itself no longer exists.
    void set_harmony() {
        set_mode(JoystickPlayMode::kHarmony);
        auto config = app_state.config();
        config.joy_families.notes.source = app::NoteSource::kIntervals;
        app::apply_legacy_harmony_override(config.joy_families.notes);
        app_state.set_config(config);
    }
    void fire() { esp_timer_fire_pending(); }

    // Press a chord degree key
    void press(uint8_t idx) {
        std::vector<drivers::KeypadEvent> evs{{idx, true}};
        app::prod::process_keypad_events(ctx, ui, evs);
    }
    void release(uint8_t idx) {
        std::vector<drivers::KeypadEvent> evs{{idx, false}};
        app::prod::process_keypad_events(ctx, ui, evs);
    }
    void tap(uint8_t idx) { press(idx); fire(); release(idx); fire(); }
};

// Chord degree keypad indices (row-major 4×4)
static constexpr uint8_t kDeg[7] = {0, 1, 2, 4, 5, 6, 8};

// ---------------------------------------------------------------------------
// Mode state activation helpers
// ---------------------------------------------------------------------------

// Activate "dirty state" for each mode — whatever makes the mode's cleanup
// logic non-trivial so the transition test is meaningful.
static void activate_dirty_state(Ctx& c, JoystickPlayMode mode) {
    c.set_mode(mode);
    switch (mode) {
    case JoystickPlayMode::kDrone:
        // 3 drones active in 3 directions
        c.joy(0); c.joy(2); c.joy(4);
        break;
    case JoystickPlayMode::kHarmony:
        c.joy(2);   // one harmony note
        break;
    case JoystickPlayMode::kBassClock:
        c.joy(0);   // one bass note
        break;
    case JoystickPlayMode::kSoloist:
        c.joy(2);   // scale walk
        break;
    case JoystickPlayMode::kInterval:
        c.joy(2);
        break;
    case JoystickPlayMode::kPentatonic:
        c.joy(2);
        break;
    case JoystickPlayMode::kArpControl:
        c.joy(0); c.joy(2);   // change rate and pattern
        break;
    case JoystickPlayMode::kChordVel:
        c.nav.active_degree = 1;
        c.joy(2);
        break;
    case JoystickPlayMode::kBend:
        // Analog-only; fake a bend value
        c.js.last_bend = 1000;
        c.js.bend_vibrato_depth = 50;
        break;
    default:
        // Quality, Discover, Rhythm, CCControl, Voicing, StrumFeel,
        // ScaleExplore, Euclidean: analog-only, or removed.
        // No dirty state achievable
        // via keypad; just set the mode.
        break;
    }
}

// Invariant: after apply_joystick_mode(dst), these must all hold. Harmony now
// shares the drone_active_mask latch array (both are
// NOTES/Sustain=latch), so one check covers both; Euclidean is retired.
static bool joystick_state_is_clean(const Ctx& c) {
    bool ok = true;
    ok &= (c.js.drone_active_mask == 0);  // no stuck drone or harmony latch
    ok &= (c.js.last_bend == 0);          // pitch bend reset
    ok &= (c.js.note_active_count == 0);  // direct-MIDI notes cleared
    return ok;
}

// ===========================================================================
// Suite 1: Exhaustive mode-to-mode transitions (all 17×17 = 289 pairs)
// ===========================================================================

static void test_all_mode_transitions_clean_state() {
    // For every (src, dst) pair:
    //   1. activate dirty state in src
    //   2. transition to dst
    //   3. fire pending timers
    //   4. assert clean joystick state
    //   5. assert no unmatched note-ons remain

    constexpr uint8_t kCount = static_cast<uint8_t>(JoystickPlayMode::kCOUNT);
    int failures = 0;

    for (uint8_t s = 0; s < kCount; ++s) {
        for (uint8_t d = 0; d < kCount; ++d) {
            Ctx c;
            auto src = static_cast<JoystickPlayMode>(s);
            auto dst = static_cast<JoystickPlayMode>(d);

            activate_dirty_state(c, src);
            c.fire();

            c.sink.clear();
            c.set_mode(dst);
            c.fire();

            if (!joystick_state_is_clean(c)) {
                ++failures;
            }
            // After firing, unmatched note-ons should be 0
            // (engine all_notes_off + joy_notes_off cover everything)
            if (c.sink.unmatched_note_ons() != 0) {
                ++failures;
            }
        }
    }
    ASSERT_EQ(failures, 0);
}

// Test a meaningful subset explicitly for clearer failure messages.

static void test_drone_to_every_mode_kills_drones() {
    constexpr uint8_t kCount = static_cast<uint8_t>(JoystickPlayMode::kCOUNT);
    for (uint8_t d = 0; d < kCount; ++d) {
        Ctx c;
        c.set_mode(JoystickPlayMode::kDrone);
        c.joy(0); c.joy(3); c.joy(6);  // 3 drones
        ASSERT_NE(c.js.drone_active_mask, 0);
        c.set_mode(static_cast<JoystickPlayMode>(d));
        ASSERT_EQ(c.js.drone_active_mask, 0);
    }
}

static void test_harmony_to_every_mode_kills_harmony() {
    // Harmony is now NOTES/Sustain=latch, sharing drone_active_mask.
    constexpr uint8_t kCount = static_cast<uint8_t>(JoystickPlayMode::kCOUNT);
    for (uint8_t d = 0; d < kCount; ++d) {
        Ctx c;
        c.set_mode(JoystickPlayMode::kHarmony);
        c.joy(2);
        ASSERT_NE(c.js.drone_active_mask, 0);
        c.set_mode(static_cast<JoystickPlayMode>(d));
        ASSERT_EQ(c.js.drone_active_mask, 0);
    }
}

// Euclidean was removed; no replacement
// test — there is no clock-driven joystick producer left to stop.

static void test_bend_to_every_mode_resets_pitch_bend() {
    constexpr uint8_t kCount = static_cast<uint8_t>(JoystickPlayMode::kCOUNT);
    for (uint8_t d = 0; d < kCount; ++d) {
        Ctx c;
        c.set_mode(JoystickPlayMode::kBend);
        c.js.last_bend = 4096;
        c.set_mode(static_cast<JoystickPlayMode>(d));
        ASSERT_EQ(c.js.last_bend, 0);
    }
}

static void test_bassclock_to_every_mode_stops_notes() {
    constexpr uint8_t kCount = static_cast<uint8_t>(JoystickPlayMode::kCOUNT);
    for (uint8_t d = 0; d < kCount; ++d) {
        Ctx c;
        c.set_mode(JoystickPlayMode::kBassClock);
        c.joy(0);
        ASSERT_GT(c.js.note_active_count, 0);
        c.set_mode(static_cast<JoystickPlayMode>(d));
        ASSERT_EQ(c.js.note_active_count, 0);
    }
}

// Rapid cycling through all modes in sequence — stress test.
static void test_rapid_mode_cycle_all_17_no_crash() {
    Ctx c;
    constexpr uint8_t kCount = static_cast<uint8_t>(JoystickPlayMode::kCOUNT);
    for (uint8_t i = 0; i < kCount; ++i) {
        auto mode = static_cast<JoystickPlayMode>(i);
        activate_dirty_state(c, mode);
        c.fire();
    }
    // After cycling through all, state must be clean
    // (last mode set was kCOUNT-1; the set_mode call in activate_dirty_state
    //  always leaves state clean from the previous mode)
    ASSERT_TRUE(joystick_state_is_clean(c));
}

// ===========================================================================
// Suite 2: Real user sessions
// ===========================================================================

// Session A: Chord layer → Drone layer → back to Quality
// Scenario: musician plays chords with Quality mode, then activates 3 drone
// pedal notes, plays more chords on top, then stops everything cleanly.
static void test_session_chord_then_drone_layer() {
    Ctx c;

    // 1. Start in Quality mode (default), play a chord I
    ASSERT_EQ(c.js.mode, JoystickPlayMode::kQuality);
    c.tap(kDeg[0]);  // chord I
    ASSERT_TRUE(c.sink.has_on());
    c.sink.clear();

    // 2. Switch to Drone — activate 3 pedal notes
    c.set_mode(JoystickPlayMode::kDrone);
    c.joy(0); c.joy(2); c.joy(5);  // E, N, SW drones
    ASSERT_EQ(c.js.drone_active_mask, (1u<<0)|(1u<<2)|(1u<<5));
    ASSERT_GT(c.sink.count_on(), 0);

    // 3. While drones ring, play more chords via engine (kChordVel on keypad)
    c.tap(kDeg[2]);  // chord III over drone
    ASSERT_TRUE(c.sink.has_on());
    c.sink.clear();

    // 4. Switch back to Quality — drones must stop
    c.set_mode(JoystickPlayMode::kQuality);
    c.fire();
    ASSERT_EQ(c.js.drone_active_mask, 0);
    ASSERT_GT(c.sink.count_off(), 0);  // drone note-offs sent
}

// Session B: BassClock melody → ArpControl tune → back to BassClock
// Scenario: play bass lines, switch to ArpControl to change the arp pattern
// while notes are "remembered", switch back and continue.
static void test_session_bassclock_arpcontrol_bassclock() {
    Ctx c;

    // 1. BassClock: play bass notes in several directions
    c.set_mode(JoystickPlayMode::kBassClock);
    c.joy(0); c.joy(2);  // E and N scale notes
    int bass_ons = c.sink.count_on();
    ASSERT_GT(bass_ons, 0);

    // 2. Switch to ArpControl — bass notes cleared
    c.sink.clear();
    c.set_mode(JoystickPlayMode::kArpControl);
    ASSERT_EQ(c.js.note_active_count, 0);
    c.joy(-1);  // new recipe requires a neutral sample before a new gesture

    // 3. The live arp tweaker has no preset left (PARAM/Flip is disabled):
    //    the mode lands on CONT/Macro, where a keypad direction is inert.
    ASSERT_TRUE(c.app_state.config().active_preset == app::PresetId::kMacro);
    uint8_t before_rate = c.s().arp_rate;
    c.joy(0);
    ASSERT_EQ(c.s().arp_rate, before_rate);
    ASSERT_EQ(c.sink.count_on(), 0);

    // 4. Back to BassClock — clean start
    c.sink.clear();
    c.set_mode(JoystickPlayMode::kBassClock);
    c.joy(-1);
    c.joy(4);  // W
    ASSERT_GT(c.sink.count_on(), 0);
    ASSERT_EQ(c.sink.unmatched_note_ons(), c.js.note_active_count);  // tracked correctly

    // 5. Cleanup
    c.set_mode(JoystickPlayMode::kQuality);
    ASSERT_EQ(c.js.note_active_count, 0);
}

// Session C: Harmony melody → mode change → harmony note gone
// Scenario: musician plays a single harmony interval above the chord,
// then accidentally (or intentionally) switches mode — note must stop.
static void test_session_harmony_melody_then_mode_change() {
    Ctx c;

    // 1. Play chord I
    c.tap(kDeg[0]);
    c.sink.clear();

    // 2. Add harmony above chord
    c.set_harmony();
    c.joy(1);   // NE = perfect 5th above root
    ASSERT_NE(c.js.drone_active_mask, 0);
    uint8_t harmony = c.js.drone_notes[1];
    ASSERT_GT(c.sink.count_on(), 0);

    // 3. Move to a new harmony note — Mono=on un-latches the old direction
    c.sink.clear();
    c.joy(3);   // NW = a different interval
    ASSERT_TRUE(c.js.drone_active_mask & (1u << 3));
    ASSERT_FALSE(c.js.drone_active_mask & (1u << 1));
    // The previous note (harmony) must have been stopped
    bool old_stopped = false;
    for (auto& m : c.sink.msgs)
        if (m.t == "off" && m.a == harmony) { old_stopped = true; break; }
    ASSERT_TRUE(old_stopped);

    // 4. Switch to CHORD/Colors mid-phrase — harmony must vanish
    c.sink.clear();
    uint8_t h2 = c.js.drone_notes[3];
    c.set_mode(JoystickPlayMode::kQuality);
    ASSERT_EQ(c.js.drone_active_mask, 0);
    bool h2_stopped = false;
    for (auto& m : c.sink.msgs)
        if (m.t == "off" && m.a == h2) { h2_stopped = true; break; }
    ASSERT_TRUE(h2_stopped);
}

// Session D (previously: Euclidean -> ScaleExplore -> resume) is retired
// with both features.

// Session E: Full performance loop through 5 families
// Quality (CHORD) → ChordVel (NOTES) → Drone (NOTES/latch) → Harmony
// (NOTES/latch) → ArpControl (PARAM) → back to Quality.
// Verifies the entire chain leaves no MIDI garbage. (The old 6th stage,
// ScaleExplore, was removed with that mode.)
static void test_session_full_performance_loop() {
    Ctx c;
    c.nav.active_degree = 1;
    c.s().midi_channel_joy = 3;

    // --- Stage 1: Quality — pick quality via 8-dir ---
    ASSERT_EQ(c.js.mode, JoystickPlayMode::kQuality);
    // Quality is modified via the quality map (no direct MIDI from keypad dispatch)

    // --- Stage 2: ChordVel --- play several degrees ---
    c.set_mode(JoystickPlayMode::kChordVel);
    c.joy(0); c.joy(2); c.joy(4);
    ASSERT_TRUE(c.sink.has_on());
    c.sink.clear();

    // --- Stage 3: Drone — add pedal bass ---
    c.set_mode(JoystickPlayMode::kDrone);
    c.joy(-1);
    c.joy(0); c.joy(6);   // E and S drones
    ASSERT_EQ(c.js.drone_active_mask, (1u<<0)|(1u<<6));

    // --- Stage 4: Harmony — melodic interval ---
    c.set_mode(JoystickPlayMode::kHarmony);
    ASSERT_EQ(c.js.drone_active_mask, 0);   // drones cleared on mode switch
    c.joy(-1);
    c.joy(2);
    ASSERT_NE(c.js.drone_active_mask, 0);

    // --- Stage 5: ArpControl — tune arp live ---
    c.set_mode(JoystickPlayMode::kArpControl);
    ASSERT_EQ(c.js.drone_active_mask, 0);   // harmony latch cleared
    c.joy(-1);
    c.joy(1);   // inert under CONT/Macro (keypad directions carry no position)
    c.joy(3);

    // --- Return to Quality — full cleanup ---
    c.sink.clear();
    c.set_mode(JoystickPlayMode::kQuality);
    c.fire();

    ASSERT_TRUE(joystick_state_is_clean(c));
    // All notes should be balanced
    ASSERT_EQ(c.sink.unmatched_note_ons(), 0);
}

// Session F: Rapid user mash — stress test of interleaved actions
// Simulates nervous fast mode switching while playing.


// Session G: Joy mode switch sends all_notes_off (by design)
// apply_joystick_mode calls engine.all_notes_off() to guarantee a clean slate.
// This test documents that behavior: switching joy mode IS expected to stop
// engine notes — the chord channel is included in the cleanup.
static void test_session_chord_notes_survive_joy_mode_change() {
    Ctx c;
    c.s().midi_channel      = 0;
    c.s().midi_channel_joy  = 3;
    c.s().play_trigger = PlayTrigger::kHold;
    apply_settings(c.app_state, c.engine, c.ble, c.usb);

    // Hold chord I — notes start on ch=0
    c.press(kDeg[0]);
    c.fire();
    ASSERT_GT(c.sink.count_on(), 0);

    // Switch joystick mode — apply_joystick_mode calls engine.all_notes_off()
    // which sends note-offs for all active engine notes including ch=0.
    c.sink.clear();
    c.set_mode(JoystickPlayMode::kDrone);
    c.fire();

    // By design: engine notes are cleared. Verify the cleanup happened.
    bool all_notes_off_sent = false;
    for (auto& m : c.sink.msgs)
        if (m.t == "off" && m.ch == 0) { all_notes_off_sent = true; break; }
    ASSERT_TRUE(all_notes_off_sent);  // design contract: joy mode switch cleans engine

    // Joy channel is also clean
    ASSERT_EQ(c.js.note_active_count, 0);
    ASSERT_EQ(c.js.drone_active_mask, 0);

    // Release the chord key cleanly
    c.release(kDeg[0]);
    c.fire();
}

// Session H: Multi-drone + harmony + a live param tweak, then full panic
// (three sources active in turn, one global cleanup via set_mode). Euclidean,
// the original third source, was removed;
// ArpControl (PARAM family) takes its slot in the chain instead.
static void test_session_multi_source_cleanup() {
    Ctx c;
    c.s().midi_channel_joy = 3;
    c.nav.active_degree = 1;

    // 1. Activate drones
    c.set_mode(JoystickPlayMode::kDrone);
    c.joy(0); c.joy(2); c.joy(4); c.joy(6);
    ASSERT_EQ(c.js.drone_active_mask, (1u<<0)|(1u<<2)|(1u<<4)|(1u<<6));

    // 2. Switch to Harmony — drone clears, harmony starts
    c.set_mode(JoystickPlayMode::kHarmony);
    ASSERT_EQ(c.js.drone_active_mask, 0);
    c.joy(-1);
    c.joy(0); c.joy(2);   // play two harmony notes in sequence
    ASSERT_NE(c.js.drone_active_mask, 0);

    // 3. Switch to ArpControl (now CONT/Macro) — harmony latch clears
    c.set_mode(JoystickPlayMode::kArpControl);
    ASSERT_EQ(c.js.drone_active_mask, 0);
    c.joy(-1);
    uint8_t before_pattern = c.s().arp_pattern;
    c.joy(2);   // inert: ArpControl is CONT/Macro now, not a PARAM tweaker
    ASSERT_EQ(c.s().arp_pattern, before_pattern);

    // 4. Final cleanup via Quality
    c.sink.clear();
    c.set_mode(JoystickPlayMode::kQuality);
    c.fire();
    ASSERT_TRUE(joystick_state_is_clean(c));
    ASSERT_EQ(c.sink.unmatched_note_ons(), 0);
}

// ===========================================================================
// main
// ===========================================================================

int main() {
    TEST_SUITE("Exhaustive mode transitions (17×17)");
    RUN_TEST(test_all_mode_transitions_clean_state);
    RUN_TEST(test_drone_to_every_mode_kills_drones);
    RUN_TEST(test_harmony_to_every_mode_kills_harmony);
    RUN_TEST(test_bend_to_every_mode_resets_pitch_bend);
    RUN_TEST(test_bassclock_to_every_mode_stops_notes);
    RUN_TEST(test_rapid_mode_cycle_all_17_no_crash);

    TEST_SUITE("Real user sessions");
    RUN_TEST(test_session_chord_then_drone_layer);
    RUN_TEST(test_session_bassclock_arpcontrol_bassclock);
    RUN_TEST(test_session_harmony_melody_then_mode_change);
    RUN_TEST(test_session_full_performance_loop);

    RUN_TEST(test_session_chord_notes_survive_joy_mode_change);
    RUN_TEST(test_session_multi_source_cleanup);

    test_print_result();
    return (g_fail > 0) ? 1 : 0;
}
