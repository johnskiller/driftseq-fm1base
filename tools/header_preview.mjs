/* tools/header_preview.mjs — draw the proposed header with the REAL FONT_T glyph
 * data, at 4x, into a PNG.
 *
 * Why not a mock font: the point of a design sheet is that the numbers on the
 * page are true. FONT_T is 12px line / 8px cap with 2px left pad and a 4-bit
 * alpha per pixel, decoded here exactly the way gfx.c's cv_text decodes it
 * (see platform/device/gfx.c:154). If the preview is tight, the build is tight.
 */
import fs from 'node:fs';
import zlib from 'node:zlib';

/* ---- FONT_T, read out of the generated header ---------------------------- */
const src = fs.readFileSync(new URL('../build/gen/felucca_font.h', import.meta.url), 'utf8');
const grabArr = (name) => {
    const m = new RegExp(`${name}\\[[^\\]]*\\]\\s*=\\s*\\{([^}]*)\\}`, 's').exec(src);
    if (!m) throw new Error('missing ' + name);
    return m[1].split(',').map((t) => t.trim()).filter(Boolean).map((t) => parseInt(t, 0));
};
const T = {
    h: 12, pad: 2, first: 32, last: 95,
    adv: grabArr('FONT_T_ADV'), bw: grabArr('FONT_T_BW'),
    off: grabArr('FONT_T_OFF'), data: grabArr('FONT_T_DATA'),
};
const gi = (ch) => Math.max(0, Math.min(T.bw.length - 1, ch - T.first));
const textWidth = (s) => [...s].reduce((a, c) => a + T.adv[gi(c.charCodeAt(0))], 0);

/* ---- a tiny framebuffer, in the panel's own RGB565 vocabulary -------------- */
const W = 240, H = 64;                      /* the header plus room to breathe */
const fb = new Uint16Array(W * H);
/* The panel's framebuffer is big-endian 565: gfx.c writes cv_px through swap16(), so
 * a uint16 read out of wasm memory has its bytes reversed. Everything in this file is
 * kept in THAT order -- rgb565() returns a swapped value and toRGB() accepts one -- so
 * a pixel captured from the wasm and a pixel drawn here are the same kind of thing.
 * Swap is its own inverse, so one helper serves both directions. */
const swap16 = (c) => ((c & 0xff) << 8) | ((c >> 8) & 0xff);
const rgb565 = (r, g, b) => swap16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
const toRGB = (c0) => { const c = swap16(c0);
    return [((c >> 11) & 31) * 255 / 31 | 0, ((c >> 5) & 63) * 255 / 63 | 0, (c & 31) * 255 / 31 | 0]; };
const px = (x, y, c) => { if (x >= 0 && x < W && y >= 0 && y < H) fb[y * W + x] = c; };
const rect = (x, y, w, h, c) => { for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) px(x + i, y + j, c); };

/* cv_text, as gfx.c does it: 4-bit alpha, blended against what is already there */
function text(x, y, s, c) {
    const [r, g, b] = toRGB(c);
    for (const ch of s) {
        const k = gi(ch.charCodeAt(0));
        const w = T.bw[k], bpr = (w + 1) >> 1;
        for (let gy = 0; gy < T.h; gy++) {
            for (let gx = 0; gx < w; gx++) {
                const byte = T.data[T.off[k] + gy * bpr + (gx >> 1)];
                const a = (gx & 1) ? (byte & 15) : (byte >> 4);
                if (!a) continue;
                const X = x - T.pad + gx, Y = y + gy;
                if (X < 0 || X >= W || Y < 0 || Y >= H) continue;
                if (a === 15) { fb[Y * W + X] = c; continue; }
                const [br, bg, bb] = toRGB(fb[Y * W + X]);
                const m = (p, q) => (p * a + q * (15 - a)) / 15 | 0;
                fb[Y * W + X] = rgb565(m(r, br), m(g, bg), m(b, bb));
            }
        }
        x += T.adv[k];
    }
    return x;
}

/* ---- the palette, from ui_screens.c -------------------------------------- */
const TB_BG = rgb565(24, 24, 32), TB_TX = rgb565(217, 217, 232);
const DIM = rgb565(122, 122, 136), SEP = rgb565(42, 42, 52);
const GREEN = rgb565(95, 224, 138), MUTE = rgb565(90, 90, 98);
const HUE = [rgb565(255, 138, 128), rgb565(255, 196, 90), rgb565(180, 230, 120),
             rgb565(110, 220, 200), rgb565(120, 190, 250), rgb565(180, 160, 250)];

/* ---- the two 8x8 type glyphs -------------------------------------------- */
const GLYPH = {
    /* drum: a filled block with a rim — "a pad you hit" */
    drum: ['11111110', '11111110', '11111110', '11111110',
           '11111110', '11111110', '11111110', '00000000'],
    /* melodic: a little contour — pitch going somewhere */
    mel:  ['00000000', '00011000', '00011000', '00111100',
           '00111100', '01111110', '01111110', '00000000'],
};
function glyph8(x, y, rows, c) {
    for (let j = 0; j < 8; j++) for (let i = 0; i < 8; i++) if (rows[j][i] === '1') px(x + i, y + j, c);
}

/* ---- the layout ---------------------------------------------------------
 * 16px bar. FONT_T glyphs sit at y=4 (8px cap, 4 above and 4 below).
 * Every slot is measured from the real advance widths above, so the numbers on
 * this sheet are the numbers the build will produce.
 */
const BAR = 24, TY = 4, TY2 = 15;
const S = {
    hue:   { x: 6,  w: 6 },                  /* track colour: 6x6 */
    glyph: { x: 15, w: 8 },                  /* drum / melodic */
    id:    { x: 26 },                        /* TRACK n */
    sep1:  { x: 0 },                         /* filled in below */
    axis:  { },                              /* 1 BAR */
    grid:  { },                              /* 16 STEPS */
    clock: { },                              /* 120 */
    loop:  { },                              /* S1 */
    dots:  { x: 211 },                       /* page indicator */
};

function drawHeader(opts) {
    const { track, type, bars, steps, bpm, loop, page, pages, muted, name } = opts;
    rect(0, 0, W, BAR, TB_BG);
    /* a 1px rule under the bar: the bar now carries two lines, and without a
     * floor it reads as part of the page body */
    rect(0, BAR - 1, W, 1, SEP);

    /* ---- line 1: identity (y 3..12) ---- */
    rect(S.hue.x, 4, 10, 10, HUE[track % HUE.length]);     /* track colour, 10x10 */
    glyph8(S.glyph.x, 4, type === 'drum' ? GLYPH.drum : GLYPH.mel, TB_TX);
    text(S.id.x, 3, "TRACK", TB_TX);
    text(S.id.x + textWidth("TRACK ") + 2, 3, `${track + 1}`, TB_TX);
    /* page name, dim, top right: the ONLY mode name on screen and it is in the
     * bar, which is content the page body does not repeat */
    {   const nm = name || "";
        text(W - 6 - textWidth(nm), 3, nm, DIM);
    }

    /* ---- line 2: the readouts (y 15..22) ---- */
    let x = S.hue.x;
    if (type === 'drum' && opts.drumName) {
        text(x, TY2, opts.drumName, HUE[track % HUE.length]);
        x += textWidth(opts.drumName) + 8;
    } else {
        /* melodic: the loop's span in bars, and its length in steps */
        text(x, TY2, `${bars} BAR`, DIM); x += textWidth(`${bars} BAR`) + 7;
        if (steps !== undefined) { text(x, TY2, `${steps} STEPS`, DIM); x += textWidth(`${steps} STEPS`) + 7; }
    }
    text(x, TY2, `${bpm} BPM`, TB_TX);

    /* ---- right: loop start + page dots, on line 2 ---- */
    text(196, TY2, muted ? "MUTE" : `S${loop}`, muted ? MUTE : GREEN);
    for (let i = 0; i < pages; i++) {
        const dx = S.dots.x + i * 6;
        if (i === page) rect(dx, TY2 + 2, 3, 3, TB_TX);
        else { rect(dx, TY2 + 2, 3, 3, SEP); rect(dx + 1, TY2 + 3, 1, 1, TB_BG); }
    }
}

/* ---- render the sheet: four rows, one per page --------------------------- */
const rows = [
    { name: 'HOME',      track: 0, type: 'mel',  bars: 4, steps: 16, bpm: 120, loop: 1, page: 0, pages: 5 },
    { name: 'GRID',      track: 2, type: 'mel',  bars: 4, steps: 16, bpm: 120, loop: 1, page: 1, pages: 5 },
    { name: 'GRID',      track: 0, type: 'drum', drumName: 'KICK',  bpm: 120, loop: 1, page: 1, pages: 5 },
    { name: 'GRID',      track: 3, type: 'drum', drumName: 'SNARE', bpm: 96,  loop: 3, page: 1, pages: 5, muted: true },
];
fb.fill(rgb565(8, 8, 10));
rows.forEach((r, i) => drawHeader(r));

/* ---- write the PNG at 4x ------------------------------------------------- */
const SCALE = 4;
const OW = W * SCALE, OH = BAR * rows.length * SCALE;
const out = Buffer.alloc(OH * (OW * 3 + 1));
let p = 0;
for (let y = 0; y < OH; y++) {
    out[p++] = 0;
    for (let x = 0; x < OW; x++) {
        const [r, g, b] = toRGB(fb[(y / SCALE | 0) * W + (x / SCALE | 0)]);
        out[p++] = r; out[p++] = g; out[p++] = b;
    }
}
const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(zlib.crc32 ? zlib.crc32(td) : crc32(td));
    return Buffer.concat([len, td, crc]);
};
function crc32(buf) {
    let c, crc = 0xffffffff;
    for (let n = 0; n < buf.length; n++) {
        c = (crc ^ buf[n]) & 0xff;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        crc = (crc >>> 8) ^ c;
    }
    return (crc ^ 0xffffffff) >>> 0;
}
const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(OW, 0); ihdr.writeUInt32BE(OH, 4);
ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
const png = Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(out)), chunk('IEND', Buffer.alloc(0)),
]);
const dest = process.argv[2] || '/tmp/header_design.png';
fs.writeFileSync(dest, png);

console.log(`${OW}x${OH} -> ${dest} (${png.length} B)`);
console.log('\nslot geometry, from the real advance widths:');
const probe = { track: 15, type: 'mel', bars: 4, steps: 16, bpm: 120, loop: 1, page: 0, pages: 5 };
let x = S.id.x + textWidth('TRACK 16') + 8;
console.log(`  hue      x=${S.hue.x}..${S.hue.x + S.hue.w - 1}`);
console.log(`  glyph    x=${S.glyph.x}..${S.glyph.x + 7}`);
console.log(`  id       x=${S.id.x}..${S.id.x + textWidth('TRACK 16') - 1}   ("TRACK 16" = ${textWidth('TRACK 16')}px)`);
console.log(`  sep      x=${x}`);
x += 5;
console.log(`  axis     x=${x}..${x + textWidth('4 BAR') - 1}   ("4 BAR" = ${textWidth('4 BAR')}px)`); x += textWidth('4 BAR') + 6;
console.log(`  grid     x=${x}..${x + textWidth('16 STEPS') - 1}   ("16 STEPS" = ${textWidth('16 STEPS')}px)`); x += textWidth('16 STEPS') + 6;
console.log(`  clock    x=${x}..${x + textWidth('120') - 1}`); x += textWidth('120');
console.log(`  loop     x=196..${196 + textWidth('S1') - 1}`);
console.log(`  dots     x=${S.dots.x}..${S.dots.x + 5 * 6 - 2}`);
console.log(`  widest page content ends at x=${Math.max(x, 196 + textWidth('S1'))}, dots start at ${S.dots.x}`);
