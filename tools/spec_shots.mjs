/* tools/spec_shots.mjs — the screenshots the specs point at.
 *
 * The specs used to carry hand-drawn SVGs. Those drift: melody-grid.svg still
 * had a 16px header and cells at y=17 long after the bar grew to 24, so the
 * figure described a screen that no longer existed. These are not drawn at all
 * -- they are the wasm renderer's own framebuffer, so they cannot disagree with
 * the firmware. Cheap enough to regenerate on every layout change.
 *
 *   node tools/spec_shots.mjs [outdir]
 *
 * Writes melody-grid.png (the GRID page with a tied phrase) and tie-states.png
 * (play / tie / rest, stacked 4x).
 */
import fs from 'node:fs';
import zlib from 'node:zlib';

const OUTDIR = process.argv[2] || new URL('../docs/', import.meta.url).pathname;
const W = 240, H = 240;

const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(new URL('../build/emu/app.wasm', import.meta.url)),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const grab = () => new Uint16Array(M.memory.buffer, M.web_fb(), W * H).slice();
const rgb = (c0) => { const c = ((c0 & 0xff) << 8) | ((c0 >> 8) & 0xff);
    return [((c >> 11) & 31) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3]; };

const B_GLO = 5, B_SEQ = 9, B_OCTUP = 13;
const EN_SELECT = 0, EN_K1 = 3;

/* ---- the phrase every figure shares ----
 * Four attacks on steps 0..3, then one high note at 9. The attacks must be
 * CONSECUTIVE and every tied step must already hold a voice: the gesture cycles
 * play->tie->rest on the step under the cursor, and a step with no voice has
 * nothing to cycle (rest->play needs a voice to revive), so tying an empty step
 * is a silent no-op. Learn it here or learn it from a figure that is mysteriously
 * missing its tie. */
let cursor = 0;
const setup = () => {
    M.web_boot(); step(400);
    M.web_buttons(1 << B_SEQ); step(90); M.web_buttons(0); step(180);
    M.web_enc(EN_SELECT, 6); step(180);          /* the demo's 0/1/2 are drums */
    cursor = 0;
};
const key = (k, ms = 60) => { M.web_keys(1 << k); step(ms); M.web_keys(0); step(ms); };
const cursorTo = (s) => { if (s !== cursor) { M.web_enc(EN_K1, s - cursor); cursor = s; step(50); } };
const cycle = () => {
    M.web_buttons(1 << B_GLO); step(50);
    M.web_buttons((1 << B_GLO) | (1 << B_OCTUP)); step(50);
    M.web_buttons(1 << B_GLO); step(50);
    M.web_buttons(0); step(70);
};

/* tieSteps: which of 1,2,3 become tie; restStep: which of those goes one step
 * further into rest */
const build = (tieSteps = [], restStep = -1) => {
    setup();
    key(1); cursor++;                 /* C step 0 */
    key(4); cursor++;                 /* E step 1 */
    key(7); cursor++;                 /* G step 2 */
    key(0); cursor++;                 /* C step 3 */
    cursorTo(9); key(7); cursor++;    /* G step 9, away from the tied run */
    for (const s of tieSteps) { cursorTo(s); cycle(); }
    if (restStep >= 0) { cursorTo(restStep); cycle(); }
    cursorTo(5); step(90);            /* park the cursor on a plain step */
};

/* ---- PNG encoder (zlib + two CRCs, no deps) ---- */
const crcT = new Int32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c; });
const crc = (b) => { let c = -1; for (const v of b) c = crcT[(c ^ v) & 0xff] ^ (c >>> 8); return (c ^ -1) >>> 0; };
const chunk = (t, d) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(d.length);
    const td = Buffer.concat([Buffer.from(t, 'ascii'), d]);
    const c = Buffer.alloc(4); c.writeUInt32BE(crc(td));
    return Buffer.concat([len, td, c]);
};
const writePng = (path, px, w, h) => {
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
    ihdr[8] = 8; ihdr[9] = 2;
    const raw = Buffer.alloc(h * (w * 3 + 1));
    for (let y = 0; y < h; y++) {
        raw[y * (w * 3 + 1)] = 0;
        px.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3);
    }
    const png = Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
        chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0)),
    ]);
    fs.writeFileSync(path, png);
    return png.length;
};

const blit = (fb, scale, ox, oy, canvas, cw) => {
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
        const [r, g, b] = rgb(fb[y * W + x]);
        for (let dy = 0; dy < scale; dy++) for (let dx = 0; dx < scale; dx++) {
            const o = ((oy + y * scale + dy) * cw + ox + x * scale + dx) * 3;
            canvas[o] = r; canvas[o + 1] = g; canvas[o + 2] = b;
        }
    }
};

/* 1. the GRID page, 2x: steps 1..3 tied, so the figure shows a run */
build([1, 2, 3]);
let fb = grab();
const S1 = 2, W1 = W * S1, H1 = H * S1;
const c1 = Buffer.alloc(W1 * H1 * 3);
blit(fb, S1, 0, 0, c1, W1);
console.log(`melody-grid.png   ${W1}x${H1}  ${writePng(OUTDIR + 'melody-grid.png', c1, W1, H1)} B`);

/* 2. play / tie / rest stacked, 4x -- same phrase, step 1 in each of the three
 * states. "play" is the same build with no tie applied, so the only difference
 * between the first row and the others is the gesture itself. */
const S2 = 4, CROPY = 16, CROPH = 96;
const shots = [];
build([]); cursorTo(5); shots.push(['play', grab()]);
build([1]); shots.push(['tie', grab()]);
build([1], 1); shots.push(['rest', grab()]);

const W2 = W * S2, GAP = 4 * S2;
const H2 = shots.length * (CROPH * S2 + GAP);
const c2 = Buffer.alloc(W2 * H2 * 3);
shots.forEach(([label, f], si) => {
    const oy = si * (CROPH * S2 + GAP);
    for (let y = 0; y < CROPH; y++) for (let x = 0; x < W; x++) {
        const [r, g, b] = rgb(f[(CROPY + y) * W + x]);
        for (let dy = 0; dy < S2; dy++) for (let dx = 0; dx < S2; dx++) {
            const o = ((oy + y * S2 + dy) * W2 + x * S2 + dx) * 3;
            c2[o] = r; c2[o + 1] = g; c2[o + 2] = b;
        }
    }
});
console.log(`tie-states.png    ${W2}x${H2}  ${writePng(OUTDIR + 'tie-states.png', c2, W2, H2)} B`);
console.log('rows top to bottom: ' + shots.map((s) => s[0]).join(' / '));
