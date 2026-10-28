/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal, johnskiller */
/* DriftSeq on fm1-base.
 *
 * The sequencer is engine/driftseq.c: it owns the pattern, the clock and the
 * bytes to send, and it does not know about the platform at all. This file is
 * the other half of that contract -- it hands the engine real delta time from
 * plat_ticks() and puts engine->out on the wire. Everything the panel and the
 * screen do arrives here through plat.h (B_*, EN_*, the keys).
 *
 * MIDI out goes to USB and to both Bluetooth ports, which is what a sequencer
 * wants: no thru, nothing echoed back where it came from (plat.h says call
 * plat_midi_in_from() right after plat_midi_in for that).
 *
 * The template app's voice/wave/octave demo is GONE -- DriftSeq is a MIDI
 * sequencer, it plays whatever is downstream, so there is nothing here to
 * synthesise. */
#include "fastmath.h"
/* fm1-base is a unity build: device/unity.c includes app.c and nothing else of
 * ours, so the engine's translation unit comes in here. driftseq.c is pure C99
 * and has no platform dependencies, which is why it moves between projects
 * unchanged. */
/* The engine can carry the 16-pattern bank inline, which is 323 KB. The FM-1
 * has 96 KiB of RAM, so the firmware build leaves it out and keeps the live
 * pattern plus saved patterns in flash -- the same switch the old firmware's
 * tools/build.py passed as -DDS_PATTERN_BANK_IN_STATE=0. fm1-base has no
 * defines knob in fm1.toml, and this has to be set before the header is read,
 * so it is defined here. Nothing reads the bank but ds_init's clearing loop,
 * so the golden MIDI stream is identical either way. */
#define DS_PATTERN_BANK_IN_STATE 0
/* DRIFTSEQ_FW tells the engine it is on the bare toolchain, which has no libc
 * headers: it then declares memset itself instead of including <string.h>.
 * Same switch the old firmware's build passed as -DDRIFTSEQ_FW. It must NOT be
 * set for the --web build, which compiles these same sources with emcc and has
 * a real <string.h> -- declaring memset there conflicts with the system one
 * ("conflicting types for 'memset'"). PLAT_HOST is what fm1-base defines for
 * every hosted build (host tests and emscripten alike). */
#ifndef PLAT_HOST
#define DRIFTSEQ_FW 1
#endif
#include "engine/driftseq.h"
#include "engine/driftseq.c"
#include "engine/demo_pattern.h"
#include "engine/demo_pattern.c"
/* the sparse save format for the pattern bank (engine/pack.h). Included here
 * rather than left as a separate translation unit because fm1-base builds the
 * app as one unity unit -- a .c file that nothing includes is not compiled at
 * all, which is how a format can look finished and never be built. */
#include "engine/pack.c"

#define SET_MAGIC 0x54504C31u            /* "TPL1": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -2 .. +2 */
    uint8_t wave;                        /* 0 sine, 1 triangle */
    uint8_t ble_off;                     /* Bluetooth switched off (REC held); 0, the default: on */
    uint8_t rsv[1];
} settings_t;
static settings_t set;
static uint8_t set_dirty;                /* changed since the last save: SAVE is lit */

static const char *const WAVE_NAME[] = {"SINE", "TRIANGLE"};
/* the controls by their names in plat.h, in its order */
static const char *const BTN_NAME[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                         "PLAY", "REC", "OCT-", "OCT+"};
static const char *const ENC_NAME[NE] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};

/* ------------------------------------------------------------------ voice --- */
/* one voice, the last note wins; the main loop and MIDI in set it, the audio interrupt plays it */
static volatile int32_t voice_note = -1;     /* MIDI note, -1 = released */
static volatile float voice_vel = 1.0f;      /* the note's velocity, 0..1 */
static float voice_phase, voice_level, gain_now;   /* gain_now glides to MASTER: no step at a block's edge */
static volatile uint32_t render_blocks;            /* the audio ISR's heartbeat (the remote log's app line) */
static volatile uint32_t gm_blocks;                /* blocks EAS actually rendered */
static volatile int32_t gm_last_peak;              /* EAS's newest block's peak: is it producing sound? */
static volatile uint32_t last_master_q12;          /* the newest block's MASTER (the log's app line) */

static void note_on(int32_t note, uint32_t vel)
{
    voice_vel = (float)vel / 127.0f;
    voice_note = note;
}
static void note_off(int32_t note)
{
    if (voice_note == note)
        voice_note = -1;
}

/* ------------------------------------------------------------------ GM --- */
/* The Sonivox EAS wavetable synth (eas/eas_unity.c, its own -O2 unit): the
 * pattern's notes as real sounds, until now the app was silent but for the
 * template's beep voice above.
 *
 * The engine (ui_glue.c) routes a playing step to gm_on/off and a MIDI-in note
 * goes to both EAS and the template voice. EAS renders 128 stereo frames per
 * call at 22050 Hz -- exactly one app_render block (256 frames at 44100) --
 * and the call is linear upsampled by 2 into the mix. Its render interval is
 * the app block by construction, not by luck: AUDIO_FRAME_LENGTH (1486/256 ms
 * at 22050) matches n=256 at 44100 to within a sample.
 *
 * EAS_PCM is int16; its full scale is 32767, and the master's int32 window is
 * ~8.3 M (fm1-base's codec convention), so one EAS point maps to <<8.
 * EAS's own master volume is set to full and the FM-1's MASTER knob is applied
 * here, once per block, with the same glide the template voice uses. */
#include "eas.h"

static EAS_DATA_HANDLE eas_data;
static EAS_HANDLE eas_stream;                    /* the live MIDI stream into the synth */
static EAS_I32 eas_prev_l, eas_prev_r;           /* last upsampled pair (the interpolation's left point) */
static uint8_t eas_ok;                           /* EAS_Init succeeded (else the app runs without it) */

int eas_pool_peak(void);                         /* eas_host_fm1.c: the pool's high-water (diagnostics) */

static void gm_start(void)
{
    if (EAS_Init(&eas_data) != EAS_SUCCESS) {
        eas_data = NULL;
        return;
    }
    if (EAS_OpenMIDIStream(eas_data, &eas_stream, NULL) != EAS_SUCCESS) {
        eas_stream = NULL;
        return;
    }
    EAS_SetVolume(eas_data, NULL, 100);          /* EAS master to full; MASTER is ours */
    eas_prev_l = eas_prev_r = 0;
    eas_ok = 1;
}

static void gm_stop(void)                        /* all notes off, EAS kept (a panic, not a shutdown) */
{
    static const uint8_t cc[3] = {0xB0, 123, 0};
    if (!eas_ok)
        return;
    EAS_WriteMIDIStream(eas_data, eas_stream, (EAS_U8 *)cc, 3);
}

static void gm_on(uint8_t ch, uint8_t note, uint8_t vel)
{
    uint8_t b[3] = {(uint8_t)(0x90u | (ch & 15u)), note, vel};
    if (eas_ok)
        EAS_WriteMIDIStream(eas_data, eas_stream, b, 3);
}
static void gm_off(uint8_t ch, uint8_t note)
{
    uint8_t b[3] = {(uint8_t)(0x80u | (ch & 15u)), note, 0};
    if (eas_ok)
        EAS_WriteMIDIStream(eas_data, eas_stream, b, 3);
}
static void gm_cc(uint8_t ch, uint8_t num, uint8_t val)
{
    uint8_t b[3] = {(uint8_t)(0xB0u | (ch & 15u)), num, val};
    if (eas_ok)
        EAS_WriteMIDIStream(eas_data, eas_stream, b, 3);
}
static void gm_pc(uint8_t ch, uint8_t prog)
{
    uint8_t b[2] = {(uint8_t)(0xC0u | (ch & 15u)), prog};
    if (eas_ok)
        EAS_WriteMIDIStream(eas_data, eas_stream, b, 2);
}

/* one block: EAS renders its 128-frame frame, the result is upsampled by two
 * (linear between the frames, hold at the seam) and mixed into lr. gain is the
 * plain 0..1 MASTER (the pcm is ALREADY <<8 to the codec window; multiplying by
 * the window again is a 32 000x overflow, the rail the host sim's peak reads). */
static void gm_render(int32_t *lr, uint32_t n, float gain)
{
    EAS_PCM buf[128 * 2];
    EAS_I32 got = 0;
    uint32_t i;
    if (!eas_ok || EAS_Render(eas_data, buf, 128, &got) != EAS_SUCCESS || got <= 0)
        return;
    gm_blocks++;
    {   int32_t pk = 0;                          /* the log's "is EAS producing sound" */
        for (i = 0; i < (uint32_t)got; i++) {
            int32_t l = buf[2 * i] < 0 ? -buf[2 * i] : buf[2 * i];
            int32_t r = buf[2 * i + 1] < 0 ? -buf[2 * i + 1] : buf[2 * i + 1];
            if (l > pk) pk = l;
            if (r > pk) pk = r;
        }
        gm_last_peak = pk;
    }
    for (i = 0; i < (uint32_t)got; i++) {
        int32_t l = (int32_t)buf[2 * i] << 8, r = (int32_t)buf[2 * i + 1] << 8;
        uint32_t j = i * 2;
        float lmid = (float)(eas_prev_l + l) * 0.5f, rmid = (float)(eas_prev_r + r) * 0.5f;
        if (j < n) {                             /* odd: the midpoint between frames */
            lr[2 * j] += (int32_t)(lmid * gain);
            lr[2 * j + 1] += (int32_t)(rmid * gain);
        }
        if (j + 1 < n) {
            lr[2 * (j + 1)] += (int32_t)((float)l * gain);
            lr[2 * (j + 1) + 1] += (int32_t)((float)r * gain);
        }
        eas_prev_l = l;
        eas_prev_r = r;
    }
}

/* ---- MIDI in -> the UI ---------------------------------------------------- */
/* midi_in() runs in the audio interrupt and step entry writes the pattern, so
 * the handoff to the UI is a ring drained once a frame (ui_midi_dispatch). One
 * slot per message, packets as plat.h holds them (status<<8|d1<<16|d2<<24): the
 * same shape plat_midi_in hands over, nothing re-packed.
 *
 * What the UI gets, and why:
 *   note on/off -> ui_midi_note(): the r16j dispatch by NOTE, so a MIDI keyboard
 *       (or pads) does what the same key on the panel does -- drum grid: a black
 *       key picks and auditions the drum; melodic grid: step entry at the cursor
 *       with the MIDI velocity (the panel writes 100; pads have real dynamics);
 *       EDIT layer: add/del a voice; other pages: audition only.
 *   FA / FB / FC -> transport: the machine's own PLAY/STOP without anyone
 *       reaching for the panel.
 *   CC -> nothing: v1 has no CC handler (the emu page maps the FM-1's own CC
 *       echo onto its panel); clock (F8) and active sense (FE) are dropped, as
 *       they always were.
 *   SysEx -> ignored for now: a pattern/settings dump protocol is planned and
 *       will need a real buffer, not this ring.
 *
 * A note in range for the panel (53+shift .. 79+shift melodic; a GM_DRUMS hit
 * on a drum track) drives the same handler the panel key does; out-of-range
 * notes still audition (an external sound module's range is far wider than
 * the keybed) and go through ds_live_on/off so the sequencer tracks held notes.
 */
#define MIDI_Q 32u                               /* a burst between frames; 32 is 2 bars of 16ths at 120 */
static volatile uint32_t midi_q_w, midi_q_r;
static uint32_t midi_q[MIDI_Q];

static volatile uint32_t midi_last, midi_last_port, midi_count;   /* the last message in (shown), and how many */
static void midi_in(void)                    /* audio interrupt: the packets that came in */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24;
        int keep = st != 0xF8u && st != 0xFEu;   /* clock and active sensing would flood the screen */
        uint32_t dispatch = 0;
        if (keep)
            dispatch = pkt;                  /* note/transport: for ui_midi_dispatch */
        else if (st != 0xF8u)
            continue;                        /* FE only: not kept, not shown, not dispatched */
        if (keep) {
            midi_last = pkt;
            midi_last_port = (uint32_t)port;
            midi_count++;
        }
        if (dispatch && midi_q_w - midi_q_r < MIDI_Q)
            midi_q[midi_q_w++ % MIDI_Q] = dispatch;
        /* Notes are NOT played here: ui_midi_dispatch routes them through the engine, so the
         * sound carries the selected track's program and channel, and the engine's note-offs
         * (key-up AND the 2-cell live auto-gate) reach the same EAS channel that started the
         * note. The old code here started a template beep voice (never note_off'd by the
         * engine's offs: it sustained forever -- John 2026-10-28 "melody track 一直 sustain")
         * and a second EAS voice on hardcoded ch0 whose channel never matched the engine's
         * off. CC and PC stay: nothing else forwards a controller to EAS. */
        if ((st & 0xF0u) == 0xB0u)
            gm_cc(0, (uint8_t)d1, (uint8_t)d2);
        else if ((st & 0xF0u) == 0xC0u)
            gm_pc(0, (uint8_t)d1);
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    int32_t note = voice_note;
    float target = note >= 0 ? 0.25f * voice_vel : 0.0f;
    float inc = note >= 0 ? 440.0f * fm_exp2f((float)(note - 69) / 12.0f) / 44100.0f : 0.0f;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;   /* MASTER, read once a block */
    render_blocks++;
    last_master_q12 = master_q12;
    midi_in();
    for (i = 0; i < n; i++) {
        float s;
        voice_level += (target - voice_level) * 0.002f;          /* ~10 ms attack and release */
        gain_now += (gain - gain_now) * 0.004f;                  /* ~5 ms: turning MASTER does not click */
        voice_phase += inc;
        if (voice_phase >= 1.0f)
            voice_phase -= 1.0f;
        s = set.wave ? 4.0f * fm_fabsf(voice_phase - 0.5f) - 1.0f : fm_sinf(voice_phase * 6.2831853f);
        s *= voice_level * gain_now;
        lr[2 * i] = (int32_t)s;
        lr[2 * i + 1] = (int32_t)s;
    }
    gm_render(lr, n, gain_now / 8300000.0f);  /* the GM synth mixed after the beep voice;
                                               * gain_now is the codec-window gain, the GM wants the plain 0..1 */
}

void app_panic(void)
{
    voice_note = -1;
    voice_level = 0.0f;
    gm_stop();
}

/* --------------------------------------------------------------- settings --- */
static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.octave = 0;
    set.wave = 0;
    set.ble_off = 0;
}

/* ---------------------------------------------------------------- engine --- */
/* The sequencer. Seed from the clock so two units do not start identical, and
 * never reseed: the pattern is the user's, not the boot's.
 *
 * PLAT_POOL puts it in the .pool section (app.ld: 0x01C20000..0x01C74000, 336
 * KiB) instead of .bss (the 96 KiB RAM region). It is 26,164 B -- the single
 * largest object in the app -- and every remaining byte of .bss is contended by
 * the Bluetooth stack. Measured: RAM 94,436 -> 68,276 B, free 3,868 -> 30,028.
 *
 * The pool is plain on-chip SRAM on the same bus as .bss; cv_px (107 KB, written
 * per pixel every frame) already lives there, so per-byte CPU access is proven.
 * Nothing here is touched by DMA. */
static ds_state eng PLAT_POOL;
static uint32_t eng_last_ms;
static uint32_t eng_sent;                          /* bytes put on the wire, for the host test */

/* DriftSeq's clock is the real delta between frames, not a frame count: the
 * FM-1's frame period wobbles, and a sequencer that counts frames drifts
 * against everything it is playing with.
 *
 * plat_ms() and not plat_ticks24(): on the device both are the crystal, but on
 * the host emulator plat_ticks24() reads CLOCK_MONOTONIC -- real wall time --
 * while frames advance the emulated now_ms. Driving the engine from wall time
 * there means the sequencer sees a fraction of the time the frame loop thinks
 * has passed, and it stalls after a bar. plat_ms() is the emulator's clock on
 * both, and 1 ms is well inside a 16th at any tempo this runs at. */
static void engine_drain(void)
{
    uint32_t i;
    for (i = 0; i < eng.out_len; ) {
        /* one packet per message: status|d1|d2 is 3 bytes for the messages
         * this engine sends (note on/off, CC), 2 for a program change */
        uint8_t st = eng.out[i];
        uint32_t n = ((st & 0xF0u) == 0xC0u || (st & 0xF0u) == 0xD0u) ? 2u : 3u;
        /* byte0 = the USB-MIDI event packet's Cable|CIN nibble. The platform sends the
         * 4 bytes as-is on EP1, and CIN 0 (misc) is a packet macOS drops — the BLE side
         * never sees this byte (it rebuilds from the status), which is why the wire
         * played on 88emu while USB-MIDI stayed silent. CIN = the status's high nibble
         * is exactly the spec's mapping for the channel voice messages this engine
         * sends (8 note-off, 9 note-on, B CC, C program change). */
        uint32_t pkt = (uint32_t)(st >> 4) | (uint32_t)st << 8;
        if (n >= 2 && i + 1 < eng.out_len) pkt |= (uint32_t)eng.out[i + 1] << 16;
        if (n >= 3 && i + 2 < eng.out_len) pkt |= (uint32_t)eng.out[i + 2] << 24;
        /* room 0 has TWO different causes: the wire ring is full (hold the event
         * for the next frame), or every link in `to` is down (the plat returns 0
         * then, not a big room). The second must not starve the local GM: with
         * USB and BLE both down the old check broke here and the synth went
         * silent though the engine kept stepping (John 2026-10-28: sound only
         * while a link was up). So: the wire send waits for room, the GM feed
         * never does; the event is consumed either way — the platform's send
         * drops on a full ring itself, and a disconnected wire has no listener
         * to lose. */
        if (plat_midi_out_room(TO_USB | TO_BLE))
            plat_midi_send(pkt, TO_USB | TO_BLE);
        eng_sent += n;
        /* and into the GM synth: the wire hears it, the local sound does too.
         * d1/d2 live just past the status byte. */
        if ((st & 0xF0u) == 0x90u)
            gm_on(st & 15u, (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24));
        else if ((st & 0xF0u) == 0x80u)
            gm_off(st & 15u, (uint8_t)(pkt >> 16));
        else if ((st & 0xF0u) == 0xB0u)
            gm_cc(st & 15u, (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24));
        else if ((st & 0xF0u) == 0xC0u)
            gm_pc(st & 15u, (uint8_t)(pkt >> 16));
        i += n;
    }
    if (i) {                                       /* drop what went out */
        uint32_t k;
        for (k = 0; k + i < eng.out_len; k++)
            eng.out[k] = eng.out[k + i];
        eng.out_len -= i;
    }
}
/* drain before ds_tick, not after: ds_tick's own bookkeeping resets out_len at
 * its start (a quiet stopped tick, and the playing branch both), which wiped
 * whatever the UI queued since the last frame — a key pressed on the panel or
 * a note in from MIDI auditions through ds_live_on BETWEEN frames, and the
 * audition never reached the wire (masked for a year on the emu by its local
 * square-wave preview; on hardware the key simply made no sound). Draining
 * first, then ticking, then draining again is belt and braces; the first drain
 * is the one that matters. */
static void engine_tick(void)
{
    uint32_t now = plat_ms(), el = now - eng_last_ms;
    eng_last_ms = now;
    if (eng.out_len)
        engine_drain();
    ds_tick(&eng, (double)el / 1000.0);
    if (eng.out_len)
        engine_drain();
}

static void engine_init(void)
{
    ds_init(&eng, (uint64_t)plat_ticks24() * 2654435761u);
    /* A sequencer that comes up empty is indistinguishable from a broken one on
     * a screen that does not draw notes yet, and the first thing anyone does is
     * press PLAY. Start on the demo song (engine/demo_pattern.c, the same one
     * the golden test locks). */
    ds_load_demo(&eng);
    eng_last_ms = plat_ms();
    eng.out_len = 0;
}

void app_init(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.octave < -2 || set.octave > 2 || set.wave > 1u || set.ble_off > 1u)
        settings_defaults();
    engine_init();
    gm_start();                              /* the GM synth; on failure eas_ok stays 0 and the app runs beep-only */
}

/* --------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];                     /* a short message over the header */
    uint32_t msg_until;
    uint32_t keys, btn;                  /* held at the last input */
    uint8_t vel[NKEYS], prs[NKEYS];      /* each key's last velocity, and its pressure while held */
    int32_t last_key;                    /* the key pressed last: its numbers in the header */
    uint8_t looking;                     /* Bluetooth MIDI devices: REC pressed, the scan's list up (lib/btdev) */
    uint32_t rec_t;                      /* REC went down (0: not held, or its hold already acted) */
    int32_t turns[NE];                   /* detents since boot, per encoder */
    uint32_t turned_at[NE];              /* when each was last turned: it shows lit for a moment */
    uint8_t redraw;
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)     /* signed, no padding */
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {                          /* and to a screen reader (announce.c, fm1-reader) */
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void octave_text(char *t)
{
    t[0] = set.octave < 0 ? '-' : '+';
    t[1] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave));
    t[2] = 0;
}

/* a screen reader that has just started listening (or asks) hears the whole screen */
static void speak(void)
{
    char t[96], o[4];
    if (ann_on() && ann_where()) {
        octave_text(o);
        t[0] = 0;
        text_cat(t, APP_NAME ". OCTAVE ");
        text_cat(t, o);
        text_cat(t, ". WAVE ");
        text_cat(t, WAVE_NAME[set.wave]);
        if (midi_count) {                    /* the last MIDI message in, as the screen shows it */
            static const char *const PORT[NPORTS] = {"USB", "BLE", "TRS"};
            static const char HEX[] = "0123456789ABCDEF";
            uint32_t m = midi_last, b;
            char h[10];
            for (b = 0; b < 3u; b++) {
                uint32_t v = (m >> (8u + 8u * b)) & 0xFFu;
                h[3 * b] = HEX[v >> 4];
                h[3 * b + 1] = HEX[v & 15u];
                h[3 * b + 2] = b < 2u ? ' ' : 0;
            }
            text_cat(t, ". MIDI IN ");
            text_cat(t, midi_last_port < NPORTS ? PORT[midi_last_port] : "");
            text_cat(t, " ");
            text_cat(t, h);
        }
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

static void note_name(int32_t note, char *b)
{
    static const char *const N[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char o[12];
    b[0] = 0;
    text_cat(b, N[note % 12]);
    int_text(note / 12 - 1, o);
    text_cat(b, o);
}

static void octave_step(int32_t d)
{
    int32_t o = clamp(set.octave + d, -2, 2);
    char t[4];
    if (o == set.octave)
        return;
    voice_note = -1;                         /* a key held across the change would not find its note off */
    set.octave = (int8_t)o;
    set_dirty = 1;
    octave_text(t);
    app_say("OCTAVE", t);
}

int template_ble_off(void) { return set.ble_off; }

/* REC held a second: Bluetooth off or on (kept with the settings, so it stays so at the next power-on) */
static void ble_toggle(void)
{
    set.ble_off = (uint8_t)!set.ble_off;
    set_dirty = 1;
    plat_ble_enable(!set.ble_off);
    app_say("BLUETOOTH", set.ble_off ? "OFF" : "ON");
}

/* REC: a Bluetooth MIDI device (FM1B_BLE_CENTRAL, lib/btdev): look for devices, then connect to the one ALGORITHM
 * picks, then let it go */
static void ble_device_say(void)
{
    char name[21];
    if (btdev_found(btdev_pick(), name, 0))
        app_say("DEVICE", name);
    else
        app_say("BLE", "SCANNING");
}
static void ble_device_button(void)
{
    int ev;
    if (!btdev_busy() && !ui.looking) {
        ui.looking = 1;
        app_say("BLE", "SCANNING");
        return;
    }
    ev = btdev_save();
    ui.looking = 0;
    app_say(ev == BTDEV_CONNECTING ? btdev_word(ev) : "BLE", ev == BTDEV_CONNECTING ? btdev_name() : btdev_word(ev));
}

/* ---- the UI ----
 *
 * Everything below is DriftSeq's own screen code, migrated from the old tree
 * (firmware/src/ui*.c) and included here in the order its forward declarations
 * require. It is a unity section for the same reason the old one was: the
 * painters are static, they share a good deal of state, and splitting them into
 * translation units would mean inventing headers for it.
 *
 * ui_glue.c also carries the port's platform differences, so the painter files
 * are the painters that were designed against the old tree.
 */
#include "canvas_extra.h"                /* cv_disc, the one primitive fm1-base's gfx.c lacks */
#ifdef APP_TOUCH
#include "touch.h"                       /* the keys' velocity readings (ui_input.c polls them) */
#endif
#include "ui/ui_glue.c"
#include "ui/ui_tables.c"
#include "ui/ui_text.c"
#include "ui/ui.c"
#include "ui/ui_input.c"
#include "ui/ui_screens.c"
#include "ui/ui_edit.c"

void app_input_only(void)
{
    ui_input_poll();
    ui_midi_dispatch();                      /* MIDI in as input, on the same schedule */
}

void app_ui_init(void)
{
    touch_on(1);                             /* velocity and pressure: the keys' readings on */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui_init();
}

/* The old tree redrew every frame, unconditionally, and that is what the
 * sequencer needs: the playhead moves, the hero meter fills, gates open. A
 * dirty flag would have to be set from the engine's tick, which is inside the
 * thing being drawn. 60 cv_blit passes a second is the cost, and the platform
 * was designed for it. */
void app_frame(void)
{
    ui_input_poll();
    ui_midi_dispatch();                      /* MIDI in as input, on the same schedule */
    engine_tick();                           /* the sequencer runs on real delta time */
    if (FM1B_BLE_CENTRAL) {                  /* looking: a device found is named at once (REC connects to it) */
        static int found;
        int n = ui.looking ? btdev_count() : 0, ev;
        btdev_page(ui.looking && !set.ble_off);
        if (n > found && btdev_pick() < n)
            ble_device_say();
        found = n;
        while ((ev = btdev_event()) != BTDEV_NONE)   /* a device connected, given up or lost */
            if (ev == BTDEV_CONNECTED || ev == BTDEV_NO_ANSWER || ev == BTDEV_LOST)
                app_say("DEVICE", btdev_word(ev));
    }
    ui_draw();
    ui_leds_sync();
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "playing"))
        *got = eng.playing;
    else if (!strcmp(what, "mode"))
        *got = ui_mode;
    else if (!strcmp(what, "cursor"))
        *got = cursor;
    else if (!strcmp(what, "track"))
        *got = sel_track;
    else if (!strcmp(what, "gm"))            /* GM diagnostics: eas_ok | voices<<8 (EAS internals below) */
        *got = eas_ok;
    else if (!strcmp(what, "gm_pool"))
        *got = eas_pool_peak();
    else
        return 0;
    return 1;
}
#endif

/* the remote log's app line: is the render alive, is EAS alive, is it loud?
 * (the platform's b is 48 chars: keep under it) */
void app_log_line(char *b)
{
    static const char H[] = "0123456789ABCDEF";
    char *q = b;
    const char *l = "app r";
    while (*l) *q++ = *l++;
    q[0] = H[(render_blocks >> 20) & 15]; q[1] = H[(render_blocks >> 16) & 15];
    q[2] = H[(render_blocks >> 12) & 15]; q[3] = H[(render_blocks >> 8) & 15];
    q[4] = H[(render_blocks >> 4) & 15]; q[5] = H[render_blocks & 15];
    q += 6;
    for (l = " g"; *l;) *q++ = *l++;
    q[0] = H[(gm_blocks >> 20) & 15]; q[1] = H[(gm_blocks >> 16) & 15];
    q[2] = H[(gm_blocks >> 12) & 15]; q[3] = H[(gm_blocks >> 8) & 15];
    q[4] = H[(gm_blocks >> 4) & 15]; q[5] = H[gm_blocks & 15];
    q += 6;
    for (l = " m"; *l;) *q++ = *l++;
    q[0] = H[(last_master_q12 >> 8) & 15]; q[1] = H[(last_master_q12 >> 4) & 15];
    q[2] = H[last_master_q12 & 15];
    q += 3;
    for (l = " e"; *l;) *q++ = *l++;
    *q++ = (char)('0' + (eas_ok != 0));
    for (l = " p"; *l;) *q++ = *l++;
    q[0] = H[(gm_last_peak >> 8) & 15]; q[1] = H[(gm_last_peak >> 4) & 15];
    q[2] = H[gm_last_peak & 15];
    q += 3;
    *q = 0;
}
