// Tests: MidiClock external and auto sync modes.
//
// Key behavioral invariants of the clock implementation:
//   - feed_byte() stamps ticks with esp_timer_get_time() (= g_test_time_us in tests)
//   - update() consumes pending_ticks atomically; BPM calc needs last_tick_processed_us > 0
//   - First update() initializes last_update_us; first-ever tick batch only sets the baseline
//   - BPM is computed on the SECOND tick batch (once baseline is established)
//   - Sync requires interval in [kMinIntervalUs=5ms, kMaxIntervalUs=125ms]
//   - Timeout: 2s without ticks → synced=false, falls back to internal
//
// Test helpers simulate real firmware by advancing g_test_time_us before each
// feed_byte, then calling update() after each batch — matching the ~10ms main-loop cadence.

#include "../framework.h"
#include "engine/midi_clock.h"
#include "../stubs/esp_timer.h"

using namespace engine;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Advance time by interval_us, feed one tick, then call update().
// Returns the tick count returned by update().
static uint16_t tick_and_update(MidiClock& clk, uint64_t interval_us,
                                ClockSource src = ClockSource::kUsb) {
    g_test_time_us += interval_us;
    clk.feed_byte(0xF8, src);
    return clk.update(g_test_time_us);
}

// Run N tick-and-update cycles. Returns ticks from last update.
static uint16_t drive_ticks(MidiClock& clk, int n, uint64_t interval_us,
                             ClockSource src = ClockSource::kUsb) {
    uint16_t last = 0;
    for (int i = 0; i < n; ++i)
        last = tick_and_update(clk, interval_us, src);
    return last;
}

// Initialize the clock: set mode first (set_mode resets last_update_us),
// then prime with a non-zero update so tick timestamps > 0 work correctly.
static void clock_init(MidiClock& clk, ClockMode mode = ClockMode::kAuto) {
    clk.set_mode(mode);               // must come first; resets last_update_us
    g_test_time_us = 1000;            // non-zero so tick timestamps pass tick_us > 0 check
    clk.update(g_test_time_us);       // primes last_update_us = 1000
}

// BPM from 24-PPQ tick interval in microseconds
// tick_interval_us = 60,000,000 / (bpm * 24)
static constexpr uint64_t bpm_to_tick_us(float bpm) {
    return static_cast<uint64_t>(60'000'000.0f / (bpm * 24.0f));
}

static constexpr uint64_t kTick120 = bpm_to_tick_us(120.0f);  // 20833 μs
static constexpr uint64_t kTick60  = bpm_to_tick_us(60.0f);   // 41666 μs
static constexpr uint64_t kTick200 = bpm_to_tick_us(200.0f);  // 12500 μs

// ===========================================================================
// Suite 1: Basic tick feeding and update
// ===========================================================================

// update() only returns external ticks when synced; before sync it returns
// internal-PPQ ticks. To verify a tick was consumed, check last_tick state
// via is_synced() after enough ticks — or check pending queue is drained
// by verifying BPM calc starts running.
static void test_ticks_consumed_for_bpm_baseline() {
    // Feed 2 tick batches: first sets the baseline, second computes BPM.
    MidiClock clk;
    clock_init(clk);  // mode=kAuto, g_test_time_us=1000, update primes last_update_us
    tick_and_update(clk, kTick120);  // batch 1: sets last_tick_processed_us
    tick_and_update(clk, kTick120);  // batch 2: computes first BPM interval → synced!
    // EMA is primed on the first valid interval; sync is immediate.
    ASSERT_TRUE(clk.is_synced());
}

static void test_multiple_ticks_batch_consumed_on_update() {
    // Feed N ticks then call update — they should all be consumed (atomic exchange).
    MidiClock clk;
    clock_init(clk);
    // batch 1: prime baseline
    tick_and_update(clk, kTick120);
    // Feed 3 more ticks before next update
    g_test_time_us += kTick120; clk.feed_byte(0xF8, ClockSource::kUsb);
    g_test_time_us += kTick120; clk.feed_byte(0xF8, ClockSource::kUsb);
    g_test_time_us += kTick120; clk.feed_byte(0xF8, ClockSource::kUsb);
    clk.update(g_test_time_us);
    // Next update has no pending ticks (they were consumed above)
    uint16_t ticks_after = clk.update(g_test_time_us);
    // Internal PPQ with zero elapsed time → 0 ticks
    ASSERT_EQ(ticks_after, 0u);
}

static void test_no_ticks_no_sync() {
    MidiClock clk;
    clock_init(clk);  // mode=kAuto primed
    clk.update(g_test_time_us);
    ASSERT_FALSE(clk.is_synced());
}

// ===========================================================================
// Suite 2: BPM calculation
// ===========================================================================

// Sync needs: first update sets baseline, subsequent updates compute BPM.
// EMA alpha=2/25 ≈ converges in ~25 samples; drive 50 ticks to be sure.
static void test_120bpm_ticks_sync_to_120bpm() {
    MidiClock clk;
    clock_init(clk);  // mode=kAuto
    drive_ticks(clk, 50, kTick120);
    ASSERT_TRUE(clk.is_synced());
    ASSERT_NEAR(clk.effective_bpm(), 120.0f, 5.0f);
}

static void test_60bpm_ticks_sync_to_60bpm() {
    MidiClock clk;
    clock_init(clk);
    drive_ticks(clk, 50, kTick60);
    ASSERT_TRUE(clk.is_synced());
    ASSERT_NEAR(clk.effective_bpm(), 60.0f, 5.0f);
}

static void test_200bpm_ticks_sync_to_200bpm() {
    MidiClock clk;
    clock_init(clk);
    drive_ticks(clk, 50, kTick200);
    ASSERT_TRUE(clk.is_synced());
    ASSERT_NEAR(clk.effective_bpm(), 200.0f, 10.0f);
}

static void test_too_fast_ticks_ignored_below_5ms() {
    MidiClock clk;
    clock_init(clk);
    drive_ticks(clk, 50, 2000);  // 2ms < kMinIntervalUs=5ms → ignored
    ASSERT_FALSE(clk.is_synced());
}

static void test_too_slow_ticks_ignored_above_125ms() {
    MidiClock clk;
    clock_init(clk);
    drive_ticks(clk, 10, 200000);  // 200ms > kMaxIntervalUs=125ms → ignored
    ASSERT_FALSE(clk.is_synced());
}

// ===========================================================================
// Suite 3: Transport messages
// ===========================================================================

static void test_transport_start_sets_playing() {
    MidiClock clk;
    clock_init(clk);  // mode=kAuto
    ASSERT_EQ(clk.transport_state(), TransportState::kStopped);
    clk.feed_byte(0xFA, ClockSource::kUsb);  // Start
    clk.update(g_test_time_us);
    ASSERT_EQ(clk.transport_state(), TransportState::kPlaying);
}

static void test_transport_stop_clears_playing() {
    MidiClock clk;
    clock_init(clk);
    clk.feed_byte(0xFA, ClockSource::kUsb);
    clk.update(g_test_time_us);
    ASSERT_EQ(clk.transport_state(), TransportState::kPlaying);
    clk.feed_byte(0xFC, ClockSource::kUsb);  // Stop
    clk.update(g_test_time_us);
    ASSERT_EQ(clk.transport_state(), TransportState::kStopped);
}

static void test_transport_continue_sets_playing() {
    MidiClock clk;
    clock_init(clk);
    clk.feed_byte(0xFB, ClockSource::kUsb);  // Continue
    clk.update(g_test_time_us);
    ASSERT_EQ(clk.transport_state(), TransportState::kPlaying);
}

static void test_transport_unknown_byte_ignored() {
    MidiClock clk;
    clock_init(clk);
    clk.feed_byte(0xC0, ClockSource::kUsb);  // Unknown status byte
    clk.update(g_test_time_us);
    ASSERT_EQ(clk.transport_state(), TransportState::kStopped);
}

static void test_transport_start_after_sync_still_playing() {
    MidiClock clk;
    clock_init(clk);
    drive_ticks(clk, 50, kTick120);
    ASSERT_TRUE(clk.is_synced());
    clk.feed_byte(0xFA, ClockSource::kUsb);
    clk.update(g_test_time_us);
    ASSERT_EQ(clk.transport_state(), TransportState::kPlaying);
}

// ===========================================================================
// Suite 4: Sync timeout
// ===========================================================================

static void test_sync_timeout_after_2s_no_ticks() {
    MidiClock clk;
    clock_init(clk);  // mode=kAuto
    drive_ticks(clk, 50, kTick120);
    ASSERT_TRUE(clk.is_synced());

    g_test_time_us += 2'100'000;  // kSyncTimeoutUs = 2,000,000 μs
    clk.update(g_test_time_us);
    ASSERT_FALSE(clk.is_synced());
    ASSERT_EQ(clk.active_source(), ClockSource::kNone);
}

static void test_after_timeout_reverts_to_internal_bpm() {
    MidiClock clk;
    clock_init(clk);
    clk.set_internal_bpm(90.0f);
    drive_ticks(clk, 50, kTick120);
    ASSERT_NEAR(clk.effective_bpm(), 120.0f, 5.0f);

    g_test_time_us += 2'100'000;
    clk.update(g_test_time_us);
    ASSERT_FALSE(clk.is_synced());
    ASSERT_NEAR(clk.effective_bpm(), 90.0f, 0.1f);
}

static void test_resync_after_timeout() {
    MidiClock clk;
    clock_init(clk);
    drive_ticks(clk, 50, kTick120);
    ASSERT_TRUE(clk.is_synced());

    g_test_time_us += 2'100'000;
    clk.update(g_test_time_us);
    ASSERT_FALSE(clk.is_synced());

    // Re-sync: must re-prime baseline (after timeout, last_tick_processed is stale)
    // Give it enough ticks to re-establish BPM
    drive_ticks(clk, 50, kTick120);
    ASSERT_TRUE(clk.is_synced());
}

static void test_unlocked_owner_expires_and_other_source_can_lock() {
    MidiClock clk; clock_init(clk);
    tick_and_update(clk, kTick120, ClockSource::kUsb);  // USB owns before lock
    g_test_time_us += 2'100'000; clk.update(g_test_time_us);
    drive_ticks(clk, 50, kTick120, ClockSource::kBle);
    ASSERT_TRUE(clk.is_synced());
    ASSERT_EQ(clk.active_source(), ClockSource::kBle);
}

static void test_position_accumulates_across_lock_estimates() {
    MidiClock clk; clock_init(clk);
    drive_ticks(clk, 5, kTick120, ClockSource::kUsb);
    ASSERT_GE(clk.snapshot().position, 5u);
}

static void test_realtime_is_owned_by_clock_source() {
    MidiClock clk; clock_init(clk);
    clk.feed_byte(0xFA, ClockSource::kUsb); clk.update(g_test_time_us);
    ASSERT_EQ(clk.transport_state(), TransportState::kPlaying);
    clk.feed_byte(0xFC, ClockSource::kBle); clk.update(g_test_time_us);
    ASSERT_EQ(clk.transport_state(), TransportState::kPlaying);
    clk.feed_byte(0xFC, ClockSource::kUsb); clk.update(g_test_time_us);
    ASSERT_EQ(clk.transport_state(), TransportState::kStopped);
}

static void test_stop_suppresses_external_ticks_until_continue() {
    MidiClock clk; clock_init(clk);
    clk.feed_byte(0xFA, ClockSource::kUsb); clk.update(g_test_time_us);
    drive_ticks(clk, 50, kTick120, ClockSource::kUsb);
    clk.feed_byte(0xFC, ClockSource::kUsb); clk.update(g_test_time_us);
    ASSERT_EQ(tick_and_update(clk, kTick120, ClockSource::kUsb), 0u);
    clk.feed_byte(0xFB, ClockSource::kUsb); clk.update(g_test_time_us);
    ASSERT_GE(tick_and_update(clk, kTick120, ClockSource::kUsb), 1u);
}

// ===========================================================================
// Suite 5: ClockMode::kExternal and kInternal
// ===========================================================================

static void test_kexternal_holds_without_external_lock() {
    // kExternal is a hard sync boundary: it must not invent internal ticks.
    MidiClock clk;
    clock_init(clk, ClockMode::kExternal);  // mode set BEFORE priming update
    clk.set_internal_bpm(120.0f);
    // Advance one full quarter note = 500ms
    g_test_time_us += 500'000;
    uint16_t ticks = clk.update(g_test_time_us);
    ASSERT_EQ(ticks, 0u);
}

static void test_kexternal_uses_external_when_synced() {
    MidiClock clk;
    clock_init(clk, ClockMode::kExternal);
    drive_ticks(clk, 50, kTick120);
    ASSERT_TRUE(clk.is_synced());
    // When synced, update() returns the external tick count
    uint16_t ticks = tick_and_update(clk, kTick120);
    ASSERT_GE(ticks, 1u);
}

static void test_kinternal_ignores_external_ticks() {
    MidiClock clk;
    clock_init(clk, ClockMode::kInternal);
    clk.set_internal_bpm(120.0f);

    // Feed external ticks — kInternal ignores them (no sync)
    drive_ticks(clk, 50, kTick120);
    ASSERT_FALSE(clk.is_synced());

    // Internal generates ~24 ticks in one quarter note (500ms)
    g_test_time_us += 500'000;
    uint16_t ticks = clk.update(g_test_time_us);
    ASSERT_NEAR((int)ticks, 24, 4);
}

static void test_kinternal_effective_bpm_matches_set_value() {
    MidiClock clk;
    clk.set_mode(ClockMode::kInternal);
    clk.set_internal_bpm(140.0f);
    ASSERT_NEAR(clk.effective_bpm(), 140.0f, 0.1f);
}

// ===========================================================================
// Suite 6: effective_bpm and mode switching
// ===========================================================================

static void test_effective_bpm_internal_when_not_synced() {
    MidiClock clk;
    clk.set_mode(ClockMode::kAuto);
    clk.set_internal_bpm(140.0f);
    ASSERT_NEAR(clk.effective_bpm(), 140.0f, 0.1f);
}

static void test_effective_bpm_external_when_synced() {
    MidiClock clk;
    clock_init(clk);  // mode=kAuto
    clk.set_internal_bpm(80.0f);
    drive_ticks(clk, 50, kTick120);
    ASSERT_TRUE(clk.is_synced());
    ASSERT_NEAR(clk.effective_bpm(), 120.0f, 5.0f);
    ASSERT_GT(clk.effective_bpm(), 100.0f);  // clearly not internal 80 BPM
}

static void test_mode_switch_to_internal_clears_sync() {
    MidiClock clk;
    clock_init(clk);  // mode=kAuto
    drive_ticks(clk, 50, kTick120);
    ASSERT_TRUE(clk.is_synced());
    clk.set_mode(ClockMode::kInternal);
    ASSERT_FALSE(clk.is_synced());
    ASSERT_EQ(clk.active_source(), ClockSource::kNone);
}

static void test_ble_source_sets_active_source() {
    MidiClock clk;
    clock_init(clk);  // mode=kAuto
    drive_ticks(clk, 50, kTick120, ClockSource::kBle);
    ASSERT_TRUE(clk.is_synced());
    ASSERT_EQ(clk.active_source(), ClockSource::kBle);
}

static void test_usb_source_sets_active_source() {
    MidiClock clk;
    clock_init(clk);  // mode=kAuto
    drive_ticks(clk, 50, kTick120, ClockSource::kUsb);
    ASSERT_TRUE(clk.is_synced());
    ASSERT_EQ(clk.active_source(), ClockSource::kUsb);
}

// ===========================================================================
// main
// ===========================================================================

static void test_tick_newer_than_the_update_timestamp_is_not_a_timeout() {
    // A transport task can stamp a tick after the main loop sampled its
    // `now`. That tick is newer than now; it must not read as an ancient one.
    MidiClock clk; clk.set_mode(engine::ClockMode::kAuto);
    drive_ticks(clk, 30, kTick120);
    ASSERT_TRUE(clk.snapshot().locked);
    const uint64_t loop_now = g_test_time_us;
    g_test_time_us += 500;                    // the tick lands 0.5 ms later
    clk.feed_byte(0xF8, ClockSource::kUsb);
    clk.update(loop_now);                     // ...but update() runs with the earlier time
    ASSERT_TRUE(clk.snapshot().locked);
}

static void test_clock_only_source_drives_ticks_again_after_a_dropout() {
    // No Start/Stop is ever sent. After the clock goes quiet and comes back,
    // ticks must flow again: a dropout is not a transport Stop.
    MidiClock clk; clk.set_mode(engine::ClockMode::kAuto);
    drive_ticks(clk, 30, kTick120);
    ASSERT_TRUE(clk.snapshot().locked);
    g_test_time_us += 3'000'000;
    clk.update(g_test_time_us);
    ASSERT_FALSE(clk.snapshot().locked);
    uint16_t total = 0;
    for (int i = 0; i < 30; ++i) total += tick_and_update(clk, kTick120);
    ASSERT_TRUE(clk.snapshot().locked);
    ASSERT_TRUE(total > 0);
}

int main() {
    TEST_SUITE("Basic tick feeding and update");
    RUN_TEST(test_ticks_consumed_for_bpm_baseline);
    RUN_TEST(test_multiple_ticks_batch_consumed_on_update);
    RUN_TEST(test_no_ticks_no_sync);

    TEST_SUITE("BPM calculation from tick intervals");
    RUN_TEST(test_120bpm_ticks_sync_to_120bpm);
    RUN_TEST(test_60bpm_ticks_sync_to_60bpm);
    RUN_TEST(test_200bpm_ticks_sync_to_200bpm);
    RUN_TEST(test_too_fast_ticks_ignored_below_5ms);
    RUN_TEST(test_too_slow_ticks_ignored_above_125ms);

    TEST_SUITE("Transport messages (Start/Continue/Stop)");
    RUN_TEST(test_transport_start_sets_playing);
    RUN_TEST(test_transport_stop_clears_playing);
    RUN_TEST(test_transport_continue_sets_playing);
    RUN_TEST(test_transport_unknown_byte_ignored);
    RUN_TEST(test_transport_start_after_sync_still_playing);

    TEST_SUITE("Sync timeout — 2s without ticks reverts to internal");
    RUN_TEST(test_sync_timeout_after_2s_no_ticks);
    RUN_TEST(test_after_timeout_reverts_to_internal_bpm);
    RUN_TEST(test_resync_after_timeout);
    RUN_TEST(test_unlocked_owner_expires_and_other_source_can_lock);
    RUN_TEST(test_position_accumulates_across_lock_estimates);
    RUN_TEST(test_realtime_is_owned_by_clock_source);
    RUN_TEST(test_stop_suppresses_external_ticks_until_continue);

    TEST_SUITE("ClockMode::kExternal and kInternal");
    RUN_TEST(test_kexternal_holds_without_external_lock);
    RUN_TEST(test_kexternal_uses_external_when_synced);
    RUN_TEST(test_kinternal_ignores_external_ticks);
    RUN_TEST(test_kinternal_effective_bpm_matches_set_value);

    TEST_SUITE("effective_bpm and mode switching");
    RUN_TEST(test_effective_bpm_internal_when_not_synced);
    RUN_TEST(test_effective_bpm_external_when_synced);
    RUN_TEST(test_mode_switch_to_internal_clears_sync);
    RUN_TEST(test_ble_source_sets_active_source);
    RUN_TEST(test_usb_source_sets_active_source);
    RUN_TEST(test_tick_newer_than_the_update_timestamp_is_not_a_timeout);
    RUN_TEST(test_clock_only_source_drives_ticks_again_after_a_dropout);

    test_print_result();
    return (g_fail > 0) ? 1 : 0;
}
