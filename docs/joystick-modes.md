# Joystick Reference

The joystick has eight factory presets, grouped into three families. The
active preset decides what the stick does; its family decides which screen
body is drawn and which parameters the Edit menu shows. There is no physical
joystick button: picking a preset implies its family.

Walk the presets with the **Recipe** row in the Joystick filter (turning the
encoder wraps at both ends). The order is:

```
Lead · Bass · Drone  |  Colors · Discover · Voicings  |  Bend · Macro
   NOTES                       CHORD                        CONTINUOUS
```

Each family keeps its own full set of parameters, so tuning one preset never
overwrites another family's tuning.

## NOTES: the stick plays notes

| Preset | Source | Gesture | Voices | Sustain | Notes |
|--------|--------|---------|--------|---------|-------|
| Lead | Scale | Walk | Single note | Momentary | Lead channel; velocity by distance |
| Bass | Scale | Clock | Single note | Momentary | Mono, two octaves down |
| Drone | Degrees | Clock | Chord | Latch | A direction latches a chord on the clock layout |

Parameters per family (Edit menu): **Source** (Degrees, Scale, Chord tones,
Intervals, Pentatonic), **Gesture** (Clock, Walk), **Voices** (Note, Chord),
**Sustain** (Momentary, Latch), **Mono**, **Octave** (-2 to +2), **Vel mode**
(Fixed, Distance), **Channel** (Chord, Lead).

Releasing the stick to centre releases a momentary note. A latched note stays
until its own direction is pressed again.

## CHORD: the stick changes the next chord

Chord-family directions never sound a note. They arm a change that the next
numbered key applies. Arming the same direction again disarms it. Moving the
stick while a chord is held does not release that chord.

| Preset | Direction arms | Parameter |
|--------|----------------|-----------|
| Colors | A quality from the palette (the armed tag shows as `+quality` on PLAY) | Palette |
| Discover | A ranked suggestion for the next key | Palette (`7THS` by default), ranking |
| Voicings | A voicing spread for the chord (Config, survives unrelated edits) | None |

Edit-menu parameters: **Palette** (Colors and Discover), **Order** (Fixed,
Ranking) and **Retrigger** (hidden; fixed per preset).

## CONTINUOUS: the stick is a position

Continuous presets read the stick's position on both axes, after the deadzone
and curve. Nothing is sent at rest inside the deadzone.

| Preset | X axis | Y axis |
|--------|--------|--------|
| Bend | Pitch bend, snapped to chord tones (or chromatic, or off) | Vibrato, up to ±50 cents |
| Macro | Continuous CC (default CC 1) | Continuous CC (default CC 11) |

Edit-menu parameters per axis: **X/Y target** (Bend, Vibrato, CC, Aftertouch),
**X/Y CC** number, **Bend snap**. Shared: **Curve** (Linear, Exp, Log) and
**Deadzone** (0–100 %, default 10 %). Bend uses the bend range from Config
(default 12 semitones); vibrato stays within ±50 cents whatever that range is.

Pushing the Y axis up sends higher values. Invert X and Invert Y in System flip
the axes.

## Keypad as joystick

With **Keypad as joy** enabled, keys 1, 2, 3, 4, 6 and 7 act as directions and
key 5 returns to centre. Keys 8 and 9 keep their inversion function. The keys
do not play chords while this is on.
