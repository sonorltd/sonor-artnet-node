// SONOR Art-Net Node — wiring schematic renderer. Shared by the Pages site (index.html) and the on-node page
// (inlined into web/node-page.html by build.sh → esp_page.h). Pure: drawSchematic(board) → SVG markup string.
const NET = {  // wire colours by net
  '5V':'#e5484d', '3V3':'#f0883e', 'GND':'#9aa4b2', 'DI':'#6cb6e6', 'DE':'#46c37b', 'LED':'#f5d05c',
  'A':'#f5d05c', 'B':'#F4F1EC', 'SCK':'#c084fc', 'MISO':'#f472b6', 'MOSI':'#a78bfa', 'CS':'#60a5fa', 'INT':'#a3e635',
  'U_TX':'#7fb4f0', 'U_RX':'#7fb4f0', 'U_BOOT':'#9aa4b2'
};
function drawSchematic(b) {
  const W = 880, PIN = 26;
  const S = [];
  const txt = (x, y, s, o = {}) => S.push(`<text x="${x}" y="${y}" fill="${o.fill || '#F4F1EC'}" font-size="${o.size || 12}" font-family="ui-monospace,Menlo,Consolas,monospace" text-anchor="${o.anchor || 'start'}" font-weight="${o.w || 400}" dominant-baseline="middle">${s}</text>`);
  const box = (x, y, w, h, title) => { S.push(`<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="8" fill="#171410" stroke="rgba(173,153,120,.35)" stroke-width="1.5"/>`); txt(x + w / 2, y + 16, title, {anchor:'middle', w:700, size:12.5}); };
  const wire = (pts, net) => S.push(`<polyline points="${pts.map(p => p.join(',')).join(' ')}" fill="none" stroke="${NET[net] || '#fff'}" stroke-width="2.2" stroke-linejoin="round" stroke-linecap="round"/>`);
  const dot = (x, y, net) => S.push(`<circle cx="${x}" cy="${y}" r="3" fill="${NET[net] || '#fff'}"/>`);

  // MCU block (left)
  const mx = 30, my = 56, mw = 190, mh = 34 + b.mcu.pins.length * PIN;
  box(mx, my, mw, mh, b.mcu.title);
  const mcuPin = {};
  b.mcu.pins.forEach(([label, net], i) => {
    const y = my + 36 + i * PIN + PIN / 2;
    txt(mx + mw - 10, y, label, {anchor:'end', fill:'#c9d1d9'});
    S.push(`<line x1="${mx + mw}" y1="${y}" x2="${mx + mw + 14}" y2="${y}" stroke="#5a5145" stroke-width="2"/>`);
    mcuPin[net] = [mx + mw + 14, y];
  });

  // MAX485 block (middle)
  const dx = 470, dy = 56, dw = 150, maxPins = [['VCC', b.power === '3V3' ? '3V3' : '5V'],['GND','GND'],['DI','DI'],['DE','DE'],['RE','DE2'],['RO',null]];
  const dh = 34 + maxPins.length * PIN;
  box(dx, dy, dw, dh, b.max3485 ? 'MAX3485 module (3.3 V)' : 'MAX485 module');
  const maxPin = {};
  maxPins.forEach(([label, net], i) => {
    const y = dy + 36 + i * PIN + PIN / 2;
    txt(dx + 10, y, label, {fill:'#e6e0d6'});
    S.push(`<line x1="${dx - 14}" y1="${y}" x2="${dx}" y2="${y}" stroke="#5a5145" stroke-width="2"/>`);
    if (net) maxPin[net] = [dx - 14, y];
  });
  const ay = dy + 36 + PIN / 2 + PIN * 1, by = ay + PIN * 2;   // A, B on the right side
  txt(dx + dw - 10, ay, 'A', {anchor:'end', fill:'#c9d1d9'}); txt(dx + dw - 10, by, 'B', {anchor:'end', fill:'#c9d1d9'});
  S.push(`<line x1="${dx + dw}" y1="${ay}" x2="${dx + dw + 14}" y2="${ay}" stroke="#5a5145" stroke-width="2"/><line x1="${dx + dw}" y1="${by}" x2="${dx + dw + 14}" y2="${by}" stroke="#5a5145" stroke-width="2"/>`);
  // DE–RE link
  wire([maxPin.DE, [maxPin.DE[0] - 10, maxPin.DE[1]], [maxPin.DE[0] - 10, maxPin.DE2[1]], maxPin.DE2], 'DE');
  txt(dx + 46, maxPin.DE[1] + 13, 'tie DE + RE', {fill:'#6a6156', size:10});

  // XLR female (right) — drawn as a connector block, pins 3 / 2 / 1 top to bottom
  const xx = 760, xw = 100, xpins = [['3  Data +','A'],['2  Data −','B'],['1  GND','GND']];
  const xh = 34 + xpins.length * PIN, xy = dy;
  box(xx, xy, xw, xh, 'XLR female');
  const xp = {};
  xpins.forEach(([label, net], i) => {
    const y = xy + 36 + i * PIN + PIN / 2;
    txt(xx + 10, y, label, {fill:'#e6e0d6'});
    S.push(`<line x1="${xx - 14}" y1="${y}" x2="${xx}" y2="${y}" stroke="#5a5145" stroke-width="2"/>`);
    xp[net] = [xx - 14, y];
  });
  // little face icon under the block so the pin positions on a real socket are obvious
  const fcx = xx + xw / 2, fcy = xy + xh + 30;
  S.push(`<circle cx="${fcx}" cy="${fcy}" r="20" fill="#171410" stroke="rgba(173,153,120,.35)" stroke-width="1.2"/>`);
  [[fcx - 10, fcy - 6, '2'], [fcx + 10, fcy - 6, '3'], [fcx, fcy + 10, '1']].forEach(([x, y, n]) => { S.push(`<circle cx="${x}" cy="${y}" r="4" fill="#0d0b07" stroke="#8f8574" stroke-width="1"/>`); txt(x, y + 0.5, n, {anchor:'middle', size:7, fill:'#c9d1d9'}); });
  txt(fcx, fcy + 34, 'DMX out · face view', {anchor:'middle', fill:'#6a6156', size:10});
  // A → pin 3, B → pin 2 (two short bus columns between the MAX485 and the XLR)
  const xb = dx + dw + 40;
  wire([[dx + dw + 14, ay], [xb, ay], [xb, xp.A[1]], xp.A], 'A'); dot(xb, ay, 'A'); dot(xb, xp.A[1], 'A');
  wire([[dx + dw + 14, by], [xb + 16, by], [xb + 16, xp.B[1]], xp.B], 'B'); dot(xb + 16, by, 'B'); dot(xb + 16, xp.B[1], 'B');

  // buses between MCU and MAX485: one column per net
  const busX0 = mx + mw + 40, order = [b.power === '3V3' ? '3V3' : '5V','GND','DI','DE'];
  let bus = {};
  order.forEach((net, i) => bus[net] = busX0 + i * 16);
  const routeTo = (net, dst) => { const s = mcuPin[net]; if (!s || !dst) return; wire([s, [bus[net], s[1]], [bus[net], dst[1]], dst], net); dot(bus[net], s[1], net); dot(bus[net], dst[1], net); };
  routeTo(b.power === '3V3' ? '3V3' : '5V', maxPin[b.power === '3V3' ? '3V3' : '5V']); routeTo('GND', maxPin.GND); routeTo('DI', maxPin.DI); routeTo('DE', maxPin.DE);
  // GND onward to XLR pin 1 — over the top of the blocks so it never crosses a module
  const topY = my - 16;
  wire([[bus.GND, maxPin.GND[1]], [bus.GND, topY], [xb + 32, topY], [xb + 32, xp.GND[1]], xp.GND], 'GND');
  dot(bus.GND, maxPin.GND[1], 'GND');
  const gy = my + mh + 24;   // ground rail under the MCU for the LED branch

  // optional LED branch
  if (b.led && mcuPin.LED) {
    const [lx, ly] = mcuPin.LED, x1 = lx + 16;
    wire([[lx, ly], [x1, ly], [x1, ly + 22]], 'LED');
    S.push(`<rect x="${x1 - 8}" y="${ly + 22}" width="16" height="30" fill="#171410" stroke="#f5d05c" stroke-width="1.6"/>`); txt(x1 + 14, ly + 37, '330 Ω', {fill:'#8f8574', size:10});
    wire([[x1, ly + 52], [x1, ly + 64]], 'LED');
    S.push(`<polygon points="${x1 - 8},${ly + 64} ${x1 + 8},${ly + 64} ${x1},${ly + 78}" fill="#171410" stroke="#f5d05c" stroke-width="1.6"/><line x1="${x1 - 8}" y1="${ly + 78}" x2="${x1 + 8}" y2="${ly + 78}" stroke="#f5d05c" stroke-width="1.6"/>`); txt(x1 + 14, ly + 71, 'LED', {fill:'#8f8574', size:10});
    wire([[x1, ly + 78], [x1, gy], [bus.GND, gy], [bus.GND, maxPin.GND[1]]], 'GND'); dot(x1, gy, 'GND');
  }

  // boot strap (WT32-ETH01 IO0): dashed lead to a ground symbol
  if (mcuPin.U_BOOT) {
    const [bx0, by0] = mcuPin.U_BOOT;
    S.push(`<polyline points="${bx0},${by0} ${bx0 + 24},${by0} ${bx0 + 24},${by0 + 18}" fill="none" stroke="#9aa4b2" stroke-width="2" stroke-dasharray="4 3"/>`);
    [[10, 18], [6, 22], [2, 26]].forEach(([hw, yo]) => S.push(`<line x1="${bx0 + 24 - hw}" y1="${by0 + yo}" x2="${bx0 + 24 + hw}" y2="${by0 + yo}" stroke="#9aa4b2" stroke-width="2"/>`));
    txt(bx0 + 40, by0 + 8, 'to GND while powering up = download mode', {fill:'#8f8574', size:10});
  }

  // extra modules (W5500, USB-serial) below the MAX485
  let ey = dy + dh + 50;
  b.modules.forEach(mod => {
    const eh = 34 + mod.pins.length * PIN;
    box(dx, ey, dw + 40, eh, mod.title);
    const nets = mod.pins.map(p => p[1]);
    // fresh bus columns to the right of the main ones
    let bx = busX0 + 4 * 16 + 20; const b2 = {};
    nets.forEach((n, i) => b2[n] = bx + i * 14);
    mod.pins.forEach(([label, net], i) => {
      const y = ey + 36 + i * PIN + PIN / 2;
      txt(dx + 10, y, label, {fill:'#e6e0d6'});
      S.push(`<line x1="${dx - 14}" y1="${y}" x2="${dx}" y2="${y}" stroke="#5a5145" stroke-width="2"/>`);
      const s = mcuPin[net]; if (!s) return;
      const col = (net === 'GND' || net === '5V') ? bus[net] : b2[net];
      wire([s, [col, s[1]], [col, y], [dx - 14, y]], net); dot(col, s[1], net); dot(col, y, net);
    });
    ey += eh + 30;
  });
  const H = Math.max(ey, gy + 40, my + mh + 100, xy + xh + 90);
  // legend
  const legendNets = [...new Set([b.power === '3V3' ? '3V3' : '5V','GND','DI','DE', ...(b.led ? ['LED'] : []), 'A','B', ...b.modules.flatMap(m => m.pins.map(p => p[1]))])].filter(n => NET[n]);
  let lx = 30; legendNets.forEach(n => { S.push(`<line x1="${lx}" y1="${H - 14}" x2="${lx + 18}" y2="${H - 14}" stroke="${NET[n]}" stroke-width="3"/>`); txt(lx + 24, H - 14, n.replace('U_', 'USB-'), {fill:'#8f8574', size:10}); lx += 24 + n.length * 7 + 22; });
  return `<svg viewBox="0 0 ${W} ${H}" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="Wiring schematic for ${b.name}">${S.join('')}</svg>`;
}

