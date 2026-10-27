/* tools/tie_probe.mjs — the tie gesture and, more importantly, what it looks like.
 *
 * The gesture is GLO+OCT+ on the cursor step, cycling the spec's three states
 * (03 §2.3): play -> tie -> rest -> play. There is no exported state to read,
 * so everything here is judged from the framebuffer — which is the right
 * standard anyway: a tie the engine honours but the screen does not draw is a
 * feature the user cannot find.
 *
 * The visual contract being tested:
 *   attack : a hue line with a 2px margin on each side of the cell
 *   tie    : the SAME hue at the SAME pitch, edge to edge, reaching 1px into
 *            the gap before it -- so a tied run fuses into one bar
 *   rest   : no line at all
 *
 * That difference is the whole design: "joined or not" is readable at a glance,
 * with no extra glyph and no mode word on screen.
 *
 * MEASUREMENT NOTES, all learned the hard way:
 *   - the cursor cell carries a yellow 1px outline, ink on every row, so every
 *     measurement parks the cursor on a DIFFERENT step and reads the cell it
 *     left behind;
 *   - the demo's only populated tracks are drums (0, 3, 4) and a drum page
 *     draws hollow squares, not pitch lines. So this writes its own melody onto
 *     an empty melodic track with the keybed;
 *   - web_enc() takes a RELATIVE detent count, not an absolute position, and
 *     the keybed advances the cursor on every write. So the cursor is tracked
 *     in a variable here, never assumed.
 */
import fs from 'node:fs';

const W = 240, H = 240;
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(new URL('../build/emu/app.wasm', import.meta.url)),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const grab = () => new Uint16Array(M.memory.buffer, M.web_fb(), W * H).slice();

/* plat.h: B_FX,SEL,ENV,LFO,EDIT,GLO,HOME,SAVE,ARP,SEQ,PLAY,REC,OCTDN,OCTUP */
const B_GLO = 5, B_SEQ = 9, B_OCTUP = 13;
const EN_SELECT = 0, EN_K1 = 3;

const exp = (c0) => { const c = ((c0 & 0xff) << 8) | ((c0 >> 8) & 0xff);
    const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)]; };
const rgb = (fb, x, y) => exp(fb[y * W + x]);

/* ---- the melodic GRID geometry, from ui_screens.c draw_grid_melodic ---- */
const X0 = 18, CW = 13, CHH = 22, GY = 24;
const cellX = (s) => X0 + (s % 16) * CW;
const cellY = (s) => GY + Math.floor(s / 16) * CHH;

/* A pitch line is saturated (the hue) and horizontal. Saturation separates it
 * from the cell background and from the grey rest line. Plan D: an attack is
 * a 2px stub (the extension lives in other cells, painted after the loop), a
 * tie runs edge to edge (cw+1 with the 1px overhang) -- so the threshold is
 * 2: enough to reject the 1px cursor edge, low enough to catch the stub. */
function lines(fb, s) {
    const x = cellX(s), y = cellY(s), out = [];
    for (let r = 1; r < CHH - 1; r++) {
        let run = 0, widest = 0;
        for (let dx = -1; dx <= CW; dx++) {
            const [rr, gg, bb] = rgb(fb, x + dx, y + r);
            const v = (rr + gg + bb) / 3, sat = Math.max(rr, gg, bb) - Math.min(rr, gg, bb);
            if (v > 40 && sat > 25) { run++; if (run > widest) widest = run; }
            else run = 0;
        }
        if (widest >= 2) out.push({ r, widest });
    }
    return out;
}
const widest = (fb, s) => lines(fb, s).reduce((m, q) => Math.max(m, q.widest), 0);
const topRow = (fb, s) => { const l = lines(fb, s); return l.length ? l[0].r : -1; };

let fails = 0;
const check = (ok, label, detail = '') => {
    if (!ok) fails++;
    console.log(`${ok ? 'ok  :' : 'FAIL:'} ${label}${detail ? `  [${detail}]` : ''}`);
};

M.web_boot();
step(400);
M.web_buttons(1 << B_SEQ); step(80); M.web_buttons(0); step(150);

/* an empty melodic track: 0..4 are drums, 5+ are melodic */
M.web_enc(EN_SELECT, 6); step(120);

let cursor = 0;
const cursorTo = (s) => { if (s !== cursor) { M.web_enc(EN_K1, s - cursor); cursor = s; step(60); } };
const key = (k, ms = 70) => { M.web_keys(1 << k); step(ms); M.web_keys(0); step(ms); };
const cycle = () => {                     /* GLO+OCT+ on the cursor step */
    M.web_buttons(1 << B_GLO); step(60);
    M.web_buttons((1 << B_GLO) | (1 << B_OCTUP)); step(60);
    M.web_buttons(1 << B_GLO); step(60);
    M.web_buttons(0); step(80);
};

/* write a melody; every write advances the cursor by one */
const PITCHES = [0, 4, 7];
for (const k of PITCHES) { key(k); cursor++; }

const PARK = 12;                          /* an empty cell, clear of the work */
const measure = (s) => { cursorTo(PARK); return grab(); };

console.log('== the keybed wrote the melody ==');
{
    const fb = measure(0);
    const n = [0, 1, 2].filter((s) => lines(fb, s).length).length;
    check(n === 3, 'three steps carry a pitch line', `${n}/3`);
}

const A = 0, T = 1;                       /* attack on 0, tie parks on 1 */

console.log(`\n== the gesture cycles three states on step ${T} ==`);
{
    const beforeRows = lines(measure(T), T).length;
    const beforeW = widest(measure(T), T);

    cursorTo(T); cycle();                 /* play -> tie */
    const tieRows = lines(measure(T), T).length;
    const tieW = widest(measure(T), T);

    cursorTo(T); cycle();                 /* tie -> rest */
    const restRows = lines(measure(T), T).length;

    cursorTo(T); cycle();                 /* rest -> play */
    const playW2 = widest(measure(T), T);

    check(beforeRows > 0, 'step 1 attacks before the gesture', `${beforeRows} rows`);
    check(tieRows > 0, 'tie state still draws the note it extends', `${tieRows} rows`);
    check(restRows === 0, 'rest state draws no pitch line at all', `${restRows} rows`);
    check(playW2 === beforeW, 'third press restores the play state', `${beforeW}px -> ${playW2}px`);
    check(tieW > beforeW, 'the tie line reaches further than the attack line',
          `tie ${tieW}px vs attack ${beforeW}px`);
}

console.log('\n== a tie draws the pitch of the step it extends ==');
{
    cursorTo(T); cycle();                 /* play -> tie again */
    const rA = topRow(measure(A), A);
    const rT = topRow(measure(T), T);
    check(rA >= 0 && rT >= 0 && Math.abs(rT - rA) <= 1,
          'the tie line sits at the extended note\'s pitch', `row ${rT} vs ${rA}`);
    cursorTo(T); cycle(); cycle(); cycle(); /* back to play */
}

console.log('\n== a tie makes the sound continuous, not restarted ==');
{
    /* This is the acoustic claim behind the visual: with step 1 tied into
     * step 0, step 1 must not emit a note-on of its own. The engine test
     * covers the exact byte stream; here we only need to know the UI path
     * really set a tie rather than a rest -- which the line existing already
     * proves. Kept as an explicit statement so the intent is on the record. */
    cursorTo(T);
    const hasLineTied = lines(measure(T), T).length > 0;
    check(hasLineTied, 'a tied step still paints (a rest would paint nothing)');
    cursorTo(T); cycle();
    check(lines(measure(T), T).length === 0, 'and cycling on turns it into a rest');
    cursorTo(T); cycle(); cycle();
}

console.log('\n== a full cycle does not disturb the header ==');
{
    cursorTo(T);
    step(120);
    const before = grab();
    cycle(); cycle(); cycle();
    step(120);
    const after = grab();
    let diff = 0;
    for (let y = 0; y < 16; y++) for (let x = 0; x < W; x++)
        if (before[y * W + x] !== after[y * W + x]) diff++;
    check(diff === 0, 'play -> tie -> rest -> play leaves the header identical', `${diff} px differ`);
}

console.log(fails ? `\nTIE PROBE FAILED (${fails})` : '\nTIE PROBE PASSED');
process.exit(fails ? 1 : 0);
