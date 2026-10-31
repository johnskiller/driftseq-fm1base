/* tools/preset_probe.mjs — the PRESETS knob, in the SHIPPED wasm.
 *
 * John's two reports, in his words:
 *   "webemu上的版本PRESETS没起作用啊"
 *   "hero，arp，seq画面PRESET起作用，但是在track set和scale画面就不起作用了，
 *    我认为PRESET是一个全局旋钮，应该和SELECT一样任何时候都起作用"
 *
 * The C tests (tests/preset.txt) assert the app's MIDI. This asserts the thing he
 * was looking at: that in the wasm the page actually serves, a detent on PRESET
 * reaches the app on EVERY page - including the three that take all four knobs
 * for themselves - and puts a program change on the wire with a popup on screen.
 *
 *   node tools/preset_probe.mjs
 *
 * Three traps this file exists to not fall into again:
 *   - build/emu/app.wasm can be STALE. The first time PRESETS "did nothing" in
 *     the emu, the wasm predated ui_preset.c by half an hour. The stamp is
 *     printed, and the probe refuses a wasm older than ui/ui_preset.c.
 *   - web_buttons is a LEVEL and the out ring is drained by the app, so both are
 *     read across frames, never inside one.
 *   - the wasm exports no state readers (no app_mode / app_expect). Page identity
 *     is read off the footer, whose column count differs per page: GLOBAL names
 *     ROW/EDIT (2), TRACK SET ROW/VALUE/MUTE (3), SCALE KIND/KEY/OFC (3) and ARP
 *     K/N/ROT/LINK (4) - and ARP's are single letters, so its ink is far smaller.
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const WASM = path.join(HERE, '..', 'build', 'emu', 'app.wasm');
const SRC  = path.join(HERE, '..', 'ui', 'ui_preset.c');
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
check(wasmAge >= srcAge, 'the wasm is newer than ui/ui_preset.c',
      wasmAge.toISOString() + ' vs ' + srcAge.toISOString());

const { instance } = await WebAssembly.instantiate(fs.readFileSync(WASM), {
    wasi_snapshot_preview1: {
        clock_time_get: () => 0, proc_exit: () => {}, fd_write: () => 0,
        fd_close: () => 0, environ_get: () => 0, environ_sizes_get: () => 0,
    },
    env: {},
});
const M = instance.exports;

const W = 240;
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const press = (bit) => { M.web_buttons(1 << bit); step(40); M.web_buttons(0); step(60); };
const enc = (role, n) => { M.web_enc(role, n); step(40); };
const grab = () => Array.from(new Uint16Array(M.memory.buffer, M.web_fb(), W * W));

const swap = (c) => ((c & 0xff) << 8) | ((c >> 8) & 0xff);
const px = (fb, x, y) => fb[y * W + x];
/* luminance on the un-swapped word: TB_BG (24,24,32) is far below footnote gray */
const lum = (v) => {
    const c = swap(v), r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (r * 255 / 31 + g * 255 / 63 + b * 255 / 31) / 3;
};
const ink = (fb, y0, y1, x0, x1) => {
    let n = 0;
    for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) if (lum(px(fb, x, y)) > 90) n++;
    return n;
};
/* columns of footer text: runs of columns that carry ink, 8px apart or more */
const footCols = (fb) => {
    const cols = [];
    for (let x = 0; x < W; x++) {
        let hit = 0;
        for (let y = 224; y < 236; y++) if (lum(px(fb, x, y)) > 90) hit++;
        if (hit) cols.push(x);
    }
    let n = 0, last = -99;
    for (const x of cols) { if (x - last > 8) n++; last = x; }
    return n;
};
const differs = (a, b) => a.some((v, i) => v !== b[i]);

/* every program change the app put on the wire since the last call */
const takePc = () => {
    const n = M.web_out_n();
    const p = new Uint32Array(M.memory.buffer, M.web_out_pkts(), n);
    const out = [];
    for (let i = 0; i < n; i++) {
        const st = (p[i] >> 8) & 0xFF;
        if ((st & 0xF0) === 0xC0)
            out.push('C' + (st & 0x0F) + ' ' + ((p[i] >> 16) & 0xFF).toString(16).padStart(2, '0'));
    }
    M.web_out_clear();
    return out;
};

const EN = { SELECT: 0, ALGO: 1, PRESET: 2, K1: 3 };
/* plat.h's button order, which is what web_buttons' bit index means:
 * B_FX 0, B_SEL 1, B_ENV 2, B_LFO 3, B_EDIT 4, B_GLO 5, B_HOME 6, B_SAVE 7,
 * B_ARP 8, B_SEQ 9, B_PLAY 10, B_REC 11. ENV opens TRACK SET, SEL opens SCALE
 * (the panel prints SEL, the code calls it SCL). */
const BTN = { SEL: 1, ENV: 2, GLO: 5, HOME: 6, ARP: 8 };

M.web_boot();
step(600);                                  /* the demo pattern loads */
takePc();
/* Boot leaves the panel on track 0, and the demo's tracks 0..2 are all on the
 * drum channel (ch9). A kit is a nine-slot set now, not 128 programs, so the
 * page-by-page section below (one detent -> one program, three detents ->
 * three) has to run on a MELODIC track or it would be testing the kit walk
 * instead. Track 3 is the bass on ch0. SELECT is relative and dead-stops, so
 * +3 from 0 lands exactly there. */
for (let i = 0; i < 3; i++) enc(EN.SELECT, 1);
step(200);
takePc();
const home = grab();

/* ---- the pages, each entered by the button that documents it ---- */
/* Page identity without a state reader: each page's own body ink is a distinct
 * number (HOME 2144, ARP 1213, SCALE 9470, TRACK SET 3711, GLOBAL page 1 7125 in
 * the build this was written against), so "am I on the page I think I am" is a
 * pixel question. A tolerance keeps it from failing over a one-glyph change. */
const page = (name, enter, leave, wantBody) => {
    enter();
    step(160);
    const up = grab();
    const body = ink(up, 0, 215, 0, W);
    check(differs(up, home) || name === 'HOME', name + ': the page is not HOME', '');
    /* wantBody < 0 = "this page has no stable ink fingerprint" (GLOBAL's two
     * pages differ, and rule 4 lets a page remember itself). tests/pages.txt
     * pins those numbers directly. */
    if (wantBody >= 0)
        check(Math.abs(body - wantBody) <= wantBody * 0.25, name + ': this is the page it should be',
              body + ' px, want ~' + wantBody);
    takePc();                                /* entering states nothing; clear anyway */

    const pcs = [];
    let popped = 0;
    for (let i = 0; i < 3; i++) {
        enc(EN.PRESET, 1);
        pcs.push(...takePc());
        if (i === 0) popped = ink(grab(), 0, 215, 0, W);
    }
    check(popped > 400, name + ': a detent opens the popup', popped + ' px inside the canvas');
    check(pcs.length === 3, name + ': every detent is a program change', pcs.join(' | '));
    check(new Set(pcs).size === 3, name + ': three detents, three programs', pcs.join(' | '));
    {
        const chans = new Set(pcs.map(s => s.split(' ')[0]));
        check(chans.size === 1, name + ': all three on one channel', [...chans].join(' '));
    }
    step(2600);                              /* the popup closes by itself */
    const after = ink(grab(), 0, 215, 0, W);
    check(Math.abs(after - body) <= body * 0.25, name + ': and closes onto the page underneath',
          after + ' px, page is ' + body + ', popup was ' + popped);
    leave();
    step(160);
};

page('HOME',      () => {},                                  () => {},                         2144);
/* Leaving is HOME's job now (docs/UI_GRAMMAR.md rule 4): a page key on its own
 * page turns the page, and these features have one page each, so it stays. */
page('ARP',       () => press(BTN.ARP),                      () => press(BTN.HOME),            1213);
page('SCALE',     () => press(BTN.SEL),                      () => press(BTN.HOME),            9470);
page('TRACK SET', () => press(BTN.ENV),                      () => press(BTN.HOME),            3711);
/* GLOBAL remembers its page (docs/UI_GRAMMAR.md rule 4), so this probe cannot
 * assume it opens on page 1: enter, look, and turn with the same chord once if
 * page 2 is up. The body-ink number below is page 1's. */
/* The web build sets ONE button level per frame, so a chord cannot use press():
 * it holds GLO across HOME's whole down-and-up. */
const chord = () => {
    M.web_buttons(1 << BTN.GLO); step(40);              /* GLO down: glo_held */
    M.web_buttons((1 << BTN.GLO) | (1 << BTN.HOME)); step(40);   /* HOME down */
    M.web_buttons(1 << BTN.GLO); step(40);              /* HOME up */
    M.web_buttons(0); step(120);                        /* GLO up */
};
/* GLOBAL remembers its page (rule 4) and its two pages carry different amounts
 * of body ink, so this probe does not pin a page number: it checks that the
 * chord got off HOME and onto GLOBAL, and leaves the page number to
 * tests/pages.txt, which reads it directly. -1 = "not a body-ink claim". */
const enterGlobal = () => chord();

page('GLOBAL',    () => enterGlobal(),
                  () => press(BTN.HOME),                                                       -1);


/* ---- SELECT states the program: the voice follows the track ---- */
{
    press(BTN.ENV);                          /* TRACK SET, where John saw it */
    step(160);
    takePc();
    const seen = [];
    for (let i = 0; i < 5; i++) {
        enc(EN.SELECT, 1);
        seen.push(...takePc());
    }
    check(seen.length >= 1, 'SELECT: walking the tracks states programs', seen.join(' | '));
    {
        const chans = new Set(seen.map(s => s.split(' ')[0]));
        check(chans.size >= 2, 'SELECT: and more than one channel was stated', [...chans].join(' '));
    }
    step(2600);
}

/* ---- a drum track: nine kits, and bank select on the wire ---- */
/* A program change on the drum channel selects a KIT. GM Level 1 defines
 * exactly one, GM2 names nine, so the knob walks the nine and nothing else;
 * and the wire needs CC0 120 before the PC or no module changes kit at all.
 * John 2026-10-30: "我建议drum的preset就用有值的那9个" + "它们需要发bank select才行". */
{
    /* back to track 0 (drum, ch9). SELECT is relative and dead-stops, so walk
     * well past the first track and let it land on 0, then read it back. */
    enc(EN.SELECT, -16);
    step(200);
    takePc();                                /* entering may state a program */

    const kits = [];
    let bank0 = 0, bank32 = 0;
    for (let i = 0; i < 8; i++) {            /* eight detents = all nine kits */
        enc(EN.PRESET, 1);
        const n = M.web_out_n();
        const p = new Uint32Array(M.memory.buffer, M.web_out_pkts(), n);
        for (let k = 0; k < n; k++) {
            const st = (p[k] >> 8) & 0xFF, d1 = (p[k] >> 16) & 0xFF, d2 = (p[k] >> 24) & 0xFF;
            if ((st & 0xF0) === 0xB0 && (st & 0x0F) === 9) {
                if (d1 === 0 && d2 === 120) bank0++;
                if (d1 === 32 && d2 === 0) bank32++;
            }
            /* a program change is TWO bytes: its value is d1, and d2 is not part
             * of the message at all. Reading d2 here reported every kit as 0. */
            if ((st & 0xF0) === 0xC0 && (st & 0x0F) === 9) kits.push(d1);
        }
        M.web_out_clear();
    }
    check(kits.length === 8, 'KIT: eight detents are eight program changes', kits.join(' | '));
    check(bank0 === 8, 'KIT: every one is preceded by CC0 = 120 (the drum bank)', bank0 + '/8');
    check(bank32 === 8, 'KIT: and CC32 = 0, so no variation bank is left over', bank32 + '/8');
    /* the nine GM2 kits, 0-based on the wire */
    const want = [8, 16, 24, 25, 32, 40, 48, 56];
    check(JSON.stringify(kits) === JSON.stringify(want),
          'KIT: the detents walk the nine GM2 kits in order', kits.join(' | '));
    check(!kits.includes(1) && !kits.includes(2),
          'KIT: and never stop on an unnamed slot', kits.join(' | '));
    step(2600);
}

console.log('');
console.log(fails ? 'FAILURES: ' + fails + '/' + checks : 'PRESET PROBE OK (' + checks + ' checks)');
process.exit(fails ? 1 : 0);
