const W = 128, H = 64;
const FONT_FIRST = __FONT_FIRST__, FONT_LAST = __FONT_LAST__, GLYPH_BYTES = __GLYPH_BYTES__;

class Oled {
  constructor(){ this.buf = new Uint8Array(W * H / 8); }
  clear(){ this.buf.fill(0); }
  drawText(x, page, text, invert){
    if (page >= H/8) return;
    const mask = invert ? 0xFF : 0x00;
    let col = Math.min(x, W-1);
    for (const ch of String(text)){
      if (col + 6 > W) break;
      let c = ch.charCodeAt(0);
      if (c < FONT_FIRST || c > FONT_LAST) c = 63;
      const g = (c - FONT_FIRST) * GLYPH_BYTES, base = page * W + col;
      for (let i = 0; i < 5; i++) this.buf[base + i] = FONT[g + i] ^ mask;
      this.buf[base + 5] = mask;
      col += 6;
    }
  }
  drawText2x(x, page, text, invert){
    if (page + 1 >= H/8) return;
    const mask = invert ? 0xFF : 0x00;
    let col = Math.min(x, W-1);
    for (const ch of String(text)){
      if (col + 12 > W) break;
      let c = ch.charCodeAt(0);
      if (c < FONT_FIRST || c > FONT_LAST) c = 63;
      const g = (c - FONT_FIRST) * GLYPH_BYTES;
      for (let i = 0; i < 5; i++){
        let doubled = 0;
        const b = FONT[g + i];
        for (let bit = 0; bit < 7; bit++) if (b & (1 << bit)) doubled |= (3 << (bit * 2));
        const lo = (doubled & 0xFF) ^ mask, hi = ((doubled >> 8) & 0xFF) ^ mask;
        const b0 = page * W + col, b1 = (page + 1) * W + col;
        this.buf[b0] = lo; this.buf[b0+1] = lo; this.buf[b1] = hi; this.buf[b1+1] = hi;
        col += 2;
      }
      const b0 = page * W + col, b1 = (page + 1) * W + col;
      this.buf[b0] = mask; this.buf[b0+1] = mask; this.buf[b1] = mask; this.buf[b1+1] = mask;
      col += 2;
    }
  }
  hline(x0, x1, y){
    if (y >= H || x0 >= W) return;
    if (x0 > x1){ const t = x0; x0 = x1; x1 = t; }
    if (x1 >= W) x1 = W - 1;
    const page = y >> 3, bit = 1 << (y & 7);
    for (let c = x0; c <= x1; c++) this.buf[page * W + c] |= bit;
  }
  vline(x, y0, y1){
    if (x >= W || y0 >= H) return;
    if (y0 > y1){ const t = y0; y0 = y1; y1 = t; }
    if (y1 >= H) y1 = H - 1;
    for (let y = y0; y <= y1; y++) this.buf[(y >> 3) * W + x] |= (1 << (y & 7));
  }
  fillRect(x, y, w, h, on){
    if (on === undefined) on = true;
    if (!w || !h || x >= W || y >= H) return;
    let x1 = x + w - 1, y1 = y + h - 1;
    if (x1 >= W) x1 = W - 1;
    if (y1 >= H) y1 = H - 1;
    for (let yy = y; yy <= y1; yy++){
      const page = yy >> 3, bit = 1 << (yy & 7);
      for (let xx = x; xx <= x1; xx++){
        if (on) this.buf[page * W + xx] |= bit; else this.buf[page * W + xx] &= ~bit;
      }
    }
  }
  fillLine(page, value){ const v = value === undefined ? 0xFF : value; this.buf.fill(v, page * W, page * W + W); }
  px(x, y){ return (this.buf[(y >> 3) * W + x] >> (y & 7)) & 1; }
}

// --- production_render.cpp helpers, ported verbatim -------------------------
function pair(o, page, left, right, inv){
  const line = new Array(21).fill(' ');
  const rw = Math.min(String(right).length, 20);
  const lw = Math.min(String(left).length, 21 - rw - (rw ? 1 : 0));
  for (let i = 0; i < lw; i++) line[i] = left[i];
  for (let i = 0; i < rw; i++) line[21 - rw + i] = right[i];
  if (inv) o.fillLine(page);
  o.drawText(0, page, line.join(''), inv);
}
function center(o, page, s){ o.drawText(Math.floor((128 - String(s).length * 6) / 2), page, s); }
function registerLine(o, page, tokens){
  const widths = tokens.map(t => t.length * 6);
  const total = widths.reduce((a, b) => a + b, 0);
  const gap = Math.max(0, Math.floor((128 - total) / 3));
  let x = 0;
  tokens.forEach((t, i) => {
    const at = i === 3 ? 128 - widths[3] : x;
    o.drawText(Math.max(0, Math.min(127, at)), page, t);
    x = at + widths[i] + gap;
  });
}
function box(o, x0, y0, x1, y1){ o.hline(x0, x1, y0); o.hline(x0, x1, y1); o.vline(x0, y0, y1); o.vline(x1, y0, y1); }
function meter(o, x, y, w, h, frac){
  box(o, x, y, x + w, y + h);
  const inner = Math.round((w - 3) * Math.max(0, Math.min(1, frac)));
  if (inner > 0) o.fillRect(x + 2, y + 2, inner, h - 3);
}
const REG = ['O:+0', 'I:R', 'M:Maj', 'R:C'];
function cursorAt(o, page, x, text){
  o.fillRect(Math.max(0, x - 1), page * 8, text.length * 6 + 1, 8);
  o.drawText(x, page, text, true);
}
function listScreen(o, title, rows, selected){
  const total = rows.length, visible = 7;
  const start = Math.max(0, Math.min(selected - 2, Math.max(0, total - visible)));
  pair(o, 0, title, (selected + 1) + '/' + total, true);
  for (let i = start; i < total && i < start + visible; i++)
    pair(o, i - start + 1, rows[i][0], rows[i][1], i === selected);
}
function valueEditor(o, title, label, value, frac, footL, footR){
  pair(o, 0, title, '', true);
  center(o, 1, label);
  o.drawText2x(Math.floor((128 - value.length * 12) / 2), 3, value);
  o.hline(6, 121, 46);
  const pos = 6 + Math.round(115 * Math.max(0, Math.min(1, frac)));
  o.fillRect(pos - 2, 44, 5, 4);
  pair(o, 6, footL[0], footL[1]);
  pair(o, 7, footR[0], footR[1]);
}
// A 3x3 compass. cells[] is indexed by the firmware's direction convention
// (0=E,1=NE,2=N,3=NW,4=W,5=SW,6=S,7=SE); the centre is a dot, not a label.
function compass(o, cells, active){
  const w = Math.max.apply(null, cells.filter(Boolean).map(c => c.length));
  const px = w * 6;
  const X = [2, Math.floor((128 - px) / 2), 128 - px];
  const grid = [[3, 2, 1], [4, -1, 0], [5, 6, 7]];
  for (let r = 0; r < 3; r++) for (let c = 0; c < 3; c++){
    const dir = grid[r][c], page = 2 + r;
    if (dir < 0){ o.fillRect(X[1] + Math.floor(px / 2) - 1, page * 8 + 3, 3, 2); continue; }
    const label = cells[dir] || '';
    if (!label) continue;
    const tx = X[c] + Math.floor((px - label.length * 6) / 2);
    if (dir === active){
      o.fillRect(Math.max(0, tx - 2), page * 8, label.length * 6 + 3, 8);
      o.drawText(tx, page, label, true);
    } else o.drawText(tx, page, label);
  }
}
function cursorRight(o, page, text){ cursorAt(o, page, (21 - text.length) * 6, text); }
function spread(o, page, tokens, sel){
  const n = tokens.length;
  const widths = tokens.map(t => t.length * 6);
  const total = widths.reduce((a, b) => a + b, 0);
  const gap = n > 1 ? Math.max(0, Math.floor((128 - total) / (n - 1))) : 0;
  let x = 0;
  tokens.forEach((t, i) => {
    const at = i === n - 1 ? 128 - widths[i] : x;
    const px = Math.max(0, Math.min(127, at));
    if (i === sel) cursorAt(o, page, px, t); else o.drawText(px, page, t);
    x = at + widths[i] + gap;
  });
}
function registerLineSel(o, page, tokens, sel){ spread(o, page, tokens, sel); }
