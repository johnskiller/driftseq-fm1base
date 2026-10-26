/* tools/header_pill.mjs — the one-line header with a 1 (not "TRACK 1") in a
 * rounded pill. Renders real glyphs at the real sizes so the pill can be judged.
 *
 * Variations across the sheet: filled pill vs outlined, 1 vs 2 digits, with and
 * without the type glyph, at FONT_S (11px caps, the size that reads as "醒目").
 */
import fs from 'node:fs';
import zlib from 'node:zlib';

const src = fs.readFileSync(new URL('../build/gen/felucca_font.h', import.meta.url), 'utf8');
function load(n) {
    const a = (s) => {
        const m = new RegExp(`FONT_${n}_${s}\\[[^\\]]*\\]\\s*=\\s*\\{([^}]*)\\}`, 's').exec(src);
        return m[1].split(',').map((t) => t.trim()).filter(Boolean).map((t) => parseInt(t, 0));
    };
    const d = new RegExp(`felucca_font_t FONT_${n} = \\{([^}]*)\\};`).exec(src)[1].split(',').map((t) => parseInt(t.trim(), 10));
    return { h: d[0], pad: d[1], first: d[2], n: d[3], adv: a('ADV'), bw: a('BW'), off: a('OFF'), data: a('DATA') };
}
const FONTS = { T: load('T'), XS: load('XS'), S: load('S'), B: load('B') };

const W = 240, H = 400, SCALE = 4;
const swap16 = (c) => ((c & 0xff) << 8) | ((c >> 8) & 0xff);
const rgb565 = (r, g, b) => swap16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
const toRGB = (c0) => { const c = swap16(c0);
    return [((c >> 11) & 31) * 255 / 31 | 0, ((c >> 5) & 63) * 255 / 63 | 0, (c & 31) * 255 / 31 | 0]; };
const fb = new Uint16Array(W * H);
const px = (x, y, c) => { if (x >= 0 && x < W && y >= 0 && y < H) fb[y * W + x] = c; };
const rect = (x, y, w, h, c) => { for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) px(x + i, y + j, c); };

const TB_BG = rgb565(24, 24, 32), TB_TX = rgb565(217, 217, 232), DIM = rgb565(122, 122, 136);
const SEP = rgb565(42, 42, 52), GREEN = rgb565(95, 224, 138);
const LABEL = rgb565(255, 196, 90), RED = rgb565(230, 90, 90), NOTE = rgb565(105, 105, 118);
const HUE = [rgb565(255, 138, 128), rgb565(255, 196, 90), rgb565(180, 230, 120),
             rgb565(110, 220, 200), rgb565(120, 190, 250), rgb565(180, 160, 250)];

function text(f, x, y, s, c) {
    const [r, g, b] = toRGB(c);
    for (const ch of s) {
        const k = ch.charCodeAt(0) - f.first, w = f.bw[k], bpr = (w + 1) >> 1;
        for (let gy = 0; gy < f.h; gy++) for (let gx = 0; gx < w; gx++) {
            const byte = f.data[f.off[k] + gy * bpr + (gx >> 1)];
            const a = (gx & 1) ? (byte & 15) : (byte >> 4);
            if (!a) continue;
            const X = x - f.pad + gx, Y = y + gy;
            if (X < 0 || X >= W || Y < 0 || Y >= H) continue;
            if (a === 15) { fb[Y * W + X] = c; continue; }
            const [br, bg2, bb] = toRGB(fb[Y * W + X]);
            const mix = (p, q) => (p * a + q * (15 - a)) / 15 | 0;
            fb[Y * W + X] = rgb565(mix(r, br), mix(g, bg2), mix(b, bb));
        }
        x += f.adv[ch.charCodeAt(0) - f.first];
    }
    return x;
}
const widthOf = (f, s) => [...s].reduce((a, c) => a + f.adv[c.charCodeAt(0) - f.first], 0);

function capInfo(f) {
    const k = 'A'.charCodeAt(0) - f.first, w = f.bw[k], bpr = (w + 1) >> 1;
    let top = -1, bot = -1;
    for (let gy = 0; gy < f.h; gy++) for (let gx = 0; gx < w; gx++) {
        const byte = f.data[f.off[k] + gy * bpr + (gx >> 1)];
        if (((gx & 1) ? (byte & 15) : (byte >> 4)) > 0) { if (top < 0) top = gy; bot = gy; break; }
    }
    return { top, bot, h: bot - top + 1 };
}

/* a rounded rectangle, 1px corner cut -- the smallest rounding the canvas allows */
function roundRect(x, y, w, h, c, cut = 2) {
    rect(x + cut, y, w - cut * 2, h, c);
    rect(x, y + cut, cut, h - cut * 2, c);
    rect(x + w - cut, y + cut, cut, h - cut * 2, c);
    /* the corners themselves: a 2px stair */
    for (let i = 0; i < cut; i++) {
        rect(x + i, y + cut - i - 1, 1, 1, c);
        rect(x + w - 1 - i, y + cut - i - 1, 1, 1, c);
        rect(x + i, y + h - cut + i, 1, 1, c);
        rect(x + w - 1 - i, y + h - cut + i, 1, 1, c);
    }
}

function glyph(x, y, n, type, c) {
    if (type === 'drum') {
        rect(x, y, n, n, c);
        px(x, y, TB_BG); px(x + n - 1, y, TB_BG); px(x, y + n - 1, TB_BG); px(x + n - 1, y + n - 1, TB_BG);
    } else {
        const bar = Math.max(2, Math.round(n / 4)), gap = Math.max(1, Math.round(n / 8));
        const hs = [Math.round(n * 0.45), Math.round(n * 0.75), n];
        for (let i = 0; i < 3; i++) rect(x + i * (bar + gap), y + n - hs[i], bar, hs[i], c);
    }
}

/* ---------------------------------------------------------------- the bar */
/* opts: font, pill style, digits, showGlyph, fields[] */
function drawBar(y, o) {
    const f = FONTS[o.font || 'S'], cap = capInfo(f);
    const ty = y + Math.round((24 - cap.h) / 2) - cap.top;
    rect(0, y, W, 24, TB_BG);
    rect(0, y + 23, W, 1, SEP);

    const hue = HUE[o.track || 0];
    const pad = 5, numW = Math.max(widthOf(f, '0'), widthOf(f, o.num || '1'));
    const pillW = numW + pad * 2, pillH = cap.h + 8;
    const pillY = y + Math.round((24 - pillH) / 2);

    let x = 6;
    if (o.pill === 'filled') {
        roundRect(x, pillY, pillW, pillH, hue);
        text(f, x + pad, ty, o.num || '1', TB_BG);
    } else {
        /* outlined: a 1px ring in the track colour, number in the text colour */
        roundRect(x, pillY, pillW, pillH, hue);
        roundRect(x + 1, pillY + 1, pillW - 2, pillH - 2, TB_BG);
        text(f, x + pad, ty, o.num || '1', hue);
    }
    x += pillW + 8;

    if (o.showGlyph) { glyph(x, y + Math.round((24 - cap.h) / 2), cap.h, o.type || 'mel', DIM); x += cap.h + 8; }

    for (let i = 0; i < o.fields.length; i++) {
        x = text(f, x, ty, o.fields[i], i === 0 ? TB_TX : DIM);
        x += 8;
    }

    /* right cluster */
    const dotsW = 5 * 6 - 1;
    let rx = 234 - dotsW;
    for (let i = 0; i < 5; i++) {
        const dx = rx + i * 6, dy = ty + cap.top + Math.round((cap.h - 3) / 2);
        if (i === (o.page ?? 1)) rect(dx, dy, 3, 3, TB_TX);
        else { rect(dx, dy, 3, 3, SEP); px(dx + 1, dy + 1, TB_BG); }
    }
    if (o.loop !== undefined) {
        const lw = widthOf(f, 'S' + o.loop);
        text(f, rx - 10 - lw, ty, 'S' + o.loop, GREEN);
    }
}

/* --------------------------------------------------------------- compose */
fb.fill(rgb565(10, 10, 13));
let y = 4;
const head = (s) => { text(FONTS.T, 6, y, s, LABEL); y += 13; };
const note = (s, c) => { text(FONTS.T, 6, y, s, c || NOTE); y += 12; };

head('ONE LINE, 24px, FONT_S (11px caps)');
const variants = [
    ['filled pill, 1',            { pill: 'filled',   num: '1', showGlyph: true,  fields: ['4 BAR', '16 STEPS'], loop: 1 }],
    ['filled pill, 16 (2 digits)', { pill: 'filled',  num: '16', showGlyph: true, fields: ['4 BAR', '16 STEPS'], loop: 1 }],
    ['outlined pill, 1',          { pill: 'outlined', num: '1', showGlyph: true,  fields: ['4 BAR', '16 STEPS'], loop: 1 }],
    ['filled, no glyph',          { pill: 'filled',   num: '1', showGlyph: false, fields: ['4 BAR', '16 STEPS', '120'], loop: 1 }],
    ['drum track (glyph=block)',  { pill: 'filled',   num: '1', showGlyph: true,  fields: ['KCK', '120 BPM'], type: 'drum', loop: 2 }],
];
for (const [label, o] of variants) {
    drawBar(y, o);
    text(FONTS.T, 6, y + 25, label, NOTE);
    y += 37;
}

y += 4;
rect(0, y, W, 1, rgb565(70, 70, 82)); y += 7;
head('FONT_T (8px) FOR SCALE, SAME LAYOUT');
drawBar(y, { font: 'T', pill: 'filled', num: '1', showGlyph: true, fields: ['4 BAR', '16 STEPS', '120 BPM'], loop: 1 });
y += 25;
text(FONTS.T, 6, y, 'what is on screen today, for comparison', NOTE);

/* ------------------------------------------------------------------ save */
const OW = W * SCALE, OH = y * SCALE;
const out = Buffer.alloc(OH * (OW * 3 + 1));
let p = 0;
for (let yy = 0; yy < OH; yy++) {
    out[p++] = 0;
    for (let xx = 0; xx < OW; xx++) {
        const [r, g, b] = toRGB(fb[(yy / SCALE | 0) * W + (xx / SCALE | 0)]);
        out[p++] = r; out[p++] = g; out[p++] = b;
    }
}
function crc32(buf) { let c, crc = 0xffffffff;
    for (let n = 0; n < buf.length; n++) { c = (crc ^ buf[n]) & 0xff;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; crc = (crc >>> 8) ^ c; }
    return (crc ^ 0xffffffff) >>> 0; }
const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length);
    const td = Buffer.concat([Buffer.from(t, 'ascii'), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc32(td));
    return Buffer.concat([l, td, c]); };
const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(OW, 0); ihdr.writeUInt32BE(OH, 4); ihdr[8] = 8; ihdr[9] = 2;
const png = Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(out)), chunk('IEND', Buffer.alloc(0))]);
const dest = process.argv[2] || '/tmp/header_pill.png';
fs.writeFileSync(dest, png);
console.log(`${OW}x${OH} -> ${dest} (${png.length} B)`);
