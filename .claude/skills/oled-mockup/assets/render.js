// --- Rendering -------------------------------------------------------------
let scale = 4, pageGrid = false, cellGrid = false, pxGrid = true;
const sheet = document.getElementById('sheet');
const canvases = [];

function cssVar(n){ return getComputedStyle(document.documentElement).getPropertyValue(n).trim(); }

function paint(cv, screen){
  const s = scale;
  cv.width = W * s; cv.height = H * s;
  cv.style.width = (W * s) + 'px'; cv.style.height = (H * s) + 'px';
  const g = cv.getContext('2d');
  g.fillStyle = cssVar('--oled-off') || '#06080b';
  g.fillRect(0, 0, W * s, H * s);
  const o = new Oled(); screen.draw(o);
  g.fillStyle = cssVar('--oled-on') || '#c8ecf5';
  const inset = (pxGrid && s >= 4) ? 1 : 0;
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++)
    if (o.px(x, y)) g.fillRect(x * s, y * s, s - inset, s - inset);
  if (cellGrid){
    g.fillStyle = 'rgba(200,236,245,.20)';
    for (let x = 6; x < W; x += 6) g.fillRect(x * s, 0, 1, H * s);
  }
  if (pageGrid){
    g.fillStyle = 'rgba(255,130,70,.55)';
    for (let y = 8; y < H; y += 8) g.fillRect(0, y * s, W * s, 1);
  }
}

screens.forEach(sc => {
  const card = document.createElement('div');
  card.className = 'shot';
  const left = document.createElement('div');
  left.className = 'screen';
  const cv = document.createElement('canvas');
  cv.setAttribute('role', 'img');
  cv.setAttribute('aria-label', sc.title + ' screen, ' + sc.badge);
  left.appendChild(cv);
  const right = document.createElement('div');
  right.className = 'notes';
  right.innerHTML = '<h2>' + sc.title +
    ' <span class="tag">' + sc.badge + '</span>' +
    ' <span class="tag ' + (sc.tag === 'new' ? 'new' : 'same') + '">' + sc.tag + '</span></h2>' +
    '<p>' + sc.desc + '</p>' +
    '<dl class="calls">' + sc.calls.map(c => '<dt>' + c[0] + '</dt><dd>' + c[1] + '</dd>').join('') + '</dl>';
  card.appendChild(left); card.appendChild(right);
  sheet.appendChild(card);
  canvases.push([cv, sc]);
});

function renderAll(){ canvases.forEach(([cv, sc]) => paint(cv, sc)); }

document.getElementById('zoom').addEventListener('click', e => {
  const b = e.target.closest('button'); if (!b) return;
  scale = Number(b.dataset.s);
  document.querySelectorAll('#zoom button').forEach(x => x.setAttribute('aria-pressed', x === b ? 'true' : 'false'));
  renderAll();
});
document.getElementById('pageGrid').addEventListener('change', e => { pageGrid = e.target.checked; renderAll(); });
document.getElementById('cellGrid').addEventListener('change', e => { cellGrid = e.target.checked; renderAll(); });
document.getElementById('pxGrid').addEventListener('change', e => { pxGrid = e.target.checked; renderAll(); });
matchMedia('(prefers-color-scheme: dark)').addEventListener('change', renderAll);

renderAll();
