/* tools/trkset2_probe.mjs — TRACK SET page 2, in the SHIPPED wasm, by pixels.
 *
 * John 2026-10-31: "track set页面需要增加两个项目：1 change div，现在的div修改位置
 * 不是很明显，增加一个更明显的修改方式。2 clear track，这个危险操作需要弹出确认。
 * 页面可能容纳不下，需要第二页"
 *
 * tests/trkset2.txt asserts the state machine on the host build. This file asserts
 * the two claims that are about the SCREEN and cannot be read out of a variable:
 *
 *   1. page 2 is its own page, drawn at page 1's scale. The request was that
 *      DIVISION be OBVIOUS, so a page that renders it in the channel map's
 *      footnote type (FONT_T) would fail the request however correct the state is.
 *   2. the popup owns the screen, and Cancel leaves the page byte-for-byte intact.
 *      For a destructive decision that is the assertion that matters.
 *
 * Like the other probes here this reads NO app state: everything below is pixels.
 *
 *   node tools/trkset2_probe.mjs
 *
 * The canvas stores every pixel byte-swapped (gfx.c writes through swap16), so
 * colours are un-swapped before they are judged.
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
const BTN = { ENV: 2, HOME: 6, OCTDN: 12, OCTUP: 13 };
const EN = { K1: 3, K2: 4 };

const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
/* the web build sets ONE button level per frame, so a press is two levels */
const press = (bit) => { M.web_buttons(1 << bit); step(60); M.web_buttons(0); step(140); };
const enc = (role, n) => { M.web_enc(role, n); step(80); };
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
const body = (fb) => fb.slice(0, W * 216);          /* above the footer band */

/* ---- the reference: the hero, then page 1 ---- */
/* Render before the first grab: nothing has drawn yet, so a grab here returns the
 * framebuffer as it is at boot (all zero). This cost an hour of chasing a "HOME
 * does not come back" bug that did not exist. */
step(600);
const home = grab();
check(ink(home, 0, 240) > 500, 'the hero is drawn', ink(home, 0, 240) + ' px');
press(BTN.ENV);
step(240);
const page1 = grab();
check(!same(page1, home), 'ENV leaves the hero for TRACK SET');

/* ---- page 2 ---- */
press(BTN.ENV);
step(280);
const page2 = grab();
check(!same(page1, page2), 'a second ENV turns to a different page');

const ink1 = ink(page1, 24, 216), ink2 = ink(page2, 24, 216);
check(ink2 > 1200, 'page 2 is a populated page, not a stub', ink2 + ' px of body ink');
/* page 1 is five rows plus the channel map; page 2 is two rows plus a rule, so it
 * is legitimately lighter - but not EMPTY. A page that forgot to draw anything
 * would land near 0. */
check(ink2 < ink1, 'page 2 is lighter than page 1 (it has two rows, not five)',
      ink2 + ' vs ' + ink1);
check(ink2 > ink1 * 0.5, 'but it is the same order of magnitude, not a blank screen',
      ink2 + ' vs ' + ink1);

/* ---- the header says which half you are on ---- */
/* The context line sits right of the track pill (x40..) in the top bar. Page 1
 * says DRUM/MELODIC; page 2 says "TRACK 2/2", which is longer. */
const ctx1 = ink(page1, 4, 22, 40, 150), ctx2 = ink(page2, 4, 22, 40, 150);
check(ctx1 !== ctx2, 'the header context line differs between the pages',
      ctx1 + ' px -> ' + ctx2 + ' px');
check(ctx2 > ctx1, 'page 2\'s is the longer one (it carries the n/2)', ctx1 + ' -> ' + ctx2);

/* ---- DIVISION is drawn at the page's own scale ----
 * The five identity rows put the value in FONT_B at x100. Page 2 keeps that
 * geometry: "increase the obviousness" was a request about size, and a value in
 * the channel map's footnote type would be the old thing on a new page. */
const divRow = ink(page2, 26, 48, 96, 236);
check(divRow > 150, 'the DIVISION value is drawn big, in the value column',
      divRow + ' px at x96..236, y26..48');
/* and the row being edited is highlighted: K1 down moves the amber rule */
enc(EN.K1, 1);
step(160);
const rowSel = grab();
check(!same(body(page2), body(rowSel)), 'K1 visibly moves the highlight');
check(ink(rowSel, 24, 48, 0, 240) !== ink(page2, 24, 48, 0, 240),
      'and the highlight changed the first row band, so K1 is on row 0 -> 1');

/* ---- the footer names the action, per row ----
 * Column 2 of the footer (x64..112) says VALUE on DIVISION and ASK on CLEAR TRACK.
 * A footer that said VALUE on both would be a lie on the row whose K2 opens a
 * decision. */
const footW = (fb) => ink(fb, 224, 236, 64, 112);
check(footW(page2) !== footW(rowSel), 'the footer names a different K2 on the CLEAR row',
      footW(page2) + ' px -> ' + footW(rowSel) + ' px');

/* ---- the popup ---- */
enc(EN.K2, 1);
step(280);
const popup = grab();
check(!same(body(rowSel), body(popup)), 'K2 opens something over the page');
/* It owns the screen: the amber row rule of the page is gone, because the page is.
 * The popup's own canvas is RGB(16,16,21), which reads as 0 above the threshold, so
 * "the page underneath is gone" shows up as the page's ink no longer being there. */
check(ink(popup, 24, 216, 0, 120) < ink(rowSel, 24, 216, 0, 120),
      'the popup replaced the page\'s left column, it did not stack on top of it',
      ink(popup, 24, 216, 0, 120) + ' vs ' + ink(rowSel, 24, 216, 0, 120));
/* both answers are on the screen at once: a yes/no popup that showed only the
 * highlighted one would make K1 a guess */
const rowsInk = ink(popup, 40, 120);
check(rowsInk > 250, 'both answers are drawn at once', rowsInk + ' px in y40..120');

/* the selection is visible */
enc(EN.K1, 1);
step(160);
const popupSel = grab();
check(!same(body(popup), body(popupSel)), 'the selection is visible');

/* ---- Cancel restores the page byte for byte ----
 * The footer is excluded: the knob readout inside it expires on its own timer (it
 * is a readout, not page state), so comparing it would fail for reasons that have
 * nothing to do with the popup. Everything above y216 must be identical. */
press(BTN.OCTDN);
step(280);
const restored = grab();
check(same(body(restored), body(rowSel)), 'OCT- puts the page back byte for byte');
check(!same(body(restored), body(page2)), 'and it is the CLEAR row that came back, not the top');

/* ---- OK on NO changes nothing either ---- */
enc(EN.K2, 1);
step(280);
const again = grab();
check(!same(body(again), body(restored)), 'the popup opens again');
press(BTN.OCTUP);
step(280);
const afterOk = grab();
check(same(body(afterOk), body(rowSel)), 'OK on NO leaves the page as it was too');

/* ---- and the page is still page 2: the popup did not turn it ---- */
check(!same(body(afterOk), body(page1)), 'the page did not turn behind the popup');

/* ---- going home, and coming back, lands on page 2 again ----
 * Rule 4: a feature remembers its page. This is the same property GLOBAL has, and
 * it is worth pinning here because it is what makes a second page feel like part
 * of the feature rather than a mode you get thrown into. */
press(BTN.HOME);
step(1200);                     /* the hero settles: its bar has a fade of its own */
check(same(body(grab()), body(home)), 'HOME comes back to the hero');
press(BTN.ENV);
step(400);
const back = grab();
/* The PAGE is remembered (rule 4) but the ROW is not: arriving re-homes the row
 * cursor to the top, which is what ts_row = 0 on arrival has always meant. So the
 * page comes back as page 2 with row 0 highlighted - not as the CLEAR row that was
 * selected a moment ago. Asserting "exactly as it was" here was wrong about the
 * feature, not about the code. */
check(!same(body(back), body(page1)), 'and ENV returns to TRACK SET page 2, not page 1');
check(ink(back, 24, 216) === ink(page2, 24, 216),
      'the page it returns to is page 2 as first drawn (row 0 up, nothing stale)',
      ink(back, 24, 216) + ' vs ' + ink(page2, 24, 216));

console.log(`${checks} checks, ${fails} FAILED`);
process.exit(fails ? 1 : 0);
