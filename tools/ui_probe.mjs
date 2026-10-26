/* tools/ui_probe.mjs — pixel assertions on the emulator's own screens.
 *
 * The wasm build runs the same painters the firmware does, so what it draws is
 * what the device draws (minus the panel hardware). These assertions are the
 * part of the migration a build cannot check: a screen that compiles, links and
 * blits can still be blank, clamped, or missing a whole band.
 *
 *   node tools/ui_probe.mjs
 *
 * Every check names the failure it would catch. They are geometry and colour,
 * not pictures -- the pictures are tools/ui_page_shot.mjs.
 */
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
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

const W = 240, H = 240;

function step(ms) {
    /* web_render(n) counts sample frames: 44.1 of them per emulated ms */
    const n = Math.round(ms * 44.1);
    for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i));
}
function press(bit) { M.web_buttons(1 << bit); step(40); M.web_buttons(0); step(60); }
function grab() { return Array.from(new Uint16Array(M.memory.buffer, M.web_fb(), W * H)); }

const rgb565 = (r, g, b) => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

/* the painters' palette, as RGB565 (ui_screens.c / ui_tables.c) */
const TB_BG = rgb565(24, 24, 32);
const C_BLACK = 0;

function analyse(fb) {
    const rows = [];
    for (let y = 0; y < H; y++) {
        let n = 0;
        for (let x = 0; x < W; x++) if (fb[y * W + x] !== C_BLACK) n++;
        rows.push(n);
    }
    const bands = [];
    let s = null;
    for (let y = 0; y < H; y++) {
        if (rows[y] && s === null) s = y;
        else if (!rows[y] && s !== null) { bands.push([s, y - 1]); s = null; }
    }
    if (s !== null) bands.push([s, H - 1]);
    return { rows, bands, nonBlack: rows.reduce((a, b) => a + b, 0), colours: new Set(fb).size };
}

M.web_boot();
step(200);

/* ---------- HOME ---------- */
{
    const fb = grab();
    const r = analyse(fb);
    /* THE BUG THIS CAUGHT: fm1-base's canvas defaults to 240x144 and cv_begin
     * CLAMPS to it silently. With APP_CANVAS_PX left at the default, HOME drew
     * ten of its sixteen track rows and the bottom third of the screen stayed
     * black -- no error, no warning, just a shorter screen. */
    check(r.rows[214] > 0 && r.rows[150] > 0,
          'HOME draws past row 144 (canvas is 240x224, not the default 240x144)',
          'row 150 has ' + r.rows[150] + ' px, row 214 has ' + r.rows[214] + ' px');

    /* sixteen track rows: y = 24 + t*12, each 11 px tall, so the last ends at 214.
     * The gutter's track number is the thing that proves the row was drawn: an
     * empty track still prints "TRK n" there. */
    let rowsWithNumber = 0;
    for (let t = 0; t < 16; t++) {
        const y0 = 24 + t * 12;
        let n = 0;
        for (let y = y0; y < y0 + 11; y++)
            for (let x = 2; x < 15; x++)
                if (fb[y * W + x] !== C_BLACK) n++;
        if (n > 8) rowsWithNumber++;
    }
    check(rowsWithNumber === 16, 'HOME has all 16 track rows in the gutter', rowsWithNumber + '/16');

    /* The top bar is one 24px band on every page, and every page draws it.
     * This used to check y0..19 only, which passed on HOME's old 20px bar while
     * GRID and ARP were 16px -- the probe was asserting the very inconsistency
     * that made the header look wrong. Now it measures the bar on all four
     * pages and requires the same height, so a page that drifts fails. */
    check(r.rows[0] === W && r.rows[23] === W && r.rows[24] !== W,
          'HOME top bar spans y0..23 and stops there (24px)',
          `y0=${r.rows[0]} y23=${r.rows[23]} y24=${r.rows[24]}`);

    /* the footer band (y216..239) is drawn too */
    check(r.rows[220] === W && r.rows[239] === W, 'HOME footer band is drawn at y216..239',
          'y220=' + r.rows[220] + ' y239=' + r.rows[239]);
}

/* ---------- GRID ---------- */
press(9);                                     /* B_SEQ */
{
    const fb = grab();
    const r = analyse(fb);
    check(r.nonBlack > 20000, 'GRID draws a full screen', r.nonBlack + ' non-black px');
    check(r.colours > 60, 'GRID uses the track hues', r.colours + ' colours');
    check(r.rows[214] > 0, 'GRID fills to the footer edge', 'y214=' + r.rows[214]);
}

/* ---------- the playhead: 1px white, nothing beside it ----------
 *
 * Track 0 is ch9, so B_SEQ lands on the drum grid. Its playhead used to be a
 * line PLUS a 13px veil column (blend(WHITE, BLACK, 219)); painted after the
 * cells it darkened the very hits it pointed at, and even moved underneath them
 * it left a grey 1px+2px stripe in the cell gaps -- John saw it as "竖线旁边好像
 * 有一条一格宽的灰色bar". It is gone: the melodic grid never had one.
 *
 * The assertion has to wait for the head to be on page 0 and it has to look for
 * the veil's exact colour, not "any grey", so it cannot drift. */
{
    const x0 = 30, cw = 13, VEIL = 0x0421;   /* blend(0xFFFF, 0, 219) */
    press(10);                                /* B_PLAY */
    let found = -1, veilPx = 0;
    for (let k = 0; k < 200 && found < 0; k++) {
        step(20);
        const fb = grab();
        /* the head is the only column that is white for the full lane block
         * (y16..114); it sits at x0 + col*cw, so it can be x0 itself when the
         * page is at column 0 -- that is the head, not a border */
        for (let x = x0; x < x0 + 16 * cw && found < 0; x++) {
            let white = 0;
            for (let y = 16; y < 115; y++) if (fb[y * W + x] === 0xFFFF) white++;
            if (white > 80) found = x;
        }
        if (found >= 0) {
            for (let y = 16; y < 115; y++)
                for (let x = Math.max(0, found - 1); x <= Math.min(W - 1, found + cw); x++)
                    if (fb[y * W + x] === VEIL) veilPx++;
        }
    }
    check(found >= 0, 'GRID playhead is a 1px white column', found >= 0 ? 'x=' + found : 'never found');
    check(veilPx === 0, 'GRID playhead has no veil beside it', veilPx + ' veil px');
    press(10);                                /* B_PLAY again: stop */
    step(40);
}

/* ---------- EDIT layer (same screen, louder) ---------- */
press(4);                                     /* B_EDIT */
{
    const r = analyse(grab());
    check(r.nonBlack > 20000, 'EDIT layer draws', r.nonBlack + ' non-black px');
}

/* ---------- ARP: the ring ---------- */
press(4);                                     /* back to base layer */
press(8);                                     /* B_ARP */
{
    const fb = grab();
    const cx = 120, cy = 112;
    /* the guide circle: 64 single-pixel points at r+4 = 80 */
    let on80 = 0, onRing = 0;
    const radii = [];
    for (let y = 28; y < 196; y++) {
        for (let x = 0; x < W; x++) {
            if (fb[y * W + x] === C_BLACK) continue;
            const d = Math.hypot(x - cx, y - cy);
            if (d > 4) radii.push(d);
            if (d >= 79.5 && d <= 81) on80++;
            if (d >= 73 && d <= 79) onRing++;
        }
    }
    check(onRing > 0, 'ARP draws its ring (pulses/ticks at r=76)', onRing + ' px near r76');
    check(on80 >= 40, 'ARP ring guide circle is complete (64 pts at r=80)', on80 + ' px at r80');

    /* the guard that would make all of this pass while the ring is wrong: a ring
     * that is not round. The old RING_SIN table was a 70.75-period sine, so the
     * radius swung ~33%; the Q12 table holds it to the pixel grid. */
    const rs = radii.filter(d => d >= 70 && d <= 85);
    check(rs.length > 100, 'ARP ring has enough points to judge its shape', rs.length + ' px');
}

/* ---------- the header must be the same height on every page ---------- */
/* This is the check that had to exist: the old bar was 20px on HOME, 16px on
 * GRID and 16px on ARP, and nothing noticed because each page was measured on
 * its own with its own number baked into the assertion.
 *
 * Measure the painter's floor rule, not "how far down is it full width": HOME's
 * hero band starts right below the bar and is also full width, so a width test
 * runs straight past the bar and reported 36. The rule at y=23 is drawn by the
 * shared painter and by nothing else, so it is the honest boundary. */
const SEP_ROW = 0x2946;        /* RGB565(42,42,52) = 0x2946, as the C macro computes it */
const swap16 = (c) => ((c & 0xff) << 8) | ((c >> 8) & 0xff);
/* the canvas stores swapped values (gfx.c writes every pixel through swap16),
 * so compare against the swapped constant */
const SEP_IN_FB = swap16(SEP_ROW);
function barBottom(fbv) {
    for (let y = 0; y < 40; y++) {
        if (fbv[y * W] !== SEP_IN_FB) continue;
        let uniform = true;
        for (let x = 1; x < W; x++) if (fbv[y * W + x] !== SEP_IN_FB) { uniform = false; break; }
        if (uniform) return y;          /* the floor rule: bar is 0..y */
    }
    return -1;
}
const bottoms = {};
{
    bottoms.HOME = barBottom(grab());
    press(9); bottoms.GRID_MEL = barBottom(grab());
    press(8); bottoms.ARP = barBottom(grab());
    press(6);                                       /* back to HOME */
}
const bv = Object.entries(bottoms);
const agree = bv.every(([, v]) => v === 23);
check(agree, 'every page closes its header with the same rule at y=23 (bar = 24px)',
      bv.map(([k, v]) => `${k}=${v < 0 ? 'none' : v}`).join(' '));

/* and nothing may paint over that rule: the first track row's selection
 * outline used to be drawn at y-1 and landed on it */
{
    const fb2 = grab();
    let clean = true;
    for (let x = 0; x < W; x++) if (fb2[23 * W + x] !== SEP_IN_FB) { clean = false; break; }
    check(clean, 'nothing paints over the header floor rule', clean ? 'y=23 uniform' : 'y=23 has a foreign pixel');
}

/* ---------- back to HOME ---------- */
press(6);                                     /* B_HOME */
{
    const fb2 = grab();
    const r = analyse(fb2);
    /* y214 is the last row's bottom; y215 is the 1px gap before the footer */
    check(r.rows[214] > 0, 'HOME still draws after a round trip', 'y214=' + r.rows[214]);
}

console.log(fails ? `\n== ${fails} UI PROBE FAILURES (${checks} checks) ==` : `\nUI PROBE OK (${checks} checks)`);
process.exit(fails ? 1 : 0);
