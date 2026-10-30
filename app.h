/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The template: the smallest complete fm1-base firmware. Copy this directory to start a firmware.
 * This header is the app's half of the contract with the platform (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "DRIFTSEQ"
#define APP_SPLASH_RGB RGB(242, 120, 72)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
/* The canvas is 240 x 224: DriftSeq's painters draw one full-height pass per
 * frame (a 20px top bar, 16 twelve-px track rows, a footer) and blit it whole.
 * fm1-base defaults to 240 x 144, and cv_begin silently clamps to it rather
 * than failing, so a wrong value here does not error -- it just stops drawing
 * at row 144 and leaves the bottom of the screen black. */
#define APP_CANVAS_PX (240u * 224u)
#define APP_TOUCH                          /* velocity and pressure from the keys (lib/touch.h) */
#define APP_KEY_SENSE                      /* the keys' readings reach app_host_key: web_key's vel lands in the queue */
#define APP_BTDEV                        /* Bluetooth MIDI devices (lib/btdev.h) */
#define APP_BLE_AT_BOOT (!template_ble_off())   /* Bluetooth at power-on: as the player left it (GLOBAL page) */
int template_ble_off(void);

/* the store: the app's objects, the panel calibration (the platform's) and Bluetooth's address and pairing among
 * them, and their flash offsets */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the engine, then the saved settings (or defaults) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);                  /* the first screen, after the splash */
void app_frame(void);                    /* the UI: input and a redraw, about 60 times a second */
void app_input_only(void);               /* between frames: input only */
void app_say(const char *a, const char *b);   /* a short message on the screen and to a screen reader */

/* the host simulator's checks (host/plat_host.c "expect"): octave, wave, note (-1 none), dirty, turnsN */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);

/* the remote's `log` gets one app line (platform device main_fm1.c calls it before "end"):
 * whatever answers "why is it silent" — the render counter, the live gain, EAS's state */
#define APP_LOG_HOOK
void app_log_line(char *b);

/* SysEx the platform does not claim (plat.h APP_SYSEX): p is the bytes BETWEEN F0
 * and F7 of a complete frame, from USB or Bluetooth. The emulator needs this hook
 * wired before MMC, the pattern dump or a backup can be tested there at all — until
 * 2026-10-30 nothing in the app could receive a SysEx frame, so every one of those
 * features could only be tried on the unit.
 *
 * What it does for now is the transport floor, not the features: an identity request
 * (universal non-realtime, sub-id 06) is answered with the identity reply (sub-id 02),
 * which is the cheapest way to prove the whole path end to end — in from the port,
 * assembled by CIN, out again through plat_sysex_send. MMC is §6 of
 * docs/MIDI_IMPLEMENTATION.md and still waiting on John's decision. */
#define APP_SYSEX
int app_sysex(const uint8_t *p, uint32_t n);
