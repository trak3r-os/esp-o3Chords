# Architecture

How the firmware is put together: where input enters, which object owns which
state, how a chord becomes MIDI, and how settings are stored. For the
user-facing behaviour see the [manual](manual.md); for every setting see the
[parameter reference](parameters.md).

## Overview

```text
keypad · encoder · CON/BAK · joystick ADC
                 │
                 ▼
     production_ui  (navigation, keypad, actions)
     input_joystick (preset families)
                 │  ParamId edits
                 ▼
     commit_config()  ──►  Config  (single source of truth)
                 │
       ┌─────────┼──────────────┬───────────────┐
       ▼         ▼              ▼               ▼
   Settings   Engine       transports        UiModel
  (NVS blob)    │          (USB / BLE)          │
                ▼                               ▼
         MidiScheduler ─► NoteLedger      production_render
                │                               │
                ▼                               ▼
          MIDI Router ─► USB device · USB host · BLE     OLED
```

`main.cpp` initialises the hardware and runs the loop in a fixed order:
keypad events, encoder and button events, joystick, engine and scheduler
service, MIDI transport service, persistence, then `render_production_screen`.
The native tests drive these same entry points and replace only the hardware
boundaries with stubs.

## State ownership

| Object | Owns |
|--------|------|
| `contract::Config` (`AppState::config()`) | Every persisted setting. Validated and revisioned |
| `Settings` | The flat POD projection of Config that the engine reads and NVS stores. `AppState::sanitize()` clamps every field on load |
| `UiState` | Transient navigation: mode, cursors, confirmations, feedback |
| `ChordModifier` | Live performance state: octave shift, manual inversion, the remembered previous chord |
| `JoystickState` | Gesture state of the active preset (armed chord, latched notes, last direction) |
| `NoteLedger` | Lifetime of every sounding note, shared between the scheduler task and the main loop; locks internally |

**`commit_config()` is the only way to change a setting.** It lives in
`input_context.cpp`, validates the edited Config, projects it onto `Settings`,
the engine and the transports, rebuilds the progression when the scale
changed, and marks the configuration for autosave. Encoder edits, keypad key
changes, Reset and the joystick's own setting changes all go through it.
Writing a persisted value straight into `Settings` does not stick: the next
commit overwrites it from Config.

`Config.key` and `Config.scale` are indexes; their name strings are derived by
`config::sync_names()`. `Settings::scale` is the *engine* scale, which is
`Major` for every genre mode (genres change chord qualities, not the scale).

## Navigation and rendering

`production_ui.cpp` implements three modes:

```text
PLAY ── # ──► SELECT ── press / CON ──► EDIT
  ▲             │  ▲                      │
  └──── BAK ────┘  └──────── BAK ─────────┘
```

Rows are identified by `ParamId`. In PLAY the encoder edits the value under the
PLAY cursor (`UiState::play_cursor`, or `joystick_cursor` on the joystick
side); octave and inversion are performance shortcuts on keys C/D and 8/9 and
are not in the cursor. A long `#` opens System, whose action rows (Panic,
Reset, Reboot, DFU) need a confirmation.

`UiModel` (`ui_model.cpp`) is a pure projection of Config, navigation,
transport state and feedback. `production_render.cpp` draws that model on a
128×64 framebuffer with a 5×7 font, 21 characters per row. Each joystick
family has its own screen body: compass or walk strip for NOTES, direction
rings or ranked list for CHORD, a 2-D pad for CONT.

## How a chord becomes notes

`Engine::voice_chord()` is the single path: **register** (the octave, never
overridden) → **inversion** → **voicing spread**. The inversion is the
player's when one is set by hand (keys 8/9,
`ChordModifier::set_manual_inversion`); otherwise voice leading picks it:

- **Off** — as written.
- **Smooth** — the inversion and octave placement nearest the previous chord.
- **Common** — the same, but notes shared with the previous chord stay put
  first.

Candidates are limited to a bass within ±6 semitones of the key's tonic at the
selected octave, so a progression cannot drift out of its register.

The engine then plays the notes as a chord, a strum or an arpeggio.
`Engine::play_step()` handles one-shot triggers; `Engine::service_held()`
drives held strums and arpeggios from the clock's PPQ ticks.

## Joystick

`Config.active_preset` selects one of eight factory presets (`app::PresetId`),
each in one family (`app::JoyFamily`):

| Family | Presets | Behaviour |
|--------|---------|-----------|
| NOTES | Lead, Bass, Drone | Emits its own notes through the NoteLedger on the joystick channel |
| CHORD | Colors, Discover, Voicings | Arms a change for the next numbered key; never sounds a note |
| CONT | Bend, Macro | Streams pitch bend, vibrato, CC or aftertouch from the stick position |

Each family keeps its full parameter set in `JoyFamilyState`, so switching
presets never discards another family's tuning. A preset change releases the
old family's notes before the new one accepts a gesture.

The driver reports raw ADC units; `JoystickAnalog::normalize_axis()` is the
single conversion to the ±512 range the app layer works in
(`process_joystick()` stores the result in `js.raw_x` / `js.raw_y`).

## MIDI timing and routing

- **`MidiScheduler`** queues timed note-on/off events and fires them from an
  `esp_timer` callback (priority 22). All public methods are mutex-protected.
  A channel change flushes notes still sounding on the old channel.
- **`MidiClock`** provides the internal clock or follows external MIDI clock
  (USB or BLE). In Auto mode it follows an external clock while one is running.
- **`Router`** fans every message out synchronously to all registered
  `TransportSink`s: USB device (TinyUSB), USB host and BLE (NimBLE). Its
  methods are non-virtual; new transports subclass `TransportSink`.

## Persistence

`Config` is encoded by `config_codec.cpp` into a versioned NVS blob with a
CRC-32 and saved about one second after the last edit. Each save carries the
Config revision, so a stale save cannot clear a newer pending change. On load,
earlier wire versions are migrated forward (v1 `Settings` blob → v2 → v3 →
v4). With no stored blob the device starts on `config::factory_defaults()`.
A blob that is corrupt or unsupported is never overwritten silently: the
device runs on defaults with autosave blocked (`NO SAVE` on screen) until
System → Reset.

USB role edits change the desired role only; it is applied at boot, and the UI
shows `REBOOT` while desired and applied roles differ.

## Source map

| Path | Responsibility |
|------|----------------|
| `src/app/contracts/contracts.h` | Config, ParamId and UI contracts |
| `src/app/input_context.*` | `InputContext` reference bundle, `commit_config()` |
| `src/app/production_ui.*` | Navigation, keypad handling, PLAY cursor, actions |
| `src/app/input_joystick.*`, `joy_family.*` | Joystick presets and family logic |
| `src/app/parameters.*` | Parameter descriptors, ranges, formatting |
| `src/app/config_codec.*` | Config wire format, migrations, factory defaults |
| `src/app/settings_bridge.*` | Config → Settings → engine projection |
| `src/app/ui_model.*` | Pure UiModel projection |
| `src/ui/production_render.*` | OLED layout and drawing |
| `src/engine/` | Chords, voice leading, performer, arpeggiator, scheduler, clock, ledger |
| `src/midi/` | Router and transport sinks |
| `src/drivers/` | I²C, OLED, keypad matrix, joystick ADC, encoder, buttons, NVS |

## Testing

```bash
./test/run_tests.sh          # configure, build and run every suite
pio run -e esp32s3           # firmware build
```

Both run in CI on every push. The native suites cover the engine, Config and
persistence, navigation, joystick logic and the renderer; golden framebuffers
in `test/golden/` pin the screens pixel for pixel. USB, BLE, the joystick ADC,
the encoder and the OLED panel are stubbed on the host and are verified on the
device.
