/* tools/steprecord_probe.mjs — step record with CHORD KEYS off, in the SHIPPED wasm.
 *
 * John 2026-10-31: "melody grid的step record有问题，只有在ofc on的情况下step record
 * 才有效"
 *
 * tests/steprecord.txt proves the state machine on the host. This file proves the
 * thing that was actually broken is now visible ON THE GRID: the step the player
 * wrote shows up as a lit cell with its notes in the header, in the browser build.
 *
 * The claim can be made without reading any app state, because the grid draws
 * each written step as ink in its own cell:
 *
 *   - before the gesture, the cell under the cursor is empty
 *   - after the last key comes up, it is not (and the cursor has moved on)
 *   - the step the cursor was on is the one that changed, not a neighbour
 *
 * OFC off matters: it is the BOOT state, so this is what a player gets from
 * power-on. The probe never turns CHORD KEYS on.
 *
 *   node tools/steprecord_probe.mjs
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const WASM = path.join(HERE, '..', 'build', 'emu', 'app.wasm');
if (!fs.existsSync(WASM)) {
    console.error('no wasm at ' + WASM + ' — build with --web first');
    process.exit(2);
}

let fails = 0, checks = 0;
const check = (ok, msg, detail) => {
    checks++;
    if (!ok) fails++;
    console.log((ok ? 'ok: ' : 'FAIL: ') + msg + (detail ? '  [' + detail + ']' : ''));
};

const { instance } = await WebAssembly.instantiate(fs.readFileSync(WASM), {
    wasi_snapshot_preview1: {
        clock_time_get: () => 0, proc_exit: () => {}, fd_write: () => 0,
        fd_close: () => 0, environ_get: () => 0, environ_sizes_get: () => 0,
    },
    env: {},
});
const M = instance.exports;

const W = 240;
const BTN = { SEQ: 9, HOME: 6 };
const EN = { SELECT: 0 };

const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const press = (bit) => { M.web_buttons(1 << bit); step(60); M.web_buttons(0); step(160); };
/* a key is a level too: web_key(k, vel, 0) down, web_key(k, 0, 0) up */
const keyDown = (k, vel = 100) => { M.web_key(k, vel, 0); step(60); };
const keyUp = (k) => { M.web_key(k, 0, 0); step(60); };
const enc = (role, n) => { M.web_enc(role, n); step(90); };
const grab = () => Array.from(new Uint16Array(M.memory.buffer, M.web_fb(), W * W));

const swap = (c) => ((c & 0xff) << 8) | ((c >> 8) & 0xff);
const px = (fb, x, y) => fb[y * W + x];
const lum = (v) => {
    const c = swap(v), r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (r * 255 / 31 + g * 255 / 63 + b * 255 / 31) / 3;
};
const ink = (fb, y0, y1, x0 = 0, x1 = W) => {
    let n = 0;
    for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) if (lum(px(fb, x, y)) > 90) n++;
    return n;
};
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

/* ---- the grid, with CHORD KEYS off ---- */
step(600);                                   /* render before the first grab */
press(BTN.SEQ);
step(300);

/* A written step is not "more ink": an empty cell draws as a hollow box or a dim
 * slot and a written one draws lit, so the count can go either way. What is
 * reliable is WHERE the pixels changed, so every check below compares the cells'
 * band pixel by pixel and asks which cells differ. */
const BAND = [8, 232, 18, 112];              /* x0, x1, y0, y1 of the step cells */
const changedCells = (a, b) => {
    const [x0, x1, y0, y1] = BAND;
    const cols = new Set();
    for (let y = y0; y < y1; y++)
        for (let x = x0; x < x1; x++)
            if (a[y * W + x] !== b[y * W + x]) cols.add(Math.floor((x - x0) / 14));
    return cols;
};

const before = grab();
const inkBefore = ink(before, BAND[2], BAND[3], BAND[0], BAND[1]);
check(inkBefore > 100, 'the grid has its cells drawn', inkBefore + ' px');

/* one key, down then up: OFC off must write a step */
keyDown(14);
step(220);
const held = grab();
check(changedCells(before, held).size === 0,
      'holding a key writes nothing yet (the write is on release)',
      changedCells(before, held).size + ' cells changed');

keyUp(14);
step(300);
const after = grab();
const cells1 = changedCells(before, after);
check(cells1.size > 0, 'releasing the key wrote a step into the grid',
      cells1.size + ' cell(s) changed');
/* HOW MANY steps were written is not a question pixels can answer here: the
 * cursor marker and the header move too, so a written cell and a moved cursor
 * both show up as a changed column. tests/steprecord.txt pins the count
 * (step0's voice total, cursor position); this probe pins that the write is
 * VISIBLE in the shipped build. */

/* a chord: three keys down, released one at a time. The gesture rule says this
 * is ONE step, so the screen must not change until the last key comes up. */
const a1 = grab();
keyDown(7); step(70);
keyDown(11); step(70);
keyDown(14); step(200);
const allDown = grab();
check(changedCells(a1, allDown).size === 0,
      'three keys down write nothing', changedCells(a1, allDown).size + ' cells changed');
keyUp(7); step(140);
const oneUp = grab();
check(changedCells(a1, oneUp).size === 0,
      'lifting one of three keys writes nothing', changedCells(a1, oneUp).size + ' changed');
keyUp(11); step(140);
keyUp(14); step(300);
const done = grab();
const cells2 = changedCells(a1, done);
check(cells2.size > 0, 'the LAST release writes the chord', cells2.size + ' cell(s) changed');

/* ---- and it survives a trip through another page ---- */
/* The step is in the pattern, not in a transient: leaving to the hero and coming
 * back must still show it. A write that only ever lived in the draw path would
 * pass every check above and fail this one. */
press(BTN.HOME);
step(400);
const hero = grab();
press(BTN.SEQ);
step(400);
const back = grab();
check(changedCells(back, done).size === 0, 'the written steps are still there after HOME and back',
      changedCells(back, done).size + ' cells changed');

console.log('\n' + (fails ? 'FAILURES: ' + fails + '/' + checks : 'STEP RECORD PROBE OK (' + checks + ' checks)'));
process.exit(fails ? 1 : 0);
