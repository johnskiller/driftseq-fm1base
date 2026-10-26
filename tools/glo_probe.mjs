/* tools/glo_probe.mjs — GLO is a live modifier, not a click.
 *
 * It was implemented in the old tree (firmware/src/ui_input.c set glo_held on
 * the press edge and cleared it on release, and `continue`d so GLO never
 * reached ui_button itself) and lost in the port to fm1-base: the new
 * ui_input_poll was rewritten around plat_buttons()' logical mask and only
 * forwarded press/release to ui_button, which discards presses outright. So
 * glo_held stayed 0 forever and every GLO+K1..K4 branch was dead code.
 *
 * What has to hold:
 *   1. holding GLO changes the footer names (the layer is named while held)
 *   2. the same knob does a DIFFERENT thing under GLO
 *   3. releasing GLO puts the footer back and the bare behaviour returns
 *   4. pressing GLO alone changes no page -- it is a modifier, not a button
 */
import fs from 'node:fs';

const W = 240, H = 240;
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync(new URL('../build/emu/app.wasm', import.meta.url)),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const grab = () => new Uint16Array(M.memory.buffer, M.web_fb(), W * H).slice();
M.web_boot(); step(300);

const B_GLO = 5;                       /* plat.h: B_FX,SEL,ENV,LFO,EDIT,GLO,... */
const FX = [8, 64, 120, 176];          /* footer columns */
const FOOT_Y = 222;

/* Footer column as an ink mask: the names are glyphs, so comparing ink is
 * comparing which word is drawn, with no OCR involved. */
const exp = (c0) => { const c = ((c0 & 0xff) << 8) | ((c0 >> 8) & 0xff);
    const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)]; };
const ink = (fb, x0) => {
    const out = [];
    for (let y = FOOT_Y - 6; y < FOOT_Y + 8; y++) for (let x = x0; x < x0 + 46; x++) {
        const [r, g, b] = exp(fb[y * W + x]);
        out.push((r + g + b) / 3 > 90 ? 1 : 0);
    }
    return out;
};
const sameInk = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

let fails = 0;
const check = (ok, label, detail = '') => {
    if (!ok) fails++;
    console.log(`${ok ? 'ok  :' : 'FAIL:'} ${label}${detail ? `  [${detail}]` : ''}`);
};

const holdGlo = () => { M.web_buttons(1 << B_GLO); step(60); };
const releaseGlo = () => { M.web_buttons(0); step(60); };
const turn = (enc, det) => { M.web_enc(enc, det); step(40); };

console.log('== while GLO is held, the footer names the GLO layer ==');
const bare = [0, 1, 2, 3].map((i) => ink(grab(), FX[i]));
holdGlo();
const held = [0, 1, 2, 3].map((i) => ink(grab(), FX[i]));
check(!sameInk(bare[1], held[1]), 'column 1 changes under GLO (LEN -> ZOOM)');
check(!sameInk(bare[2], held[2]), 'column 2 changes under GLO (MUTE -> DIV/mute layer)');
check(!sameInk(bare[0], held[0]) || sameInk(bare[0], held[0]),
      'column 0 is START on HOME both ways, so it may or may not differ', 'informational');

console.log('\n== the same knob does different work under GLO ==');
{
    /* K2 bare is loop_len (1..64, moves one per detent, no ceiling in sight
     * from here); under GLO it is the hero zoom, a dead-stop ladder 1/2/4/8.
     * So: eight detents up bare must keep changing, eight detents up under GLO
     * must saturate and stop changing. That difference cannot be faked by
     * "something moved" -- the previous version of this test compared a single
     * frame and passed even with GLO dead, because a bare loop_len also moved. */
    releaseGlo();
    step(1200);
    M.web_enc(4, 8); step(60);                     /* EN_K2, +8 detents, bare */
    const bareA = ink(grab(), FX[1]);
    M.web_enc(4, 8); step(60);
    const bareB = ink(grab(), FX[1]);
    check(!sameInk(bareA, bareB), 'bare K2 keeps moving over 8 more detents', 'loop_len has room');

    releaseGlo();
    step(1200);
    holdGlo();
    M.web_enc(4, 8); step(60);                     /* K2 +8 under GLO: zoom to 8 */
    const gloA = ink(grab(), FX[1]);
    M.web_enc(4, 8); step(60);                     /* already at the top stop */
    const gloB = ink(grab(), FX[1]);
    check(sameInk(gloA, gloB),
          'GLO+K2 saturates: 8 more detents change nothing', 'the zoom ladder dead-stops at 8');
    releaseGlo();
}

console.log('\n== releasing GLO restores the bare layer ==');
{
    step(1200);
    const back = [0, 1, 2, 3].map((i) => ink(grab(), FX[i]));
    check(sameInk(back[1], bare[1]), 'column 1 is LEN again after release');
    check(sameInk(back[2], bare[2]), 'column 2 is MUTE again after release');
}

console.log('\n== GLO alone is not a page change ==');
{
    const home = grab();
    holdGlo();
    releaseGlo();
    step(200);
    const after = grab();
    /* the header must be identical: GLO must not have advanced the page */
    let diff = 0;
    for (let y = 0; y < 24; y++) for (let x = 0; x < 240; x++) if (home[y * W + x] !== after[y * W + x]) diff++;
    check(diff === 0, 'pressing and releasing GLO leaves the header untouched', `${diff} pixels differ`);
}

console.log('\n== GLO+K1 on GRID is LOOP START ==');
{
    releaseGlo();
    step(1200);
    M.web_buttons(1 << 9); step(60);               /* B_SEQ = GRID */
    M.web_buttons(0); step(120);
    const gridBare = ink(grab(), FX[0]);
    holdGlo();
    M.web_enc(3, 1); step(40);                     /* EN_K1 = 3, +1 under GLO */
    const gridGlo = ink(grab(), FX[0]);
    check(!sameInk(gridGlo, gridBare), 'GLO+K1 on GRID shows a value where STEP was', 'LOOP START');
    releaseGlo();
}

console.log(fails ? `\nGLO FAILED (${fails})` : '\nGLO OK');
process.exit(fails ? 1 : 0);
