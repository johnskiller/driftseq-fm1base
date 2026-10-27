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
        if ((st & 0xF0u) == 0x90u && d2)
            note_on((int32_t)d1, d2);        /* the template's beep voice: still the first sound */
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
            note_off((int32_t)d1);
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    int32_t note = voice_note;
    float target = note >= 0 ? 0.25f * voice_vel : 0.0f;
    float inc = note >= 0 ? 440.0f * fm_exp2f((float)(note - 69) / 12.0f) / 44100.0f : 0.0f;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;   /* MASTER, read once a block */
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
}

void app_panic(void)
{
    voice_note = -1;
    voice_level = 0.0f;
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
        uint32_t pkt = (uint32_t)st << 8;
        if (n >= 2 && i + 1 < eng.out_len) pkt |= (uint32_t)eng.out[i + 1] << 16;
        if (n >= 3 && i + 2 < eng.out_len) pkt |= (uint32_t)eng.out[i + 2] << 24;
        if (!plat_midi_out_room(TO_USB | TO_BLE))
            break;                                 /* full: keep it for the next frame */
        plat_midi_send(pkt, TO_USB | TO_BLE);
        eng_sent += n;
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
    else
        return 0;
    return 1;
}
#endif
