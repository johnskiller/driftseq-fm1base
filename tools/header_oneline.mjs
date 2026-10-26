/* tools/header_oneline.mjs — one 24px bar, one line. Which type size, and what
 * does the size cost in fields?
 *
 * Renders with the four real fonts out of build/gen/felucca_font.h, so the widths
 * are the widths the build will produce. Rows marked OVERFLOW genuinely do not
 * fit in 240px; that is the answer to "make it more striking", priced.
 */
import fs from 'node:fs';
import zlib from 'node:zlib';

/* ------------------------------------------------------------------- fonts */
const src = fs.readFileSync(new URL('../build/gen/felucca_font.h', import.meta.url), 'utf8');
function load(name) {
    const arr = (sfx) => {
        const m = new RegExp(`FONT_${name}_${sfx}\\[[^\\]]*\\]\\s*=\\s*\\{([^}]*)\\}`, 's').exec(src);
        return m[1].split(',').map((t) => t.trim()).filter(Boolean).map((t) => parseInt(t, 0));
    };
    const d = new RegExp(`felucca_font_t FONT_${name} = \\{([^}]*)\\};`).exec(src)[1].split(',').map((t) => parseInt(t.trim(), 10));
    return { h: d[0], pad: d[1], first: d[2], n: d[3], adv: arr('ADV'), bw: arr('BW'), off: arr('OFF'), data: arr('DATA') };
}
const FONTS = { T: load('T'), XS: load('XS'), S: load('S'), B: load('B') };

/* ------------------------------------------------------- sheet framebuffer */
const W = 240, H = 460, SCALE = 3;
const swap16 = (c) => ((c & 0xff) << 8) | ((c >> 8) & 0xff);
const rgb565 = (r, g, b) => swap16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
const toRGB = (c0) => { const c = swap16(c0);
    return [((c >> 11) & 31) * 255 / 31 | 0, ((c >> 5) & 63) * 255 / 63 | 0, (c & 31) * 255 / 31 | 0]; };
const fb = new Uint16Array(W * H);
const px = (x, y, c) => { if (x >= 0 && x < W && y >= 0 && y < H) fb[y * W + x] = c; };
const rect = (x, y, w, h, c) => { for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) px(x + i, y + j, c); };

const TB_BG = rgb565(24, 24, 32), TB_TX = rgb565(217, 217, 232), DIM = rgb565(122, 122, 136);
const SEP = rgb565(42, 42, 52), GREEN = rgb565(95, 224, 138), MUTE = rgb565(90, 90, 98);
const LABEL = rgb565(255, 196, 90), RED = rgb565(230, 90, 90);
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
        x += f.adv[k];
    }
    return x;
}
const widthOf = (f, s) => [...s].reduce((a, c) => a + f.adv[c.charCodeAt(0) - f.first], 0);

/* ink height of a cap, so the bar can centre it */
function capHeight(f) {
    const k = 'A'.charCodeAt(0) - f.first, w = f.bw[k], bpr = (w + 1) >> 1;
    let top = -1, bot = -1;
    for (let gy = 0; gy < f.h; gy++) for (let gx = 0; gx < w; gx++) {
        const byte = f.data[f.off[k] + gy * bpr + (gx >> 1)];
        if (((gx & 1) ? (byte & 15) : (byte >> 4)) > 0) { if (top < 0) top = gy; bot = gy; break; }
    }
    return { top, bot, h: bot - top + 1 };
}

/* a solid block glyph for drums, a stepped contour for melodic, at size n */
function glyph(x, y, n, type, c) {
    const s = n / 8;
    if (type === 'drum') {
        rect(x, y, n, n, c);
        /* knock out the four corners a little so it does not read as a plain square */
        rect(x, y, 2, 2, c); 
    } else {
        /* three ascending steps: a small bar chart */
        const bar = Math.max(2, Math.round(n / 4)), gap = Math.max(1, Math.round(s));
        const hs = [Math.round(n * 0.45), Math.round(n * 0.72), n];
        for (let i = 0; i < 3; i++) rect(x + i * (bar + gap), y + n - hs[i], bar, hs[i], c);
    }
    /* one pixel of rounding on the drum block's corners */
    if (type === 'drum') { px(x, y, TB_BG); px(x + n - 1, y, TB_BG); px(x, y + n - 1, TB_BG); px(x + n - 1, y + n - 1, TB_BG); }
}

/* ------------------------------------------------------- layout candidates */
/* place a line of items across the bar; returns total width used */
function layout(f, y, cap, items, dots) {
    const LHUE = Math.round(cap.h * 1.15), LGLY = cap.h;
    let x = 6;
    rect(x, y + Math.round((24 - LHUE) / 2), LHUE, LHUE, HUE[0]);
    x += LHUE + 6;
    glyph(x, y + Math.round((24 - LGLY) / 2), LGLY, 'mel', TB_TX);
    x += LGLY + 8;
    for (const it of items) {
        if (!it.dim) { x = text(f, x, y, it.s, it.c || TB_TX); }
        else x = text(f, x, y, it.s, it.c || DIM);
        x += 8;
    }
    /* right cluster: loop marker + page dots, right-aligned to 234 */
    const dotsW = dots ? (5 * 6 - 1) : 0, loopW = widthOf(f, 'S1');
    let rx = 234 - dotsW;
    if (dots) for (let i = 0; i < 5; i++) {
        const dx = rx + i * 6, dy = y + Math.round((cap.h - 3) / 2);
        if (i === 1) rect(dx, dy, 3, 3, TB_TX);
        else { rect(dx, dy, 3, 3, SEP); rect(dx + 1, dy + 1, 1, 1, TB_BG); }
    }
    rx -= 8 + loopW;
    text(f, rx, y, 'S1', GREEN);
    /* the body ends at x; the right cluster starts at rx. Those two must not
     * touch -- reporting only "total > 240" misses a body that runs under the
     * cluster, which at the larger sizes it does. */
    return { bodyEnd: x, clusterStart: rx, overlap: x > rx };
}

/* --------------------------------------------------------------- compose */
fb.fill(rgb565(10, 10, 13));
let y = 4;

const head = (s) => { text(FONTS.T, 6, y, s, LABEL); y += 13; };
const note = (s, c) => { text(FONTS.T, 6, y, s, c || rgb565(105, 105, 118)); y += 12; };

head('ONE 24px BAR - the same fields at each type size');
const full = [{ s: 'TRACK 3', c: TB_TX }, { s: '4 BAR', c: DIM }, { s: '120 BPM', c: TB_TX }];
for (const name of ['T', 'XS', 'S', 'B']) {
    const f = FONTS[name], cap = capHeight(f);
    const ty = y + Math.round((24 - cap.h) / 2) - cap.top;
    rect(0, y, W, 24, TB_BG);
    rect(0, y + 23, W, 1, SEP);
    const r = layout(f, ty, cap, full, true);
    const over = r.overlap;
    text(FONTS.T, 6, y + 26, `${name}  caps ${cap.h}px   ${over ? `OVERLAP at ${r.bodyEnd}>${r.clusterStart}` : 'fits'}`, over ? RED : rgb565(120, 200, 140));
    y += 38;
}

y += 4;
rect(0, y, W, 1, rgb565(70, 70, 82)); y += 8;
head('THE SAME BAR, FIELDS TRIMMED TO FIT LARGE TYPE');
const cands = [
    ['S', [{ s: 'TRACK 3', c: TB_TX }, { s: '120 BPM', c: TB_TX }]],
    ['S', [{ s: 'TRACK 3', c: TB_TX }, { s: '4 BAR', c: DIM }]],
    ['XS', [{ s: 'TRACK 3', c: TB_TX }, { s: '4 BAR', c: DIM }, { s: '120 BPM', c: TB_TX }]],
    ['T', [{ s: 'TRACK 3', c: TB_TX }, { s: '4 BAR', c: DIM }, { s: '16 STEPS', c: DIM }, { s: '120 BPM', c: TB_TX }]],
];
for (const [name, items] of cands) {
    const f = FONTS[name], cap = capHeight(f);
    const ty = y + Math.round((24 - cap.h) / 2) - cap.top;
    rect(0, y, W, 24, TB_BG);
    rect(0, y + 23, W, 1, SEP);
    const r = layout(f, ty, cap, items, true);
    const over = r.overlap;
    text(FONTS.T, 6, y + 26, `${name}  ${items.map((i) => i.s).join(' / ')}  ${over ? `OVERLAP at ${r.bodyEnd}>${r.clusterStart}` : 'fits'}`, over ? RED : rgb565(120, 200, 140));
    y += 38;
}

/* ------------------------------------------------------------------- save */
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
const dest = process.argv[2] || '/tmp/header_oneline.png';
fs.writeFileSync(dest, png);
console.log(`${OW}x${OH} -> ${dest} (${png.length} B)`);
