# o3Chords — Claude Code Project Guide

## What this project is

ESP32-S3 USB/BLE MIDI chord instrument. A 4×4 keypad triggers diatonic chord
degrees, a joystick plays notes, changes the next chord or sends continuous
control, and a rotary encoder drives an OLED parameter UI. Output is standard
MIDI — USB device and BLE simultaneously via a fan-out router.

User docs: `docs/manual.md`, `docs/joystick-modes.md`, `docs/parameters.md`.
Developer overview: `docs/architecture.md`. Docs and code comments are in
English.

## Build

```bash
pio run -e esp32s3                  # firmware (platform pinned: espressif32@6.13.0, ESP-IDF 5.5.3)
pio run -e esp32s3 -t upload        # flash
pio device monitor -b 115200        # serial log
pio run -e esp32s3_keypadtest -t upload   # keypad wiring test, no app logic
```

The 8 MB-board / 2 MB-sdkconfig flash size warning is expected.

## Native test suite (no hardware needed)

```bash
./test/run_tests.sh                 # configure + build + run all suites (test/build)
cmake -B test/build -S test && cmake --build test/build -j
ctest --test-dir test/build --output-on-failure
```

CI: `.github/workflows/tests.yml` (native suite) and `firmware.yml`
(PlatformIO build) on every push.

`test/build/screen_dump` renders the real OLED screens on the host:
`./screen_dump` lists scenarios, `./screen_dump <name>` prints the pixels,
`--calls` lists the draw calls instead, and `./screen_dump --voicing` prints
the notes a few progressions sound in each voice-leading mode.

Golden framebuffers in `test/golden/` pin the screens; regenerate with
`UPDATE_GOLDEN=1` only after looking at the screen with `screen_dump`, then
refresh the doc screenshots with `python3 test/tools/golden_to_png.py`.

## Architecture

```
main.cpp
├── Drivers  (keypad, joystick, encoder, OLED, I2C, NVS)
├── Engine   (chord builder, arpeggiator, performer, MIDI scheduler)
│   ├── MidiScheduler  — esp_timer-based note-on/off queue (priority 22)
│   ├── MidiClock      — internal/external PPQ clock
│   └── ChordModifier  — live octave / inversion / voice-leading memory
├── MIDI     (Router → BLE sink + USB device sink + USB host sink)
├── App
│   ├── InputContext   — shared reference bundle passed to every handler
│   ├── production_ui  — navigation (Play → Select → Edit), keypad, actions
│   ├── input_joystick — families NOTES / CHORD / CONT / PARAM, 8 factory presets (PARAM has none)
│   └── config_codec   — Config wire format, migrations, factory defaults
├── UI       (production_render: OLED layout, display_names)
└── Storage  (NVS Config blob with CRC; magic 0x4F334348)
```

## Key design rules

- **Config is the authority, and `commit_config()` is the only way in.**
  `app::commit_config()` (input_context.cpp) validates an edited
  `contract::Config`, projects it onto `Settings`/engine/transports, rebuilds
  the progression when the scale changed and marks it for autosave. Encoder
  edits, key changes, Reset and the joystick's own setting changes (walk
  velocity, Voicings) all go through it. Never write a persisted value to
  `Settings` directly — the next commit overwrites it from Config.
  `Config.key`/`Config.scale` are indexes; the name strings are derived
  (`config::sync_names`). `Settings::scale` holds the *engine* scale, which is
  "Major" for every genre mode.
- **Settings** is a flat POD struct; `AppState::sanitize()` clamps every
  field. Config is persisted to NVS after 1 s idle. Stored blobs from earlier
  wire versions are migrated on load (v1 Settings → v2 → v3 → v4); keep those
  migrations working when changing the format.
- **How a chord becomes notes** is one function, `Engine::voice_chord()`:
  register (Octave, never overridden) → inversion → voicing spread. The
  inversion is the player's when one is set by hand (keys 8/9,
  `ChordModifier::set_manual_inversion`), otherwise voice leading's: Off =
  as written, Smooth = the inversion/octave placement nearest the previous
  chord, Common = same but shared notes stay put first. Candidates are
  limited to a bass within ±6 semitones of the key's tonic at the selected
  octave, so progressions cannot drift.
- **Engine::service_held** drives continuous arp/strum from PPQ ticks;
  separate code path from the one-shot dispatch in `play_step()`.
- **MidiScheduler** fires from the esp_timer task (priority 22). All public
  methods are mutex-protected for cross-task safety. A channel change flushes
  notes sounding on the old channel.
- **NoteLedger is shared across tasks** (scheduler on the esp_timer task,
  joystick notes on the main loop) and locks internally.
- **Router** fans out to all registered `TransportSink`s synchronously.
  Non-virtual methods — subclass via TransportSink, not Router.
- **UI navigation** (production_ui): Play → Select (`#`) → Edit. In Play the
  encoder edits the value under the PLAY cursor (`play_cursor` /
  `joystick_cursor`); long `#` opens System. Every row has a 21-character
  budget on the 128×64 OLED.
- **Joystick axes are ±512 in the app layer.** The driver reports ADC units;
  `JoystickAnalog::normalize_axis()` is the single conversion
  (`process_joystick()` stores the result in `js.raw_x/raw_y`).

## Test suite structure

One suite per file in `test/unit/` (`ls test/unit/` is the list; each is
registered in `test/CMakeLists.txt`). Engine behaviour: `test_chord_building`
(chords, voicings, voice leading), `test_arpeggiator`, `test_service_held`,
`test_strum_rhythm`, `test_midi_scheduler`, `test_midi_clock_external`,
`test_note_ledger`. App/UI: `test_parameters`, `test_config_codec`,
`test_navigation`, `test_keypad_joystick`, `test_ui_snapshot`,
`test_config_revision`, `test_voice_ownership`, the `test_joystick*` family
and `test_production_ui`, which owns the golden framebuffers in
`test/golden/`. Currently 41 suites, ~680 tests, all green.

## Known gaps

- The key wheel (`draw_key_wheel()` in production_render.cpp) edits Key live
  on every encoder tick, like every other `ParamId`; there is no
  apply-on-release.
- The PLAY cursor only reaches Config-backed values with a `ParamId`;
  `O:`/`I:` (octave, inversion) are live `ChordModifier` state with no
  `ParamId` and are deliberately left out.
- Voice leading Common rarely differs audibly from Smooth on triads (the
  nearest placement already keeps the shared notes); it mainly matters for
  four-note-and-larger chords.
- The `JoystickPlayMode` compatibility helpers in input_context.cpp
  (`apply_joystick_mode`, `cycle_joystick_mode_in_filter`,
  `joy_cycle_mode_config`) and `Engine::play_notes`/`play_chord` have no
  production callers; only tests drive them.
- `joystick_rotation` rotates directions and X/Y in opposite senses for odd
  values; it has no UI row and is only reachable from a migrated v1 blob.
- The Strum feel row (`ParamId::StrumFeel`) has a 0–4 range but stores the
  strum delay in ms (`Settings::strum_ms`, factory 14), so editing it clamps
  the delay to 0–4 ms.
- USB/BLE MIDI wire format, the joystick ADC, the encoder and the OLED panel
  are stubbed in native tests and need verification on hardware.
- NVS persistence round-trip — Storage stubs return false for load.
