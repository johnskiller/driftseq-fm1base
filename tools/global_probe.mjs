/* tools/global_probe.mjs — pixel assertions on the GLOBAL page, in the SHIPPED
 * wasm. The C test (tests/trkset.txt) asserts the fields; this asserts that the
 * page those fields live on is drawn where it says, and that its three channel
 * states can be told apart by pixels alone (the first version could not).
 *
 *   node tools/trkset_probe.mjs
 *
 * The canvas stores every pixel byte-swapped (gfx.c writes through swap16), so
 * every colour below is compared in its swapped form. Getting that wrong is how
 * the first draft of this file "passed" a filled cell as hollow.
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

const W = 240, H = 240;
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const press = (bit) => { M.web_buttons(1 << bit); step(40); M.web_buttons(0); step(60); };
const enc = (role, n) => { M.web_enc(role, n); step(40); };
const grab = () => Array.from(new Uint16Array(M.memory.buffer, M.web_fb(), W * H));

const swap = (c) => ((c & 0xff) << 8) | ((c >> 8) & 0xff);
const rgb565 = (r, g, b) => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
const stored = (r, g, b) => swap(rgb565(r, g, b));

const ROWBAND = stored(26, 26, 34);      /* the edited row's band */
const px = (fb, x, y) => fb[y * W + x];
/* "text" = anything that is neither the black background nor the row band */
const textAt = (fb, x, y) => { const v = px(fb, x, y); return v !== 0 && v !== ROWBAND; };

const bandsOf = (ys) => {
    const out = []; let cur = [];
    for (const y of ys) {
        if (cur.length && y - cur[cur.length - 1] > 2) { out.push([cur[0], cur[cur.length - 1]]); cur = []; }
        cur.push(y);
    }
    if (cur.length) out.push([cur[0], cur[cur.length - 1]]);
    return out;
};
const textRows = (fb, y0, y1, x0, x1) => {
    const ys = [];
    for (let y = y0; y < y1; y++)
        for (let x = x0; x < x1; x++) if (textAt(fb, x, y)) { ys.push(y); break; }
    return ys;
};

M.web_boot();                              /* the page's own boot: palette_set + app_init.
                                             * Without it every palette colour (C_GRAY, C_AMB,
                                             * C_HI) is pal[]'s zeros, so the labels and values
                                             * draw in black on black and the screen looks empty. */
step(400);
const home = Array.from(new Uint16Array(M.memory.buffer, M.web_fb(), W * H));
/* ---------- the door: GLO+HOME ---------- */
const mk = () => Array.from(new Uint16Array(M.memory.buffer, M.web_fb(), W * H));
/* web_buttons is a LEVEL, not an edge: the app samples it once a frame, so GLO
 * must be held across a frame of its own, and the chord across another. Setting
 * both in one step made GLO+HOME land as "HOME pressed with GLO not yet seen". */
M.web_buttons(1 << 5);                     /* B_GLO held */
step(40);
M.web_buttons((1 << 5) | (1 << 6));        /* + B_HOME: the chord */
step(40);
M.web_buttons(0);                          /* the release edge: the page stays */
step(60);
const page = mk();
check(page.some((v, i) => v !== home[i]), 'GLO+HOME opens a different screen');

/* ---------- six page dots, the fifth lit ---------- */
/* The dots live at y=12 (hdr_dots). Six of them means the row is wider than the
 * five the other pages had; the last dot must still be inside the bar. */
{
    const ink = [];
    for (let x = 200; x < W; x++) if (textAt(page, x, 12)) ink.push(x);
    const bands = bandsOf(ink);
    check(bands.length >= 1, 'the page dots are drawn', JSON.stringify(bands));
    let last = -1;
    for (let x = 200; x < W; x++)
        for (let y = 5; y < 19; y++) if (textAt(page, x, y)) { last = x; break; }
    check(last <= W - 2, 'the dots stay inside the bar (six, not five)', 'rightmost ink x=' + last + ' of ' + W);
}

/* ---------- three rows at 34px pitch ---------- */
/* The radio switch moved to page 2 (John 2026-10-30), so page 1 lost a row and
 * spent the space on air: the three rows are 34px apart, not 26. */
{
    const lb = bandsOf(textRows(page, 24, 140, 10, 70));   /* the three settings, not the readings under them */
    check(lb.length === 3, 'three label rows', JSON.stringify(lb));
    const want = [33, 67, 101];                            /* FONT_M caps: 16px tall */
    check(lb.length === 3 && lb.every(([a, b], i) => a === want[i] && b === a + 15),
          'the rows are 34px apart, each 16px tall', JSON.stringify(lb));
    let worst = 0, missing = 0;
    for (const y of want) {
        let lastv = -1;
        for (let x = 100; x < W; x++)
            for (let dy = 0; dy < 16; dy++) if (textAt(page, x, y + dy)) { lastv = x; break; }
        if (lastv < 0) missing++;
        if (lastv > worst) worst = lastv;
    }
    check(missing === 0, 'every row draws a value at x>=100', missing + ' rows without one');
    /* The value column must start AFTER the longest label, not at a round number.
     * "GM SYNTH" in FONT_M ends at x=124, and the value used to start at 100: the
     * two were drawn through each other (John 2026-10-30, "第一页的value需要向右
     * 移动一点，现在和左边的label重叠了，主要是第三行GM SYNTH比较长"). Measure the
     * gap between the last label pixel and the first value pixel on every row, so
     * a bigger font or a longer label fails here instead of on his screen. */
    {
        let worst = 999;
        for (const y of want) {
            let labelEnd = -1, valueStart = -1;
            for (let x = 12; x < 236; x++) {
                let ink = false;
                for (let dy = 0; dy < 16; dy++) if (textAt(page, x, y + dy)) { ink = true; break; }
                if (!ink) continue;
                if (x < 130) labelEnd = x;              /* labels all live left of 130 */
                else if (valueStart < 0 && x >= 130) valueStart = x;
            }
            if (labelEnd > 0 && valueStart > 0 && valueStart - labelEnd < worst)
                worst = valueStart - labelEnd;
        }
        check(worst >= 8, 'the value column clears the longest label',
              'tightest label-to-value gap ' + worst + 'px');
    }
    check(worst < 238, 'no value runs off the right edge', 'rightmost value pixel x=' + worst);
    /* the three readings sit below the settings with no band: a knob must not
     * look like it edits them, so no ROWBAND pixel may appear there. */
    let bands = 0;
    for (let y = 142; y < 196; y++) if (px(page, 8, y) === ROWBAND) bands++;
    check(bands === 0, 'the readings have no row band (they are not settings)', bands + ' band rows below y142');
}

/* ---------- K1 moves the highlight 24px, dead stop at the fifth row ---------- */
{
    /* A band is a RUN, not a row: the header's own bottom edge uses a colour
     * close enough to ROWBAND to fool a single-row test (it reported "band at
     * 20", which is the header). Ten consecutive rows is the smallest a 25px
     * band can produce with 16px text knocked out of the middle. */
    const bandTop = (fb) => {
        let run = 0;
        for (let y = 26; y < 145; y++) {
            if (px(fb, 8, y) === ROWBAND) { if (++run >= 10) return y - run + 1; }
            else run = 0;
        }
        return -1;
    };
    const a0 = bandTop(page);
    check(a0 > 0, 'the edited row has a band', 'band at ' + a0);
    /* ...and the 2px amber rule on its left edge, exactly as TRACK SET draws it.
     * John 2026-10-30: "Track set页面每行前有个竖线，配合选中行颜色，很美观，
     * GLOBAL页面也要保持同样的风格". A shade alone is not the house style. */
    {
        let rule = 0;
        for (let y = a0; y < a0 + 25; y++)
            if (px(page, 4, y) === stored(238, 137, 24)) rule++;
        check(rule >= 23, 'the band carries the amber rule on its left edge',
              rule + ' of 25 rows at x=4');
    }
    enc(3, 1);                              /* EN_K1 */
    const a1 = bandTop(mk());
    /* 32, not 34: the band is 25px tall at y-6 with a 34px pitch, so consecutive
     * bands overlap by 9px and the first row of the NEW band is the first row that
     * is not also part of the old one... the measured step is 32. What matters is
     * that it is the pitch, not 26 (the old four-row page) and not 18. */
    check(a1 === a0 + 32, 'K1 moves the highlight one row (~34px pitch)', a0 + ' -> ' + a1);
    enc(3, 20);
    const a2 = bandTop(mk());
    check(a2 === a0 + 66, 'K1 stops at the third row, it does not wrap', 'band at ' + a2);
    enc(3, -20);
}

/* ---------- the two lines that explain the page ---------- */
{
    /* The notes are drawn in (120,120,134), which is NOT the row band's
     * (26,26,34) - but textAt() only excludes black and the band, so a dim grey
     * line still counts. Measure "any ink", not "text". */
    /* cv_text draws AT the baseline: the ink of a line at y=152 is y154..164,
     * so measuring the single row y=152 finds the 2px gap above the caps. */
    const line = (y) => {
        let n = 0;
        for (let yy = y; yy < y + 11; yy++) for (let x = 10; x < 230; x++) if (px(page, x, yy) !== 0) n++;
        return n;
    };
    /* Three readings (CPU / XRUNS / PATTERN) sit under the four settings, and
     * the tap-tempo note under them. None of them may overlap a row. */
    check(line(146) > 60, 'the CPU reading is drawn', line(146) + ' px');
    check(line(163) > 60, 'the XRUNS reading is drawn', line(163) + ' px');
    check(line(180) > 60, 'the PATTERN reading is drawn', line(180) + ' px');
    check(line(200) > 30, 'the tap-tempo note is drawn under them', line(200) + ' px');
    /* the note's ink ends at y203; the band ends at 216 and the footer starts
     * below it, so 207..215 must be empty on both pages. */
    const gap = (y0) => {
        let n = 0;
        for (let y = y0; y < 216; y++) for (let x = 4; x < 236; x++) if (px(page, x, y) !== 0) n++;
        return n;
    };
    check(gap(211) === 0, 'nothing spills between the note and the band edge', gap(211) + ' px in y211..215');
}

/* ---------- footer names this page's knobs ---------- */
{
    const ink = (x0) => {
        let n = 0;
        for (let y = 224; y < 232; y++) for (let x = x0; x < x0 + 50; x++) if (textAt(page, x, y)) n++;
        return n;
    };
    check(ink(8) > 20, 'footer K1 says a word (ROW)', ink(8) + ' px');
    check(ink(69) > 20, 'footer K2 says a word (EDIT)', ink(69) + ' px');
    check(ink(130) === 0, 'footer K3 is blank (nothing to pick here)', ink(130) + ' px');
    check(ink(189) === 0, 'footer K4 is blank', ink(189) + ' px');
}

/* ---------- the header has no track pill on this page ---------- */
/* John 2026-10-30: "GLABAL header前面的药丸1就不需要了". The pill is a SHAPE, so
 * its absence is a pixel fact: no filled chip in the bar's left 30px, and the
 * context word ends before x100. */
{
    let pillpx = 0;
    for (let x = 6; x < 30; x++) for (let y = 5; y < 21; y++) {
        const c = px(page, x, y);
        if (c !== 0 && c !== stored(20, 20, 28) && c !== stored(24, 24, 32)) pillpx++;
    }
    /* the context text starts at x=6 now (where the pill was), so it DOES paint
     * there: what must be gone is the 24x18 rounded FILL, i.e. most of the
     * rectangle. Text is a minority of the box; a pill fills it. */
    /* a pill fills ~24x18 = 432 px even after the letter is knocked out of it;
     * the context word paints a few dozen strokes there. 250 separates them with
     * room to spare on both sides. */
    check(pillpx < 250, 'no track pill on the left of the GLOBAL header', pillpx + ' filled px in x6..29');
    let ctxEnd = -1;
    for (let x = 6; x < 140; x++) for (let y = 5; y < 21; y++) if (textAt(page, x, y)) { ctxEnd = x; break; }
    check(ctxEnd > 40 && ctxEnd < 110, 'the context word starts where the pill was, not after it',
          'rightmost header ink x=' + ctxEnd);
}

/* ---------- page 2: the radio, its switch, and the devices it finds ---------- */
/* John 2026-10-30: "BLE的开关可以移到第二页，这样第一页空间大一点，第二页也不至于
 * 这么空旷". So this block asserts the new shape of page 2: row 0 is the switch,
 * the device rows follow, K1 walks them, K2 turns only the switch, the state line
 * shares the bottom line with the SAVE hint, and the footer offers ON/OFF only
 * where ON/OFF exists. */
{
    const dots = (fb) => { const ink = []; for (let x = 200; x < W; x++) if (textAt(fb, x, 12)) ink.push(x); return bandsOf(ink).length; };
    check(dots(page) >= 5, 'page 1 still shows the six page dots', dots(page) + ' bands');
    M.web_buttons(1 << 5); step(40);
    M.web_buttons((1 << 5) | (1 << 6)); step(40);
    M.web_buttons(0); step(60);
    const p2 = mk();
    check(p2.some((v, i) => v !== page[i]), 'the same chord flips to a different page');

    /* row 0 is the switch: its label is on the left and its value on the right,
     * drawn in the shape of a settings row. y=34 baseline, like page 1's row 0. */
    const rowInk = (fb, y0, y1, x0, x1) => { let n = 0; for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) if (textAt(fb, x, y)) n++; return n; };
    check(rowInk(p2, 34, 48, 12, 140) > 30, 'row 0 names the switch', rowInk(p2, 34, 48, 12, 140) + ' px');
    check(rowInk(p2, 34, 48, 150, 220) > 10, 'row 0 shows its value on the right', rowInk(p2, 34, 48, 150, 220) + ' px');
    /* and the band is on row 0 when the page opens (gl_row resets to 0) */
    let b0 = -1;
    for (let y = 26; y < 60; y++) if (px(p2, 8, y) === ROWBAND) { b0 = y; break; }
    check(b0 > 0, 'the switch row is the one the band is on when the page opens', 'band at ' + b0);
    {
        let rule = 0;
        for (let y = b0; y < b0 + 17; y++)
            if (px(p2, 4, y) === stored(238, 137, 24)) rule++;
        check(rule >= 15, 'page 2 bands carry the same amber rule', rule + ' of 17 rows at x=4');
    }

    /* no device yet: the page says so under the switch rather than leaving an
     * empty list. The host's stand-in appears half a second into the scan. */
    check(rowInk(p2, 52, 68, 12, 200) > 10, 'the empty list says what the radio is doing',
          rowInk(p2, 52, 68, 12, 200) + ' px at y52..67');
    step(900);
    const p2b = mk();
    check(rowInk(p2b, 52, 68, 12, 140) > 20, 'a device appeared on row 1',
          rowInk(p2b, 52, 68, 12, 140) + ' px');
    let bars = 0;
    for (let x = 200; x < 236; x++) for (let y = 50; y < 70; y++) if (textAt(p2b, x, y)) bars++;
    check(bars > 8, 'the found device shows signal bars', bars + ' px in x200..235');

    /* the state line moved to the BOTTOM, beside the SAVE hint: it used to sit
     * above row 0 where it read as a list row ("SCANNING" as a device name). The
     * bottom line is y200 in FONT_T, so measure 202..209. */
    const bottom = (fb) => { let n = 0; for (let y = 202; y < 210; y++) for (let x = 12; x < 236; x++) if (textAt(fb, x, y)) n++; return n; };
    check(bottom(p2b) > 30, 'the state line is drawn on the bottom line', bottom(p2b) + ' px');
    /* the header ends at y24; row 0's caps start at y36. Nothing may be drawn in
     * between - that gap is where the state line used to sit and read as a row. */
    let above = 0;
    for (let y = 26; y < 34; y++) for (let x = 12; x < 200; x++) if (textAt(p2b, x, y)) above++;
    check(above === 0, 'and nothing is drawn between the header and row 0', above + ' px in y26..33');

    /* K1 walks: row 0 -> row 1, and the band follows. */
    enc(3, 1);
    const p2c = mk();
    let b1 = -1;
    for (let y = 40; y < 90; y++) if (px(p2c, 8, y) === ROWBAND) { b1 = y; break; }
    check(b1 === b0 + 18, 'K1 moves the band onto the device row (18px)', b0 + ' -> ' + b1);
    /* K1 dead-stops at the last row */
    enc(3, 9);
    let b2 = -1;
    for (let y = 40; y < 90; y++) if (px(p2c, 8, y) === ROWBAND) { b2 = y; break; }
    const p2d = mk();
    let b3 = -1;
    for (let y = 40; y < 90; y++) if (px(p2d, 8, y) === ROWBAND) { b3 = y; break; }
    check(b3 === b1 && b3 > 0, 'K1 stops at the last row, it does not wrap', b1 + ' -> ' + b3);

    /* the footer on this page: K1 is DEVICE, and K2 says ON/OFF only while the
     * switch row is selected - a device row has no value to turn. */
    const footInk = (fb, x0) => { let n = 0; for (let y = 224; y < 232; y++) for (let x = x0; x < x0 + 50; x++) if (textAt(fb, x, y)) n++; return n; };
    check(footInk(p2c, 8) > 20, 'footer K1 says a word (DEVICE)', footInk(p2c, 8) + ' px');
    check(footInk(p2c, 120) === 0, 'footer K2 is blank on a device row', footInk(p2c, 120) + ' px');
    enc(3, -9);                              /* back to the switch */
    const p2e = mk();
    check(footInk(p2e, 120) > 20, 'footer K2 says ON/OFF on the switch row', footInk(p2e, 120) + ' px');

    /* SAVE connects the picked device. SAVE must be a PRESS ACROSS FRAMES:
     * web_buttons is a level, and a press and release in one step is a level that
     * never changed. */
    enc(3, 1);                               /* onto the device row */
    const before = mk();
    M.web_buttons(1 << 7); step(100);
    M.web_buttons(0); step(200);
    const after = mk();
    check(after.some((v, i) => v !== before[i]), 'SAVE changes the page (connect asked for)');
    /* The page flips the hint as soon as the connect is ASKED for (btdev_busy is
     * true from CONNECTING on), and the state word changes again at READY. */
    const hint = (fb) => {
        const bits = [];
        for (let y = 202; y < 210; y++) for (let x = 150; x < 236; x++) {
            const v = px(fb, x, y);
            if (v !== 0 && v !== ROWBAND) bits.push(x + ',' + y);
        }
        return bits.join('|');
    };
    check(hint(before) !== hint(after), 'the hint flips from CONNECT to LET GO on SAVE',
          hint(before).length + ' -> ' + hint(after).length + ' ink px');
    step(1400);                              /* the stand-in takes 0.3 s */
    const linked = mk();
    /* the link row must survive the scan stopping: without it the cursor fell
     * back to the switch and this page could no longer let the device go. */
    check(rowInk(linked, 52, 68, 12, 140) > 20, 'the connected device still has a row',
          rowInk(linked, 52, 68, 12, 140) + ' px at y52..67');
    const stateWord = (fb) => {
        let n = 0;
        for (let y = 202; y < 210; y++) for (let x = 12; x < 140; x++) {
            const v = px(fb, x, y);
            if (v !== 0) n++;
        }
        return n;
    };
    check(stateWord(linked) !== stateWord(after), 'the state word reports the link coming up',
          stateWord(after) + ' -> ' + stateWord(linked) + ' px');

    /* and back to page 1 with the chord */
    M.web_buttons(1 << 5); step(40);
    M.web_buttons((1 << 5) | (1 << 6)); step(40);
    M.web_buttons(0); step(60);
    const back1 = mk();
    let band1 = 0;
    for (let y = 24; y < 140; y++) if (px(back1, 8, y) === ROWBAND) band1++;
    check(band1 > 10, 'the chord returns to page 1 (its row band is back)', band1 + ' band rows');
    /* and page 1 does not carry the switch any more */
    check(rowInk(back1, 100, 120, 12, 140) > 20, 'page 1 ends at GM SYNTH (three rows)',
          rowInk(back1, 100, 120, 12, 140) + ' px at the third row');
}

/* ---------- a bare HOME leaves ---------- */
/* press() drives one bit level across frames, which is right for a single
 * button. HOME here is the release that follows the chord's opening press, so
 * gl_opening has already swallowed it; a fresh press/release pair is a bare
 * HOME and must leave. */
M.web_buttons(1 << 6); step(40);
M.web_buttons(0); step(60);
{
    /* Not "equals the first screenshot": the knobs above moved the row and the
     * band, and HOME shows the selected track, so a few pixels legitimately
     * differ. What must be true is that GLOBAL's five rows are gone - its
     * page dots (six) and its two notes are the page's own marks. */
    const back = mk();
    let rows = 0;
    for (let y = 30; y < 145; y++) if (px(back, 8, y) === ROWBAND) rows++;
    check(rows === 0, 'a bare HOME leaves: no row band anywhere', rows + ' band rows left');
    let notes = 0;
    for (let x = 10; x < 230; x++) if (px(back, x, 155) !== 0) notes++;
    check(notes === 0, "and the page's own notes are gone", notes + ' px at y155 (the swing note line)');

    /* Removing GLOBAL's pill must not remove anybody else's. The five track pages
     * are about one track and their pill is what says so; their hdr_t is a stack
     * struct that is now `= {0}`, so the "no pill" flag is 0 there for free. Walk
     * to TRACK SET - the page whose pill was easiest to lose - and assert the
     * filled rectangle is still in the header's left corner. */
    M.web_buttons(1 << 2); step(60);         /* TRK: TRACK SET */
    M.web_buttons(0); step(60);
    const ts = mk();
    let chip = 0;
    for (let y = 4; y < 22; y++) for (let x = 6; x < 30; x++) {
        const c = px(ts, x, y);
        if (c !== 0 && c !== stored(20, 20, 28) && c !== stored(24, 24, 32)) chip++;
    }
    check(chip > 250, 'the track pages still carry their numbered pill',
          chip + ' filled px in x6..29 of TRACK SET');
}

console.log('\n' + (fails ? 'FAILURES: ' + fails + '/' + checks : 'GLOBAL PROBE OK (' + checks + ' checks)'));
process.exit(fails ? 1 : 0);
