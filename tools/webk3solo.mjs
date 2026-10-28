/* K3 solo slider on web wasm: solo trk5 (empty) during PLAY → everything silent;
 * back to ON → mix returns. Also pins the knob readout path (no crash on
 * k3_state_name). */
import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(
    fs.readFileSync('/home/john/.hermes/workspace/driftseq-fm1base/build/emu/app.wasm'),
    { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }), env: new Proxy({}, { get: () => () => 0 }) });
const M = instance.exports;
M.web_boot();
M.web_master(2800);
const pk = (ms) => {
    const n = Math.round(ms * 44.1);
    let p = 0;
    for (let i = 0; i < n; i += 128) {
        M.web_render(Math.min(128, n - i));
        const l = new Float32Array(M.memory.buffer, M.web_out_l() | 0, 128);
        for (let k = 0; k < 128; k++) p = Math.max(p, Math.abs(l[k]));
    }
    return (20 * Math.log10(p + 1e-9)).toFixed(1);
};
pk(200);
M.web_enc(0, 5);                       /* SELECT → trk5 */
M.web_buttons(1 << 10); pk(30); M.web_buttons(0);   /* PLAY tap, as webdemo does */
pk(300);
M.web_enc(5, 5);                       /* K3 +5 → SOLO the empty track */
M.web_enc(5, -0); pk(1000); const soloDb = pk(800);
M.web_enc(5, -5);                      /* K3 back → ON */
const onDb = pk(500);
M.web_buttons(0);
console.log(`solo(empty trk) = ${soloDb} dBFS (want < -50) | back ON = ${onDb} dBFS (want > -30)`);
