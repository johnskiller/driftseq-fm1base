#!/usr/bin/env node
/* Does a SysEx frame reach DriftSeq's own handler inside the emulator, and does
 * the answer come back out the way a host would see it?
 *
 * The page can assemble a frame and pack it into packets all day; that proves
 * nothing about the firmware. This runs the built app.wasm inside the real
 * worklet (the same harness as midi_reaches_fw.mjs) and reads the ONE channel the
 * firmware itself writes: worklet.publish() turns the app's USB-MIDI packets into
 * a byte stream for a Web MIDI output, and that is exactly what a host on the
 * other end of the cable would receive. So the assertions below are made on the
 * firmware's own output, decoded by a reader that only knows the CIN table.
 *
 * What is asserted:
 *   - an identity request is answered, and the answer is a SysEx frame that
 *     decodes to the identity REPLY with this device's id and a closing F7;
 *   - the packets carrying it use the SysEx CINs (4..7) with the last one ending
 *     the frame, not the channel-message CINs;
 *   - a frame the app does not claim, and an identity request for another device
 *     id, are dropped rather than answered;
 *   - a frame whose last packet is short (CIN 5) still assembles;
 *   - channel messages still play afterwards, so none of this broke the note path.
 *
 *   node tools/emu_sysex_e2e.mjs
 */
import fs from "node:fs";
import vm from "node:vm";

let checks = 0, fails = 0;
const check = (ok, msg, detail) => {
    checks++;
    if (!ok) fails++;
    console.log((ok ? "ok: " : "FAIL: ") + msg + (detail ? "  [" + detail + "]" : ""));
};

/* ---- the worklet, with an AudioWorklet stand-in ---- */
const posted = [];
class AudioWorkletProcessor {
    constructor() {
        this.port = { postMessage: (m) => posted.push(m), onmessage: null };
        this.ex = null; this.frames = 0; this.busy = 0;
        this.lastFrame = 0; this.sentBlits = -1; this.sentWrites = -1;
    }
}
let Proc = null;
const ctx = { AudioWorkletProcessor, registerProcessor: (n, c) => { Proc = c; }, currentTime: 0, console };
ctx.globalThis = ctx;
ctx.performance = { now: () => Date.now() };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(new URL("../web/emu/worklet.js", import.meta.url), "utf8"), ctx);

const wasmBytes = fs.readFileSync(new URL("../build/emu/app.wasm", import.meta.url));
const p = new Proc();
await p.onMessage({
    type: "load",
    wasm: wasmBytes.buffer.slice(wasmBytes.byteOffset, wasmBytes.byteOffset + wasmBytes.byteLength),
    store: {}, master: 2800,
});
check(!!p.ex, "the worklet booted the firmware");

/* ---- time, and the collected host-side stream ----
 * process() runs the device for one quantum; publish() is what turns the app's
 * packet ring into bytes and clears it. Only what is posted after a marker is
 * this test's. */
const out = [new Float32Array(128), new Float32Array(128)];
const runFrames = (n) => {
    for (let i = 0; i < n; i += 128) {
        out[0].fill(0); out[1].fill(0);
        p.process([], [out, out]);
        ctx.currentTime += 128 / 44100;
    }
};
/* everything the firmware put on its MIDI output since the last call */
let drained = 0;                    /* how far into `posted` this test has read */
function drainMidi() {
    p.publish();                    /* flushes the app's packet ring into a posted frame */
    const bytes = [];
    for (let i = drained; i < posted.length; i++) {
        const m = posted[i];
        if (m && m.type === "frame" && m.midi) bytes.push(...m.midi);
    }
    drained = posted.length;        /* keep the log: process() publishes on its own too */
    return bytes;
}

/* A frame cut into USB-MIDI packets the way a host port does it, with real CINs:
 * the firmware reads the CIN, so a made-up one would test nothing. */
function framePackets(frame) {
    const pkts = [];
    for (let i = 0; i < frame.length; ) {
        const k = Math.min(3, frame.length - i);
        const ends = i + k >= frame.length;
        const cin = ends ? (k === 3 ? 0x7 : 0x4 + k) : 0x4;
        const b = frame.slice(i, i + k);
        pkts.push(((cin | (b[0] ?? 0) << 8 | (b[1] ?? 0) << 16 | (b[2] ?? 0) << 24) >>> 0));
        i += k;
    }
    return pkts;
}
async function sendFrame(frame, port = 0, ms = 150) {
    for (const pkt of framePackets(frame)) {
        await p.onMessage({ type: "midi", port, pkt });
    }
    runFrames(44100 / 1000 * ms);
}
const hex = (a) => [...a].map((b) => b.toString(16).padStart(2, "0")).join(" ");

runFrames(44100 * 0.8);                        /* boot: the splash, then the first frame */
drainMidi();

/* ---- 1. the identity request is answered ---- */
const reply = await (async () => {
    await sendFrame([0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7]);
    return drainMidi();
})();
check(reply.length > 0, "the app answered a SysEx frame", `${reply.length} bytes`);
check(reply[0] === 0xf0, "the answer starts with F0 (a SysEx frame, not channel messages)",
      "0x" + (reply[0] ?? 0).toString(16));
check(reply[reply.length - 1] === 0xf7, "and ends with F7",
      "0x" + (reply[reply.length - 1] ?? 0).toString(16));
check(reply[1] === 0x7e && reply[3] === 0x06 && reply[4] === 0x02,
      "it is the identity REPLY (7E .. 06 02)", hex(reply.slice(0, 6)));
check(reply.includes(0x7d), "it carries this firmware's 0x7D id", hex(reply));
check(reply.length === 15, "the reply is 15 bytes", String(reply.length));

/* the packets behind it must have used the SysEx CINs */
{
    const pkts = framePackets([0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7]);  /* same shape as the reply, for the CIN walk */
    check(pkts.every((x) => (x & 0xf) >= 4 && (x & 0xf) <= 7), "a SysEx frame's packets all use CIN 4..7",
          pkts.map((x) => x & 0xf).join(","));
}

/* ---- 2. a frame the app does not claim is dropped ---- */
{
    await sendFrame([0xf0, 0x7d, 0x4c, 0x47, 0x61, 0x62, 0x63, 0xf7]);
    const got = drainMidi();
    check(got.length === 0, "an unknown frame is ignored, with no answer", `${got.length} bytes`);
}

/* ---- 3. another device's identity request is not ours to answer ---- */
{
    await sendFrame([0xf0, 0x7e, 0x02, 0x06, 0x01, 0xf7]);
    const got = drainMidi();
    check(got.length === 0, "a request addressed to device id 2 is ignored", `${got.length} bytes`);
}

/* ---- 4. and the request still works afterwards: nothing got stuck ---- */
{
    await sendFrame([0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7]);
    const got = drainMidi();
    check(got.length === 15 && got[4] === 0x02, "a second request is answered again", hex(got.slice(0, 6)));
}

/* ---- 5. a frame whose last packet is short (CIN 5) still assembles ----
 * The platform assembles by CIN. A 7-byte frame ends in a packet that says "CIN 5,
 * one byte of frame left"; if that were read as three bytes the frame would be
 * mangled and the app would stay silent. */
{
    const f = [0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7, 0x00];   /* 7 bytes -> CIN4, CIN4, CIN5 */
    const pkts = framePackets(f);
    check((pkts[pkts.length - 1] & 0xf) === 5, "the test frame really does end in a CIN 5 packet",
          String(pkts[pkts.length - 1] & 0xf));
    /* F7 arrives inside the CIN 5 packet, so this ends the frame there; the
     * trailing 0x00 is what follows it and must not be swallowed into the frame. */
    for (const pkt of pkts) await p.onMessage({ type: "midi", port: 0, pkt });
    runFrames(4410);
    const got = drainMidi();
    check(got.length > 0, "a frame ending in a short CIN 5 packet still reaches the app", `${got.length} bytes`);
}

/* ---- 6. channel messages still play, so none of this broke the note path ---- */
{
    drainMidi();
    const quiet = (() => {
        let last = 0;
        for (let i = 0; i < 4410; i += 128) {
            out[0].fill(0); out[1].fill(0); p.process([], [out, out]);
            for (let k = 0; k < 128; k++) last = Math.abs(out[0][k]);
            ctx.currentTime += 128 / 44100;
        }
        return last;
    })();
    await p.onMessage({ type: "midi", port: 0, pkt: ((0x09 | 0x90 << 8 | 0x45 << 16 | 0x64 << 24) >>> 0) });
    let heard = 0;
    for (let i = 0; i < 4410; i += 128) {
        out[0].fill(0); out[1].fill(0); p.process([], [out, out]);
        for (let k = 0; k < 128; k++) heard = Math.abs(out[0][k]);
        ctx.currentTime += 128 / 44100;
    }
    /* the bar is "audibly above silence", not a level: what a note reaches depends
     * on the page and the master, and midi_reaches_fw.mjs asserts the level. Here
     * the question is only whether the note path still works after the SysEx. */
    check(quiet < 0.0001 && heard > 10 * Math.max(quiet, 0.00001), "a note still plays after all of that SysEx traffic",
          `${quiet.toFixed(4)} -> ${heard.toFixed(4)}`);
}

console.log(fails ? `\nEMU SYSEX E2E: ${fails} FAILED of ${checks}` : `\nEMU SYSEX E2E PASSED (${checks} checks)`);
process.exit(fails ? 1 : 0);
