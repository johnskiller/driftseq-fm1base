/* Reproduce the page's previewMidi voice table EXACTLY (index.html logic) against
 * the device's real wire packets, to prove the note-off misses the voice. */
const voices = new Map();                       // preview.voices
const feed = (bytes) => {
    const log = [];
    for (let i = 0; i < bytes.length;) {
        const st = bytes[i], t = st & 0xf0;
        const len = st >= 0xf0 ? (st === 0xf2 ? 3 : 1) : t === 0xc0 || t === 0xd0 ? 2 : 3;
        const d1 = bytes[i + 1] ?? 0, d2 = bytes[i + 2] ?? 0;
        i += len;
        if (st >= 0xf0) continue;
        if (t === 0x90 && d2 > 0) {
            const id = st + ":" + d1;
            const old = voices.get(id);
            if (old) { voices.delete(id); log.push(`restart ${id}`); }
            voices.set(id, "VOICE");
            log.push(`ON  -> voice stored under id "${id}"`);
        } else if (t === 0x80 || (t === 0x90 && d2 === 0)) {
            const id = st + ":" + d1;           // the BUG: 0x80 lookup misses the 0x90 key
            const v = voices.get(id);
            if (v) { voices.delete(id); log.push(`OFF -> stopped "${id}"`); }
            else log.push(`OFF -> lookup id "${id}" MISSED, voice "144:x" still droning!`);
        } else if (t === 0xb0 && (d1 === 123 || d1 === 120)) {
            voices.clear();
            log.push("CC123 -> all preview voices killed");
        }
    }
    return log;
};
// what the device actually put on the wire for one hero-screen key on trk3 (DIV_16, 250ms gate):
console.log(feed([0x90, 0x3d, 0x64]).join("\n"));   // note-on
console.log(feed([0x80, 0x3d, 0x00]).join("\n"));   // auto-gate note-off 250ms later
console.log(`voices still sounding: ${JSON.stringify([...voices.keys()])}`);
