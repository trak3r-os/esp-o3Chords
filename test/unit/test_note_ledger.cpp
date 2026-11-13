#include "../framework.h"
#include "engine/note_ledger.h"

struct LedgerSink : engine::NoteLedgerSink {
  int ons = 0, offs = 0; uint8_t last_note = 0;
  void note_on(uint8_t, uint8_t, uint8_t n, uint8_t) override { ++ons; last_note = n; }
  void note_off(uint8_t, uint8_t, uint8_t n) override { ++offs; last_note = n; }
};

static app::contract::NoteIntent note(uint16_t source, uint8_t channel, uint8_t pitch, uint8_t routes = 1) {
  return {source, 1, channel, pitch, 100, routes};
}

static void test_shared_and_note_zero() {
  LedgerSink sink; engine::NoteLedger ledger(&sink);
  app::contract::VoiceToken a{}, b{};
  ASSERT_TRUE(ledger.acquire(note(1, 0, 0), a));
  ASSERT_TRUE(ledger.acquire(note(2, 0, 0), b));
  ASSERT_EQ(sink.ons, 1); ASSERT_TRUE(ledger.release(a)); ASSERT_EQ(sink.offs, 0);
  ASSERT_TRUE(ledger.release(b)); ASSERT_EQ(sink.offs, 1); ASSERT_FALSE(ledger.release(b));
}

static void test_channel_and_source_isolation() {
  LedgerSink sink; engine::NoteLedger ledger(&sink);
  app::contract::VoiceToken a{}, b{};
  ASSERT_TRUE(ledger.acquire(note(1, 0, 60), a)); ASSERT_TRUE(ledger.acquire(note(1, 1, 60), b));
  ASSERT_EQ(ledger.release_source(1), 2); ASSERT_EQ(sink.offs, 2);
}

static void test_panic_clears_even_when_released_twice() {
  LedgerSink sink; engine::NoteLedger ledger(&sink); app::contract::VoiceToken token{};
  ASSERT_TRUE(ledger.acquire(note(4, 0, 64, 3), token)); ledger.panic();
  ASSERT_EQ(ledger.active_tokens(), 0); ASSERT_EQ(ledger.active_voices(), 0);
  ASSERT_FALSE(ledger.release(token));
}

static void test_batch_is_atomic_when_capacity_is_insufficient() {
  LedgerSink sink; engine::NoteLedger ledger(&sink);
  app::contract::VoiceToken held{};
  for (uint8_t i = 0; i < 31; ++i)
    ASSERT_TRUE(ledger.acquire(note(9, 0, static_cast<uint8_t>(i + 30)), held));
  app::contract::NoteIntent triad[3] = {note(10, 0, 90), note(10, 0, 91), note(10, 0, 92)};
  app::contract::VoiceToken tokens[3]{};
  const int before = sink.ons;
  ASSERT_FALSE(ledger.acquire_batch(triad, 3, tokens));
  ASSERT_EQ(sink.ons, before);
  ASSERT_EQ(ledger.active_tokens(), 31);
}

static void test_token_ids_survive_wraparound_while_a_note_is_held() {
  // 16-bit ids wrap after 65535 acquisitions. A drone held across the wrap
  // must still be the note its own token releases.
  LedgerSink sink; engine::NoteLedger ledger(&sink);
  app::contract::VoiceToken held{};
  ASSERT_TRUE(ledger.acquire(note(1, 0, 40), held));
  for (uint32_t i = 0; i < 70000; ++i) {
    app::contract::VoiceToken t{};
    ASSERT_TRUE(ledger.acquire(note(2, 0, 60), t));
    ASSERT_TRUE(t.value != held.value);
    ASSERT_TRUE(ledger.release(t));
  }
  ASSERT_EQ(ledger.active_voices(), 1);
  ASSERT_TRUE(ledger.release(held));
  ASSERT_EQ(ledger.active_voices(), 0);
}

int main() {
  TEST_SUITE("NoteLedger");
  RUN_TEST(test_shared_and_note_zero);
  RUN_TEST(test_channel_and_source_isolation);
  RUN_TEST(test_panic_clears_even_when_released_twice);
  RUN_TEST(test_batch_is_atomic_when_capacity_is_insufficient);
  RUN_TEST(test_token_ids_survive_wraparound_while_a_note_is_held);
  test_print_result();
  return g_fail ? 1 : 0;
}
