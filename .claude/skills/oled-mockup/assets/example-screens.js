// One entry per screen. `draw(o)` issues the same calls the firmware's
// renderer would; everything else is the caption beside the panel.
const screens = [
  {
    id: 'play', title: 'PLAY', badge: 'home', tag: 'kept',
    desc: "What the panel shows at rest. Row 0 is inverted via <code>fill_line</code> plus knocked-out text, which is why two pixels stay lit at the right edge: the fill covers 128 px and 21 characters cover 126.",
    calls: [
      ['pair(0, inv)', 'title row, value flushed right'],
      ['draw_text_2x(1)', 'large value, two pages tall'],
      ['spread(7)', 'status tokens across the full width'],
    ],
    draw(o) {
      pair(o, 0, 'C Major', 'I  T1', true);
      o.drawText2x(Math.floor((128 - 5 * 12) / 2), 1, 'Fmaj9');
      center(o, 3, 'F-A-C-E-G');
      spread(o, 7, ['O:+0', 'I:R', 'M:Maj', 'R:C'], -1);
    },
  },
];
