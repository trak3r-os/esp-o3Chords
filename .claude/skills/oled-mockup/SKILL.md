---
name: oled-mockup
description: Build a pixel-exact mockup sheet for this project's 128x64 monochrome OLED and publish it as an Artifact. The page draws each screen through a JavaScript port of the firmware's own framebuffer, primitives and kFont5x7 glyph table, so what it shows is what the panel would light up. Use when asked to see, mock up, review, compare or spec any OLED screen or UI layout - "show me how the screen would look", "mock up the joystick screens", "give me the 128x64 mockups", "what would this look like on the display" - or when proposing a UI change whose value depends on how it reads at 21 characters per row.
---

# OLED mockups

Produces a page of panel simulations with zoom and page/cell/pixel grid
overlays, plus a caption per screen listing the draw calls that make it. Not
sketches: the font bytes, the framebuffer layout and the primitives come from
the firmware, so a row that would overflow, a glyph the font lacks, or the two
pixels an inverted row cannot cover all show up here rather than on hardware.

## Workflow

**1. Read the real renderer before drawing anything.** Open
`src/ui/production_render.cpp` and the display driver, and copy the geometry
you find: which page each element sits on, the character budget per row, which
helper right-flushes a value. A mockup that invents its layout is worse than
no mockup, because it will be believed. Where you deliberately change the
layout, say so in that screen's `desc`.

**2. Write a screens file.** One object per screen, in a `const screens = [...]`
array. See `assets/example-screens.js`.

```js
const screens = [
  {
    id: 'play',                    // unique slug
    title: 'PLAY',                 // heading beside the panel
    badge: 'home',                 // small qualifier chip
    tag: 'new',                    // 'new' | 'kept' | 'updated' - colours the chip
    desc: "One or two sentences. HTML allowed. Say what changed and why, and
           name the code it comes from.",
    calls: [                       // [primitive, what it draws]
      ['pair(0, inv)', 'title row, value flushed right'],
    ],
    draw(o) { /* issue the same calls the firmware would */ },
  },
];
```

**3. Build.** From the skill directory, or with absolute paths:

```bash
python3 .claude/skills/oled-mockup/scripts/build.py \
  --screens <screens.js> \
  --out <output.html> \
  --title "<Artifact name>" \
  --font-source src/drivers/oled_framebuffer.cpp \
  --meta "SSD1315 128&times;64 mono &middot; 8 pages of 8&nbsp;px" \
  --intro <optional-fragment.html> --footer <optional-fragment.html>
```

The glyph table **and its character bounds** are read out of the source, so if
the firmware gains or loses glyphs the mockups follow. The build fails loudly
if the table does not parse cleanly or the bounds disagree with its size —
that failure is a real finding about the font, not a script bug to work
around. `--font-symbol` and `--glyph-bytes` cover a different table.

Write the output and the screens file to the scratchpad directory unless asked
otherwise; they are build inputs, not project files.

**4. Publish with the Artifact tool**, passing the built HTML as `file_path`.
Redeploy to the same path to keep the URL. Give the artifact a favicon on its
first publish and a short `description`.

## Primitives

Faithful ports, same names and argument order as the C++ where they exist.
`o` is the `Oled` instance handed to `draw`.

| Call | Draws |
|---|---|
| `o.drawText(x, page, text, invert)` | 6 px advance per char; out-of-range codepoints become `?`, as in firmware |
| `o.drawText2x(x, page, text, invert)` | doubled glyphs, **two pages tall**, 12 px per char |
| `o.hline(x0, x1, y)` / `o.vline(x, y0, y1)` | one-pixel lines |
| `o.fillRect(x, y, w, h, on)` | filled block; `on: false` clears |
| `o.fillLine(page, value)` | fills a whole 8 px page row |

Helpers ported from `production_render.cpp`, which you should prefer over
raw calls so the mockup inherits the firmware's own spacing rules:

| Helper | Use |
|---|---|
| `pair(o, page, left, right, inv)` | the 21-character row budget: value reserved first, label yields |
| `center(o, page, text)` | centred text |
| `spread(o, page, tokens, sel)` | N tokens across the full width, last flush right; `sel` knocks one out (`-1` for none) |
| `cursorAt(o, page, x, text)` / `cursorRight(o, page, text)` | knock out one token in place, to mark the encoder's cursor |
| `compass(o, cells, active)` | 3x3 direction rose; `cells` indexed by this codebase's convention `0=E,1=NE,2=N,3=NW,4=W,5=SW,6=S,7=SE`; centre is a dot |
| `box(o, x0, y0, x1, y1)` / `meter(o, x, y, w, h, frac)` | outline; outline plus proportional fill |
| `listScreen(o, title, rows, selected)` | a Select list: `n/total` header, 7 visible rows, selection third from top |
| `valueEditor(o, title, label, value, frac, footL, footR)` | the Edit screen: label, 2x value, range slider, two footer rows |

## Rules that keep it honest

- **A text row is 21 characters** (126 of 128 px). `pair()` enforces the
  budget; if you place text by hand, check `x + len * 6 <= 128`.
- **`drawText2x` costs two pages.** Do not draw anything else on the second.
- **An inverted row leaves 2 px lit at the right edge** — `fillLine` covers
  128 px, 21 characters cover 126. That artefact is correct; do not hide it.
- **Only glyphs in the table exist.** Anything else renders `?`. Check the
  bounds the build reports before using a symbol; box drawing and arrows are
  primitives (`hline`/`vline`/`fillRect`), not characters.
- **Show the panel at rest.** A screen whose information only appears while a
  control is held is the failure mode these mockups exist to catch.
- Where a value is unknown, use a plausible real one and never `lorem`
  placeholders; the point is judging legibility at real widths.

## Verifying before publishing

For a screen with tight geometry, port the same draw calls into a throwaway
Python script that dumps the framebuffer as ASCII (`#`/`.` per pixel) and read
it. It catches collisions and overflows that eyeballing the code does not, and
it costs one command.
