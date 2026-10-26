/* tools/header_compare.mjs — one sheet: the header as it renders TODAY (real
 * pixels, captured from the built wasm) above the header as proposed.
 *
 * The point is that this is not a mock of either side. The top half is literally
 * the framebuffer the device draws now; the bottom half uses FONT_T's own bitmap
 * and advance table. Both are then scaled up, so what you see is what the panel
 * will show.
 */
import fs from 'node:fs';
import zlib from 'node:zlib';

/* ------------------------------------------------------------------ the font */
const src = fs.readFileSync(new URL('../build/gen/felucca_font.h', import.meta.url), 'utf8');
const grabArr = (name) => {
    const m = new RegExp(`${name}\\[[^\\]]*\\]\\s*=\\s*\\{([^}]*)\\}`, 's').exec(src);
    if (!m) throw new Error('missing ' + name);
    return m[1].split(',').map((t) => t.trim()).filter(Boolean).map((t) => parseInt(t, 0));
};
const T = { h: 12, pad: 2, first: 32, adv: grabArr('FONT_T_ADV'), bw: grabArr('FONT_T_BW'), off: grabArr('FONT_T_OFF'), data: grabArr('FONT_T_DATA') };
const gi = (ch) => Math.max(0, Math.min(T.bw.length - 1, ch - T.first));
const textWidth = (s) => [...s].reduce((a, c) => a + T.adv[gi(c.charCodeAt(0))], 0);

/* --------------------------------------------------------------- framebuffer */
const W = 240;
const SHEET_H = 400;
const fb = new Uint16Array(W * SHEET_H);
/* The panel's framebuffer is big-endian 565: gfx.c writes cv_px through swap16(), so
 * a uint16 read out of wasm memory has its bytes reversed. Everything in this file is
 * kept in THAT order -- rgb565() returns a swapped value and toRGB() accepts one -- so
 * a pixel captured from the wasm and a pixel drawn here are the same kind of thing.
 * Swap is its own inverse, so one helper serves both directions. */
const swap16 = (c) => ((c & 0xff) << 8) | ((c >> 8) & 0xff);
const rgb565 = (r, g, b) => swap16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
const toRGB = (c0) => { const c = swap16(c0);
    return [((c >> 11) & 31) * 255 / 31 | 0, ((c >> 5) & 63) * 255 / 63 | 0, (c & 31) * 255 / 31 | 0]; };
const px = (x, y, c) => { if (x >= 0 && x < W && y >= 0 && y < SHEET_H) fb[y * W + x] = c; };
const rect = (x, y, w, h, c) => { for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) px(x + i, y + j, c); };
function text(x, y, s, c) {
    const [r, g, b] = toRGB(c);
    for (const ch of s) {
        const k = gi(ch.charCodeAt(0)), w = T.bw[k], bpr = (w + 1) >> 1;
        for (let gy = 0; gy < T.h; gy++) for (let gx = 0; gx < w; gx++) {
            const byte = T.data[T.off[k] + gy * bpr + (gx >> 1)];
            const a = (gx & 1) ? (byte & 15) : (byte >> 4);
            if (!a) continue;
            const X = x - T.pad + gx, Y = y + gy;
            if (X < 0 || X >= W || Y < 0 || Y >= SHEET_H) continue;
            if (a === 15) { fb[Y * W + X] = c; continue; }
            const [br, bg2, bb] = toRGB(fb[Y * W + X]);
            const m = (p, q) => (p * a + q * (15 - a)) / 15 | 0;
            fb[Y * W + X] = rgb565(m(r, br), m(g, bg2), m(b, bb));
        }
        x += T.adv[k];
    }
    return x;
}

const TB_BG = rgb565(24, 24, 32), TB_TX = rgb565(217, 217, 232);
const DIM = rgb565(122, 122, 136), SEP = rgb565(42, 42, 52);
const GREEN = rgb565(95, 224, 138), MUTE = rgb565(90, 90, 98);
const LABEL = rgb565(255, 196, 90);
const HUE = [rgb565(255, 138, 128), rgb565(255, 196, 90), rgb565(180, 230, 120),
             rgb565(110, 220, 200), rgb565(120, 190, 250), rgb565(180, 160, 250)];
const GLYPH = {
    drum: ['11111110', '11111110', '11111110', '11111110', '11111110', '11111110', '11111110', '00000000'],
    mel:  ['00000000', '00011000', '00011000', '00111100', '00111100', '01111110', '01111110', '00000000'],
};
const glyph8 = (x, y, rows, c) => { for (let j = 0; j < 8; j++) for (let i = 0; i < 8; i++) if (rows[j][i] === '1') px(x + i, y + j, c); };

/* ------------------------------------------------------- capture the real UI */
const WASM = new URL('../build/emu/app.wasm', import.meta.url);
const { instance } = await WebAssembly.instantiate(fs.readFileSync(WASM), {
    wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }),
    env: new Proxy({}, { get: () => () => 0 }),
});
const M = instance.exports;
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const shot = () => new Uint16Array(M.memory.buffer, M.web_fb(), 240 * 240).slice();
M.web_boot(); step(200);
const B = { HOME: 6, SEQ: 9, ARP: 8 };
const press = (bit) => { M.web_buttons(1 << bit); step(40); M.web_buttons(0); step(90); };

const real = {};
real.home = shot();
press(B.SEQ); real.grid = shot();
press(B.ARP); real.arp = shot();
press(B.HOME);

/* copy a band of the captured screen into the sheet, 1:1 */
function pasteBand(dstY, band, h = 24) {
    for (let y = 0; y < h; y++) for (let x = 0; x < 240; x++) px(x, dstY + y, band[y * 240 + x]);
}

/* ------------------------------------------------ draw the proposed headers */
function drawNew(y0, opts) {
    const { track, type, bars, steps, bpm, loop, page, pages, muted, name, drumName } = opts;
    rect(0, y0, W, 24, TB_BG);
    rect(0, y0 + 23, W, 1, SEP);
    rect(6, y0 + 4, 10, 10, HUE[track % HUE.length]);
    glyph8(15, y0 + 4, type === 'drum' ? GLYPH.drum : GLYPH.mel, TB_TX);
    text(26, y0 + 3, "TRACK", TB_TX);
    text(26 + textWidth("TRACK ") + 2, y0 + 3, `${track + 1}`, TB_TX);
    text(W - 6 - textWidth(name), y0 + 3, name, DIM);
    let x = 6;
    if (drumName) { text(x, y0 + 15, drumName, HUE[track % HUE.length]); x += textWidth(drumName) + 8; }
    else {
        text(x, y0 + 15, `${bars} BAR`, DIM); x += textWidth(`${bars} BAR`) + 7;
        if (steps !== undefined) { text(x, y0 + 15, `${steps} STEPS`, DIM); x += textWidth(`${steps} STEPS`) + 7; }
    }
    text(x, y0 + 15, `${bpm} BPM`, TB_TX);
    text(196, y0 + 15, muted ? "MUTE" : `S${loop}`, muted ? MUTE : GREEN);
    for (let i = 0; i < pages; i++) {
        const dx = 211 + i * 6;
        if (i === page) rect(dx, y0 + 17, 3, 3, TB_TX);
        else { rect(dx, y0 + 17, 3, 3, SEP); rect(dx + 1, y0 + 18, 1, 1, TB_BG); }
    }
}

/* --------------------------------------------------------------- compose it */
fb.fill(rgb565(10, 10, 13));
let y = 4;

text(6, y, "NOW  -  as it renders today", LABEL); y += 14;
for (const [label, band] of [["HOME  20px bar, text y6", real.home], ["GRID  16px bar, text y4", real.grid], ["ARP   16px bar, text y6", real.arp]]) {
    pasteBand(y, band, 26);
    text(246 - textWidth(label), y + 26, label, rgb565(110, 110, 120));   /* off-screen guard, unused */
    y += 28;
}

/* a rule between the two halves */
rect(0, y + 2, W, 1, rgb565(70, 70, 82)); y += 10;

text(6, y, "NEW  -  one 24px bar, two lines", LABEL); y += 14;
const rows = [
    { track: 0, type: 'mel',  bars: 4, steps: 16, bpm: 120, loop: 1, page: 0, pages: 5, name: 'HOME' },
    { track: 2, type: 'mel',  bars: 4, steps: 16, bpm: 120, loop: 1, page: 1, pages: 5, name: 'GRID' },
    { track: 0, type: 'drum', drumName: 'KCK', bpm: 120, loop: 1, page: 1, pages: 5, name: 'GRID' },
    { track: 3, type: 'mel',  bars: 2, steps: 8,  bpm: 96,  loop: 3, page: 3, pages: 5, name: 'ARP', muted: true },
];
for (const r of rows) { drawNew(y, r); y += 27; }

/* ------------------------------------------------------------------- save PNG */
const SCALE = 3;
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
function crc32(buf) {
    let c, crc = 0xffffffff;
    for (let n = 0; n < buf.length; n++) {
        c = (crc ^ buf[n]) & 0xff;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        crc = (crc >>> 8) ^ c;
    }
    return (crc ^ 0xffffffff) >>> 0;
}
const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
    return Buffer.concat([len, td, crc]);
};
const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(OW, 0); ihdr.writeUInt32BE(OH, 4); ihdr[8] = 8; ihdr[9] = 2;
const png = Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(out)), chunk('IEND', Buffer.alloc(0)),
]);
const dest = process.argv[2] || '/tmp/header_compare.png';
fs.writeFileSync(dest, png);
console.log(`${OW}x${OH} -> ${dest} (${png.length} B)`);

/* verify the claim the sheet makes: bar heights, measured in the captured bands */
function barHeight(band) {
    let bg = band[239];
    let h = 0;
    for (let y2 = 0; y2 < 40; y2++) { if (band[y2 * 240 + 239] === bg) h = y2 + 1; else break; }
    return h;
}
console.log(`captured bar heights: HOME ${barHeight(real.home)}, GRID ${barHeight(real.grid)}, ARP ${barHeight(real.arp)}`);
