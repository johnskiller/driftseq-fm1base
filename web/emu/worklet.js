// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// An fm1-base app in an AudioWorklet: app.wasm is the whole FM-1 app (web/emu/emu_web.c). Each render
// quantum asks it for 128 frames, which runs the device's clock forward; between quanta the page's
// input goes in and, about 30 times a second, the screen, the lights, any saved objects and what the
// firmware said to a screen reader (fm1-reader's SysEx frames) go out.
// An app can add its own part (APP_DIR/web/emu/app_worklet.js, loaded first): globalThis.fm1App with any of
// load(fm1), message(fm1, m) for the page's { type: "app" } messages, and process(fm1) before each quantum.

const clock = globalThis.performance ? () => globalThis.performance.now() : () => Date.now();

class Fm1 extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ex = null;
    this.input = null;
    this.sentBlits = -1;
    this.sentWrites = -1;
    this.lastFrame = 0;
    this.busy = 0;
    this.frames = 0;
    this.port.onmessage = (e) => this.onMessage(e.data);
  }

  async onMessage(m) {
    if (m.type === "load") {
      const now = () => BigInt(Math.round(currentTime * 1e9));
      const wasi = {
        clock_time_get: (id, prec, out) => {
          new DataView(this.mem.buffer).setBigUint64(out, now(), true);
          return 0;
        },
        fd_write: (fd, iov, n, out) => { new DataView(this.mem.buffer).setUint32(out, 0, true); return 0; },
        proc_exit: () => {},
      };
      const stub = new Proxy(wasi, { get: (t, k) => (k in t ? t[k] : () => 0) });
      const env = new Proxy({}, { get: () => () => 0 });
      const { instance } = await WebAssembly.instantiate(m.wasm, { wasi_snapshot_preview1: stub, env });
      this.ex = instance.exports;
      this.mem = this.ex.memory;
      if (this.ex._initialize) this.ex._initialize();
      this.app = globalThis.fm1App || {};
      if (this.app.load) this.app.load(this);
      const max = this.ex.web_store_max();
      for (const [obj, bytes] of Object.entries(m.store || {})) {
        const b = new Uint8Array(bytes).subarray(0, max);
        new Uint8Array(this.mem.buffer, this.ex.web_store(+obj), b.length).set(b);
        this.ex.web_store_set_len(+obj, b.length);
      }
      this.sentWrites = this.ex.web_store_writes();
      this.ex.web_master(m.master ?? 2800);
      this.ex.web_boot();
      this.port.postMessage({ type: "ready" });
    } else if (!this.ex) {
      return;
    } else if (m.type === "buttons") {
      this.ex.web_buttons(m.mask >>> 0);
    } else if (m.type === "keys") {
      this.ex.web_keys(m.mask >>> 0);
    } else if (m.type === "enc") {
      this.ex.web_enc(m.role, m.n | 0);
    } else if (m.type === "master") {
      this.ex.web_master(m.value | 0);
    } else if (m.type === "midi") {                     // a Web MIDI input, as the firmware's packet
      this.ex.web_midi_in(m.port >>> 0, m.pkt >>> 0);
    } else if (m.type === "sysex") {                  // a screen reader's frame: the bytes between F0 and F7
      const b = Uint8Array.from(m.bytes).subarray(0, 64);
      new Uint8Array(this.mem.buffer, this.ex.web_sysex_buf(), b.length).set(b);
      this.ex.web_sysex(b.length);
    } else if (m.type === "app" && this.app.message) {
      this.app.message(this, m);
    }
  }

  publish() {
    const ex = this.ex;
    const blits = ex.web_blits();
    const msg = { type: "frame", buttons: ex.web_lit_buttons() >>> 0, keys: ex.web_lit_keys() >>> 0 };
    if (this.frames >= 44100) {                       // the share of real time spent running the device
      msg.load = Math.round(100 * this.busy / (this.frames / 44.1));
      this.busy = this.frames = 0;
    }
    const transfer = [];
    const said = ex.web_said_len();
    if (said) {                                       // whole F0..F7 frames
      msg.said = new Uint8Array(this.mem.buffer, ex.web_said_buf(), said).slice();
      ex.web_said_clear();
      transfer.push(msg.said.buffer);
    }
    if (blits !== this.sentBlits) {
      this.sentBlits = blits;
      msg.fb = new Uint16Array(this.mem.buffer, ex.web_fb(), 240 * 240).slice();
      transfer.push(msg.fb.buffer);
    }
    // What the app sent: its USB-MIDI packets, as plain bytes for a Web MIDI
    // output. The length matters more than it looks: a receiver walks this
    // stream from status byte to status byte, so one wrong length shifts every
    // message after it. Program change and channel pressure are 2 bytes, system
    // real-time (clock, start, stop) is 1.
    const nOut = ex.web_out_n();
    if (nOut) {
      const pk = new Uint32Array(this.mem.buffer, ex.web_out_pkts(), nOut);
      const bytes = [];
      for (const p of pk) {
        const st = (p >>> 8) & 0xFF, t = st & 0xF0;
        bytes.push(st);
        if (st < 0xF0) {                            // a channel message: 2 or 3 bytes
          bytes.push((p >>> 16) & 0xFF);
          if (t !== 0xC0 && t !== 0xD0) bytes.push((p >>> 24) & 0xFF);
        } else if (st === 0xF2) {                   // song position: two data bytes
          bytes.push((p >>> 16) & 0xFF, (p >>> 24) & 0xFF);
        }                                           // everything else: its one byte and no more
      }
      ex.web_out_clear();
      msg.midi = Uint8Array.from(bytes);
      transfer.push(msg.midi.buffer);
    }
    const writes = ex.web_store_writes();
    if (writes !== this.sentWrites) {
      this.sentWrites = writes;
      msg.store = {};
      for (let o = 0; o < ex.web_nobj(); o++) {
        const n = ex.web_store_len(o);
        if (n >= 0) msg.store[o] = new Uint8Array(this.mem.buffer, ex.web_store(o), n).slice();
      }
    }
    this.port.postMessage(msg, transfer);
  }

  process(inputs, outputs) {
    if (!this.ex) return true;
    const out = outputs[0];
    const n = out[0].length;
    const t0 = clock();
    if (this.app.process) this.app.process(this);
    this.ex.web_render(n);
    this.busy += clock() - t0;
    this.frames += n;
    out[0].set(new Float32Array(this.mem.buffer, this.ex.web_out_l(), n));
    if (out[1]) out[1].set(new Float32Array(this.mem.buffer, this.ex.web_out_r(), n));
    if (currentTime - this.lastFrame >= 1 / 30) {
      this.lastFrame = currentTime;
      this.publish();
    }
    return true;
  }
}

registerProcessor("fm1", Fm1);
