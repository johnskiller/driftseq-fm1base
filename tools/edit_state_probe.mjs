/* tools/edit_state_probe.mjs — the EDIT panel's state encoding (John 2026-10-27):
 *
 *   1. "v1..v4" is GONE: the row's ordinal is redundant (row order IS the
 *      ordinal), so the old ordinal slot now carries ONE BIG step glyph:
 *      o = attacks, - = rest, = = tied. Ties render in YELLOW.
 *   2. The tied row's "g" reads what will SOUND (the owning attack's hold,
 *      gate=1 + one tie => g2), not the dead stored value (g1).
 *   3. The resolved g matches what the GRID line draws (same number, two
 *      surfaces -- the one-information-one-place contract).
 *   4. Rest rows keep their bar at full hue strength (revival preview);
 *      only the glyph says "-".
 *
 * Layout truth: the panel ALWAYS shows steps[cursor], so panel grabs happen
 * with the cursor parked ON the edited step; grid grabs move it to an empty
 * cell first (the yellow outline would break the line run). Steps: 0 = the
 * attack (F#3), 1 = the tie carrier (A3, then cycled to tie). The cursor
 * tracker starts at 1 because the keybed write advanced it.
 */
import fs from 'node:fs';

const W = 240;
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(new URL('../build/emu/app.wasm', import.meta.url)),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const grab = () => new Uint16Array(M.memory.buffer, M.web_fb(), W * W).slice();
const exp = (c0) => { const c = ((c0 & 0xff) << 8) | ((c0 >> 8) & 0xff);
    const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)]; };
const rgb = (fb, x, y) => exp(fb[y * W + x]);
const isLine = ([r, g, b]) => (r + g + b) / 3 > 40 && Math.max(r, g, b) - Math.min(r, g, b) > 25;

let fails = 0;
const check = (ok, label, detail = '') => { if (!ok) fails++; console.log(`${ok ? 'ok  :' : 'FAIL:'} ${label}${detail ? `  [${detail}]` : ''}`); };

const B_SEQ = 9, B_GLO = 5, B_OCTUP = 13;
const EN_SELECT = 0, EN_K1 = 3;

M.web_boot(); step(400);
M.web_buttons(1 << B_SEQ); step(80); M.web_buttons(0); step(150);
M.web_enc(EN_SELECT, 6); step(120);                       /* empty melodic track */

const key = (k, ms = 70) => { M.web_keys(1 << k); step(ms); M.web_keys(0); step(ms); };
let cur = 0;                                              /* the tracker: key() bumps it below */
const cursorTo = (s) => { if (s !== cur) { M.web_enc(EN_K1, s - cur); cur = s; step(60); } };
const cycle = () => {                                     /* play -> tie -> rest -> play */
    M.web_buttons(1 << B_GLO); step(60);
    M.web_buttons((1 << B_GLO) | (1 << B_OCTUP)); step(60);
    M.web_buttons(1 << B_GLO); step(60);
    M.web_buttons(0); step(80);
};
const EMPTY = 12;
let playFb = null;                                        /* panel grab with step 1 still on play */

console.log('== steps 0 (F#3 attack) and 1 (A3, to be tied) ==');
key(1); cur = 1;                                          /* F#3 at 0, cursor -> 1 */
key(4); cur = 2;                                          /* A3 at 1, cursor -> 2 */
cursorTo(1); step(80);                                    /* panel shows step 1 */

console.log('\n== the big glyph replaces v1..v4 ==');
{
    const fb = grab();
    playFb = fb;                                            /* kept for the g-width calibration */
    let bright = 0, yellow = 0;
    for (let y = 114; y < 136; y++)
        for (let x = 7; x < 18; x++) {
            const [r, g, b] = rgb(fb, x, y);
            const v = (r + g + b) / 3, sat = Math.max(r, g, b) - Math.min(r, g, b);
            if (v > 120) { bright++; if (sat > 60 && r > g && g > b) yellow++; }
        }
    check(bright > 8, 'play row shows a big glyph', `bright=${bright}`);
    check(yellow === 0, 'play glyph is NOT the alarm color', `yellow=${yellow}`);
}

console.log('\n== tie: yellow glyph, g = the owning attack hold ==');
{
    cycle(); step(80);                                      /* step 1: play -> tie */
    const fb = grab();
    let bright = 0, yellow = 0;
    for (let y = 114; y < 136; y++)
        for (let x = 7; x < 18; x++) {
            const [r, g, b] = rgb(fb, x, y);
            const v = (r + g + b) / 3, sat = Math.max(r, g, b) - Math.min(r, g, b);
            if (v > 120) { bright++; if (sat > 60 && r > g && g > b) yellow++; }
        }
    check(bright > 8, 'tie row still shows a big glyph', `bright=${bright}`);
    check(yellow > 4, 'tie glyph IS yellow', `yellow=${yellow}`);
}

console.log('\n== g says the resolved hold: wider than the stored "1" ==');
{
    /* PANEL GEOMETRY: rows hold VOICES (pitch-sorted), not steps -- step 1
     * has one voice, so its state lives in ROW 0. The stored-gate "1"
     * calibration therefore comes from BEFORE the cycle (grabbed in the
     * first section), the resolved "2" from NOW, same row. */
    const fb = grab();
    const inkW = (f, row) => {
        let n = 0;
        for (let x = 89; x < 112; x++)
            for (let y = 112 + row * 25 + 4; y < 112 + row * 25 + 11; y++) {
                const [r, g, b] = exp(f[y * W + x]);
                const sat = Math.max(r, g, b) - Math.min(r, g, b);
                if ((r + g + b) / 3 > 150 && sat < 25) { n++; break; }
            }
        return n;
    };
    const wPlay = inkW(playFb, 0), wTie = inkW(fb, 0);
    check(wPlay > 0, 'play row g value found (stored digit "1")', `cols=${wPlay}`);
    check(wTie > 0, 'tied row g shows a value at all', `cols=${wTie}`);
    check(wTie > wPlay, 'tied row g is "2" (wider than the stored "1")',
          `tie ${wTie} cols vs play ${wPlay} cols`);
}

console.log('\n== the grid line agrees: attack stub + tie run into cell 1 ==');
{
    cursorTo(EMPTY); step(80);
    const fb = grab();
    const X0 = 18, CW = 13, GY = 24, CHH = 22;
    const cellX = (s) => X0 + (s % 16) * CW;
    /* find the A3 line row in cell 1 (the tie carrier's pitch) */
    let lineY = -1;
    for (let y = GY + 1; y < GY + CHH - 1 && lineY < 0; y++)
        if (isLine(rgb(fb, cellX(1) + 4, y))) lineY = y;
    check(lineY >= 0, 'tie cell carries the line', `y=${lineY}`);
    if (lineY >= 0) {
        /* the attack's extension: cell 0 must show a run reaching the seam
         * (gate=1=3px stub + extension to the cell edge), cell 1 edge-to-edge */
        let run0 = 0;
        for (let dx = CW - 1; dx >= 2; dx--) { if (isLine(rgb(fb, cellX(0) + dx, lineY))) run0++; else break; }
        let run1 = 0;
        for (let dx = 0; dx < CW; dx++) { if (isLine(rgb(fb, cellX(1) + dx, lineY))) run1++; else break; }
        check(run0 >= 4, 'the attack line extends to the cell 0 edge (tie adds a cell)',
              `run0=${run0}`);
        check(run1 >= 10, 'cell 1 is filled edge-to-edge (the tie)', `run1=${run1}`);
    }
}

console.log('\n== rest: glyph "-", bar keeps full hue (revival preview) ==');
{
    cursorTo(1); cycle(); cycle();                          /* tie -> rest */
    step(80);
    const fb = grab();
    let bright = 0, yellow = 0;
    for (let y = 114; y < 136; y++)
        for (let x = 7; x < 18; x++) {
            const [r, g, b] = rgb(fb, x, y);
            const v = (r + g + b) / 3, sat = Math.max(r, g, b) - Math.min(r, g, b);
            if (v > 120) { bright++; if (sat > 60 && r > g && g > b) yellow++; }
        }
    check(yellow === 0, 'rest glyph is not yellow', `yellow=${yellow}`);
    check(bright > 4, 'rest glyph still drawn (the dash)', `bright=${bright}`);
    const [r, g, b] = rgb(fb, 100, 112 + 12);
    const sat = Math.max(r, g, b) - Math.min(r, g, b);
    check(sat > 40, 'rest row bar keeps full hue strength', `sat=${sat}`);
}

console.log('\n== back on play, the glyph is white again (cycle arithmetic: ==\n'
            +'== rest section ended at PLAY, so verify WITHOUT cycling)     ==');
{
    /* state ledger: tie ->(1 cycle) rest ->(1 cycle) play happened in the
     * previous section. Step 1 is back on play right now. */
    const fb = grab();
    let bright = 0, yellow = 0;
    for (let y = 114; y < 136; y++)
        for (let x = 7; x < 18; x++) {
            const [r, g, b] = rgb(fb, x, y);
            const v = (r + g + b) / 3, sat = Math.max(r, g, b) - Math.min(r, g, b);
            if (v > 120) { bright++; if (sat > 60 && r > g && g > b) yellow++; }
        }
    check(yellow === 0 && bright > 8, 'play glyph back, not yellow', `bright=${bright} yellow=${yellow}`);
}

console.log(fails ? `\nEDIT STATE PROBE FAILED (${fails})` : '\nEDIT STATE PROBE PASSED');
process.exit(fails ? 1 : 0);
