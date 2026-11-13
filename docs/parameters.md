# Parameter Reference

Every user-editable value is a `ParamId` (`src/app/contracts/contracts.h`).
Edits go through `commit_config()` (`src/app/input_context.cpp`), which validates
the `Config`, applies it to the engine and transports, and schedules a save
about one second after the last edit.

## Filters

The Edit menu shows one filter at a time. A tap on `#` opens Chord (or
Joystick when the PLAY screen is on the joystick side); a long press opens
System.

| Filter | Parameters |
|---|---|
| Chord (`PLAY EDIT`) | Velocity, Duration, Key, Scale, Style (articulation), Arp rate, Arp pattern, Arp octaves, Strum feel, Swing, Humanize, Voicing, BPM, Voice leading, Roll, Roll direction, Trigger |
| Joystick (`JOYSTICK EDIT`) | Recipe (the active preset), Joy ch (lead channel), Vel (solo velocity), and the active preset's family rows (below) |
| System (`SYSTEM`) | Chord ch, Keypad joy, Clock, USB role, BLE, Invert X/Y, Panic, Reset, Reboot, DFU, Info, Calibrate joy |

Rows whose value is not used by the active preset are hidden. The menu shows
the labels from `short_label()` in `production_render.cpp` (e.g. `Style`,
`Joy ch`); the descriptor table in `parameters.cpp` holds the long names.

Some `ParamId`s exist without a menu row: CHORD's `ChordOrder` and
`ChordRetrigger` (fixed per preset), the generic joystick assignment fields
`JoystickXTarget`, `JoystickYTarget` and `JoystickCurve` (the CONT family's
own targets, CC numbers and curve are what the presets use), and the PARAM
family rows (no factory preset uses PARAM). They stay readable and writable
through `commands.cpp`.

Panic, Reset, Reboot, DFU, Info and Calibrate are action rows. The four that
change state require confirmation.

## Joystick presets and families

`Config.active_preset` selects one of eight factory presets. Each belongs to
one family (`app::JoyFamily`); the family picks the joystick screen body and
which family rows appear in the Joystick filter. Each family keeps its own
full parameter set (`JoyFamilyState`), so switching presets never discards
another family's tuning.

| Preset | Family | Joystick body |
|---|---|---|
| Lead, Bass, Drone | NOTES | Compass (Clock gesture) or walk strip (Walk gesture) |
| Colors, Discover, Voicings | CHORD | Direction rings (Colors, Voicings) or ranked list (Discover) |
| Bend, Macro | CONT | 2-D pad with X and Y readouts |

`PresetId::kFlip` is a disabled alias that only exists so stored
configurations that reference it still decode. The PARAM family has no
factory preset.

Family parameters, one `ParamId` per field:

| Family | ParamIds |
|---|---|
| NOTES | `NotesSource`, `NotesGesture`, `NotesVoices`, `NotesSustain`, `NotesMono`, `NotesOctave`, `NotesVelMode`, `NotesChannel` |
| CHORD | `ChordPalette`, `ChordOrder` (hidden), `ChordRetrigger` (hidden) |
| CONT | `ContXTarget`, `ContYTarget`, `ContXCc`, `ContYCc`, `ContBendSnap`, `ContCurve`, `ContDeadzone` |

`RecipeId` is part of the stored Config wire format (v2); on load,
`config_codec.cpp` maps each value onto the matching preset.

## Config values

| ParamId | Config field | Effect |
|---|---|---|
| `Bpm` | `bpm` | Engine tempo and internal clock |
| `Key` | `key` / `key_name` | Key for chords and generated notes. Edited live on the key wheel; keys A/B step it by a semitone |
| `Scale` | `scale` / `scale_name` | One of 22 `ProgressionMode` presets (7 modes + 15 genres). Sets the chord qualities of the seven degrees |
| `Voicing` | `voicing_spread` | Voicing spread for chords; Voicings preset writes it too |
| `VoiceLeading` | `voice_leading` | Off, Smooth or Common. See the manual's Voice leading section |
| `Roll` / `RollDir` | `roll_ms` / `roll_dir` | Per-voice stagger and its direction; exclusive with strum |
| `PlayTrigger` | `play_trigger` | Shot, Hold or Repeat |
| `SoloVelocity` | `solo_velocity` | Velocity of single-note joystick sources with fixed velocity |
| `ChordChannel` | `chord_channel` | MIDI channel for chords |
| `LeadChannel` | `lead_channel` | MIDI channel for lead and joystick notes; owners are released before a change |
| `JoystickRecipe` | `active_preset` | Preset, and therefore family |
| `KeypadAsJoy` | `joystick.keypad_as_joy` | Keypad keys act as joystick directions (see the manual) |
| `UsbRole` | `usb_role` | Off, Device or Host. Desired state only; takes effect after reboot |
| `BLE` | `ble_enabled` | Bluetooth MIDI on or off |

Channels are stored zero-based and displayed one-based. `Config.key` and
`Config.scale` are indexes; the name strings are derived from them
(`config::sync_names`). `Settings::scale` is the engine's scale, which is
"Major" for every genre mode.

## Feedback

- `SAVE!` shows while edits are waiting to be saved.
- `REBOOT` shows when the USB role differs from the one applied at boot.
- `NO SAVE` shows when storage is blocked; Reset clears the block.
- A `+quality` tag on PLAY means a Colors or Discover change is armed for the
  next chord.

Golden framebuffer snapshots in `test/golden/` are the pixel-level reference
for the screens (regenerate with `UPDATE_GOLDEN=1` after checking the new
pixels with `screen_dump`; `test/tools/golden_to_png.py` turns them into the
PNGs in `docs/images/`). This document describes behaviour; the goldens
describe appearance.
