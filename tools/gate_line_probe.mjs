// gate line extension test (plan D). The gate turn must happen ON the written
// step (the keybed write advanced the cursor to 1: step back first). Park with
// EDIT OFF (in EDIT, K1 is the note encoder, not the cursor).
import fs from 'node:fs';
const W = 240, H = 240;
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync('/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm'),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
const step = (ms) => { const n = Math.round(ms * 44.1); for (let i = 0; i < n; i += 1024) M.web_render(Math.min(1024, n - i)); };
const exp = (c0) => { const c = ((c0 & 0xff) << 8) | ((c0 >> 8) & 0xff);
    const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)]; };
const grab = () => new Uint16Array(M.memory.buffer, M.web_fb(), W * H).slice();
const X0 = 18, CW = 13, CHH = 22, GY = 24;
function widest(fb, s) {
    const x = X0 + (s % 16) * CW, y = GY + Math.floor(s / 16) * CHH;
    let w = 0;
    for (let r = 1; r < CHH - 1; r++) {
        let run = 0;
        for (let dx = -1; dx <= CW; dx++) {
            const [rr, gg, bb] = exp(fb[(y + r) * W + x + dx]);
            const v = (rr + gg + bb) / 3, sat = Math.max(rr, gg, bb) - Math.min(rr, gg, bb);
            if (v > 40 && sat > 25) { run++; if (run > w) w = run; } else run = 0;
        }
    }
    return w;
}
let fails = 0;
const check = (ok, label, detail = '') => { if (!ok) fails++; console.log((ok ? 'ok  :' : 'FAIL:') + ' ' + label + (detail ? '  [' + detail + ']' : '')); };

M.web_boot(); step(400);
M.web_buttons(1 << 9); step(80); M.web_buttons(0); step(150);
M.web_enc(0, 6); step(150);                    // melodic track 6
M.web_keys(1 << 1); step(70); M.web_keys(0); step(70);   // F#3 at cursor 0 (cursor -> 1)
M.web_enc(3, -1); step(60);                    // back onto the written step
M.web_buttons(1 << 4); step(60); M.web_buttons(0); step(80);   // EDIT on
M.web_enc(4, 3); step(60);                     // EN_K2: gate 1 -> 4
const fb1 = grab();
const w4on = widest(fb1, 0);                   // cursor here: yellow frame may join the run
M.web_buttons(1 << 4); step(60); M.web_buttons(0); step(80);   // EDIT off
M.web_enc(3, 12); step(80);                    // park: cursor 0 -> 12
const fb2 = grab();
const w1stub = widest(fb2, 13), w4 = widest(fb2, 0), wNext = widest(fb2, 1);
check(w4 >= 11, 'gate 4: the line runs to its own cell edge', w4 + 'px');
check(wNext >= 14, 'gate 4: the tail fills the next cell edge to edge', wNext + 'px');
check(w1stub <= 5, 'an untouched cell has no tail', w1stub + 'px');
console.log(fails ? '\nGATE LINE FAILED (' + fails + ')' : '\nGATE LINE PASSED');
process.exit(fails ? 1 : 0);
