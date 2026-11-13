#!/usr/bin/env python3
"""Build a pixel-exact display-mockup page from a screens definition file.

The framebuffer, primitives and glyph table come from the firmware itself, so
what the page draws is what the panel would light up.
"""
import argparse, json, pathlib, re, sys

HERE = pathlib.Path(__file__).resolve().parent
ASSETS = HERE.parent / 'assets'


def strip_comments(text: str) -> str:
    """Drop // and /* */ comments so numbers inside them are never read as data."""
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def extract_font(source: pathlib.Path, symbol: str, glyph_bytes: int):
    """Pull the glyph table and its character bounds out of the firmware source.

    Bounds come from the source too (kFontFirstChar / kFontLastChar or the
    equivalents), so a mockup can never claim a glyph the firmware would
    render as '?'. Both the .cpp and its header are searched.
    """
    text = source.read_text()
    m = re.search(re.escape(symbol) + r'\s*\[\s*\]\s*=\s*\{(.*?)\};', text, re.S)
    if not m:
        raise SystemExit(f"could not find `{symbol}[] = {{...}}` in {source}")
    body = strip_comments(m.group(1))
    vals = re.findall(r'0[xX]([0-9A-Fa-f]{1,2})|(\b\d{1,3}\b)', body)
    nums = [int(h, 16) if h else int(d) for h, d in vals]
    if not nums:
        raise SystemExit(f"`{symbol}` matched but held no byte values")
    if len(nums) % glyph_bytes:
        raise SystemExit(
            f"`{symbol}` holds {len(nums)} bytes, not a multiple of {glyph_bytes} — "
            "the table did not parse cleanly, check for stray numbers")

    # Bounds: look in the .cpp, then in a same-stem header next to it.
    haystack = text
    header = source.with_suffix('.h')
    if header.exists():
        haystack += header.read_text()
    def bound(name, default):
        b = re.search(name + r'\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)', haystack)
        return int(b.group(1), 0) if b else default
    first = bound('kFontFirstChar', 0x20)
    last = bound('kFontLastChar', first + len(nums) // glyph_bytes - 1)

    span = last - first + 1
    if span != len(nums) // glyph_bytes:
        raise SystemExit(
            f"bounds 0x{first:02X}..0x{last:02X} span {span} glyphs but the table "
            f"holds {len(nums) // glyph_bytes}")
    return ','.join(str(n) for n in nums), len(nums), first, last


def read(path):
    return pathlib.Path(path).read_text() if path else ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--screens', required=True, help='JS file defining `const screens = [...]`')
    ap.add_argument('--out', required=True, help='HTML file to write')
    ap.add_argument('--title', required=True, help='page <title>, also the artifact name')
    ap.add_argument('--font-source', required=True, help='C/C++ file holding the glyph table')
    ap.add_argument('--font-symbol', default='kFont5x7')
    ap.add_argument('--glyph-bytes', type=int, default=5, help='columns per glyph')
    ap.add_argument('--eyebrow', default='display mockups')
    ap.add_argument('--meta', default='', help='HTML fragment for the masthead right column')
    ap.add_argument('--intro', default='', help='file with an HTML fragment placed above the sheet')
    ap.add_argument('--footer', default='', help='file with an HTML fragment placed below the sheet')
    a = ap.parse_args()

    font, count, first, last = extract_font(
        pathlib.Path(a.font_source), a.font_symbol, a.glyph_bytes)
    shell = (ASSETS / 'shell.html').read_text()

    html = (shell
            .replace('__STYLE__', (ASSETS / 'style.css').read_text())
            .replace('__OLED__', (ASSETS / 'oled.js').read_text())
            .replace('__RENDER__', (ASSETS / 'render.js').read_text())
            .replace('__SCREENS__', pathlib.Path(a.screens).read_text())
            .replace('__FONT__', font)
            .replace('__FONT_FIRST__', str(first))
            .replace('__FONT_LAST__', str(last))
            .replace('__GLYPH_BYTES__', str(a.glyph_bytes))
            .replace('__EYEBROW__', a.eyebrow)
            .replace('__META__', a.meta)
            .replace('__INTRO__', read(a.intro))
            .replace('__FOOTER__', read(a.footer))
            .replace('__TITLE__', a.title))

    screens = pathlib.Path(a.screens).read_text()
    n_screens = screens.count("id: '")
    pathlib.Path(a.out).write_text(html)
    print(f"wrote {a.out}: {len(html)} bytes, {n_screens} screens, "
          f"{count // a.glyph_bytes} glyphs (0x{first:02X}..0x{last:02X}) "
          f"from {a.font_symbol}")
    for placeholder in ('__STYLE__', '__OLED__', '__RENDER__', '__SCREENS__',
                        '__FONT__', '__FONT_FIRST__', '__TITLE__'):
        if placeholder in html:
            raise SystemExit(f"placeholder {placeholder} was not substituted")


if __name__ == '__main__':
    main()
