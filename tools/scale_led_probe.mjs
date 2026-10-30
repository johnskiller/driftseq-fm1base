/* tools/scale_led_probe.mjs — the SCALE page's key lights, in the SHIPPED wasm.
 *
 * John, 2026-10-30:
 *   "SCALE页面的键盘灯有问题，随着弹奏逐渐越亮越多，大概率是算错了该亮哪个键"
 *   "在scale状态下不需要0,4,8,12的键盘标尺（标尺只对drum有用）反而是左边级数
 *    7个键，右边和弦8个键需要dim，然后和弦按下有效的需要亮起"
 *
 * The bug: every root press writes a step and walks the cursor (STAGE/stepEntry),
 * and ui_leds drew the GRID's step lights on EVERY page — so the bed gained one
 * more lit key per chord played. A step light also points at a key that belongs
 * to a zone on this page, which is the second half of what he saw.
 *
 * What is asserted here, on the wasm the browser actually loads:
 *   - the roof: with OFC off the bed is the live keyboard (0..18 dim); with OFC
 *     on the degree keys 0..6 and the component keys 19..26 go dim, and nothing
 *     else on the bed is lit;
 *   - no accumulation: four roots leave the masks identical to none, apart from
 *     the selection itself;
 *   - "有效才亮": a valid chord lights its components, a nonsense set (two
 *     thirds) lights none — the same silence oc_resolve gives the sound;
 *   - leaving the page hands the bed back to the pattern (the ruler returns).
 *
 *   PATH=/home/john/.hermes/node/bin:$PATH node tools/scale_led_probe.mjs
 *
 * Two traps inherited from preset_probe.mjs, both real:
 *   - build/emu/app.wasm can be STALE (the PRESETS "does nothing" report was a
 *     wasm half an hour older than the source). The stamp is printed and the
 *     probe refuses a wasm older than ui/ui_scale.c.
 *   - web_buttons is a LEVEL and the render is incremental: buttons are held
 *     across frames and every reading follows a render, never sits inside one.
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const WASM = path.join(HERE, '..', 'build', 'emu', 'app.wasm');
const SRC  = path.join(HERE, '..', 'ui', 'ui_scale.c');
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

const wasmAge = fs.statSync(WASM).mtime, srcAge = fs.statSync(SRC).mtime;
console.log('wasm ' + wasmAge.toISOString() + ', ' + fs.statSync(WASM).size + ' B');
check(wasmAge >= srcAge, 'the wasm is newer than ui/ui_scale.c',
      wasmAge.toISOString() + ' vs ' + srcAge.toISOString());

const { instance } = await WebAssembly.instantiate(fs.readFileSync(WASM), {
    wasi_snapshot_preview1: {
        clock_time_get: () => 0, proc_exit: () => {}, fd_write: () => 0,
        fd_close: () => 0, environ_get: () => 0, environ_sizes_get: () => 0,
    },
    env: {},
});
const M = instance.exports;

const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const press = (bit) => { M.web_buttons(1 << bit); step(40); M.web_buttons(0); step(60); };
const enc = (role, n) => { M.web_enc(role, n); step(40); };

/* plat.h's bit order (the panel prints SEL, the code calls it SCL) */
const BTN = { SEL: 1, ENV: 2, GLO: 5, HOME: 6 };
const EN = { SELECT: 0, K3: 5 };            /* SELECT, ALGO, PRESET, K1, K2, K3, K4 */

/* a key press as the panel would deliver it: web_key(id, vel, press);
 * vel 0 IS the release, which is how the firmware's scan reports it */
const key = (id, down) => { M.web_key(id, down ? 100 : 0, 0); step(50); };
const tapKey = (id) => { key(id, true); key(id, false); };
const lit = () => M.web_lit_keys() >>> 0;
const dim = () => M.web_dim_keys() >>> 0;
const bits = (v) => { const a = []; for (let i = 0; i < 32; i++) if (v >>> i & 1) a.push(i); return a.join(','); };

M.web_boot();
step(600);                                   /* the demo pattern loads */

/* track 4 is the demo's melodic lead (tracks 0..2 are drums on ch9, and the
 * SCALE page's chord keyboard does not exist for a drum track) */
for (let i = 0; i < 4; i++) enc(EN.SELECT, 1);
step(80);
press(BTN.SEL);                              /* SEL opens SCALE */
step(160);

const degDim  = 0x7f;                        /* keys 0..6 */
const compDim = 0x7f80000;                   /* keys 19..26 */
const rootDim = 0x7ff80;                     /* keys 7..18 (12 bits) */
const bedDim  = degDim | rootDim | compDim;
const STEP_RULER = 0x204081;                 /* keys 0,7,14,21 — the 16-step page
                                              * quartiles, ids on a real piano */

/* ---- OFC off: the bed is the live keyboard, no zones, no ruler ---------- */
/* the whole bed below the components is playable in this mode (a key plays a
 * snapped note), so all of 0..18 sits dim and 19..26 stays dark: components do
 * not exist with OFC off */
check(dim() === (degDim | rootDim), 'OFC off: keys 0..18 sit dim (the live keyboard)',
      'dim 0x' + dim().toString(16) + ' = ' + bits(dim()));
/* The ruler's absence cannot be measured in THIS state: the whole bed is dim,
 * so a quartile mark at key 0, 7, 14 or 21 would land on a key that is dim
 * anyway. It is asserted below, with OFC on, where the bed between the zones is
 * dark and a ruler mark would be the only thing lighting it. */
check(lit() === 0, 'OFC off: nothing is selected, so nothing is lit', 'lit 0x' + lit().toString(16));

/* ---- OFC on: both zones dim, nothing lit -------------------------------- */
enc(EN.K3, 1);                               /* K3: OFF -> GATE */
step(80);
check(dim() === (degDim | compDim), 'OFC on: the 7 degree keys and the 8 component keys are dim',
      'dim 0x' + dim().toString(16) + ' = ' + bits(dim()));
/* the root zone (7..18) is dark here, so any quartile mark would show: the ids
 * 7 and 14 sit inside it, 0 and 21 inside the other two zones and so are dim by
 * zone. Nothing of the 16-step ruler survives on this page. */
check((dim() & (1 << 7)) === 0 && (dim() & (1 << 14)) === 0,
      'OFC on: no 16-step ruler on the SCALE page (keys 7 and 14 stay dark)',
      'dim 0x' + dim().toString(16));
check(lit() === 0, 'OFC on: no chord yet, so nothing is lit', 'lit 0x' + lit().toString(16));

/* ---- no accumulation: four roots, one selection ------------------------- */
for (const k of [7, 9, 11, 13]) tapKey(k);
step(80);
check(lit() === (1 << 13), 'four roots played: only the last is lit (the selection)', 'lit 0x' + lit().toString(16));
check(dim() === (degDim | compDim), 'four roots played: the zones did not grow', 'dim 0x' + dim().toString(16));

/* ---- a valid chord lights its components (key 19 = MAJ) ----------------- */
tapKey(19);
step(80);
check(lit() === ((1 << 13) | (1 << 19)), 'MAJ on top of a root: the component lights',
      'lit 0x' + lit().toString(16) + ' = ' + bits(lit()));

/* ---- a nonsense set lights none (23 DIM + 25 SUS = two thirds) --------- */
tapKey(23);
step(80);
tapKey(25);
step(80);
check(lit() === (1 << 13), 'two thirds: the lights drop the chord (only the root stays)',
      'lit 0x' + lit().toString(16) + ' = ' + bits(lit()));

/* ---- leaving the page hands the bed back to the pattern ---------------- */
/* The ruler is back, and so are the steps the chords wrote: key 21's step (the
 * fourth root) is a voiced step, so it lights rather than dimming. Asserting
 * "the ruler is present" means the ruler's OTHER marks are there — a voiced
 * step at a quartile position legitimately replaces the dim mark. */
press(BTN.SEL);
step(200);
const rulerDim = dim() & STEP_RULER, rulerLit = lit() & STEP_RULER;
check((rulerDim | rulerLit) === STEP_RULER && rulerDim !== 0,
      'off SCALE: the 16-step ruler is back (dim marks, minus any voiced step)',
      'dim 0x' + rulerDim.toString(16) + ', lit 0x' + rulerLit.toString(16));

console.log('\n' + checks + ' checks, ' + (fails ? fails + ' FAILED' : 'all passed'));
process.exit(fails ? 1 : 0);
