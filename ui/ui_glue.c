/* ui_glue.c — forward declarations + shared helpers (part of the UI's unity
 * section, included FIRST so every cross-reference is declared; definitions
 * follow in ui_tables.c / ui_text.c / ui.c / ui_input.c / ui_screens.c /
 * ui_edit.c).
 *
 * This file is also where the port's platform differences live, so the four
 * painter files below stay byte-for-byte the painters that were designed
 * against the old tree:
 *
 *   - the engine is `eng` here (fm1-base's app.c owns it), not `g`;
 *   - there is no panel calibration layer: fm1-base hands the app buttons and
 *     encoders already decoded (plat_buttons / plat_enc), so the matrix
 *     id -> logical role tables that ui.c carried are gone;
 *   - lights go out through plat_lights(buttons, keys, dim_buttons, dim_keys,
 *     glow), which takes exactly the four masks ui_leds() produces.
 */

/* musical grid (ui_screens.c and ui.c alike): 96 ticks per 4/4 bar @24 PPQ,
 * so one beat = 24 ticks. Both the hero meter and the white-key LED ruler
 * derive their spacing from this, which is why it is shared, not local. */
#define TPB      96

/* screens (ui_screens.c) */
static void draw_home(void);
static void draw_grid_melodic(void);
static void draw_grid_drum(void);
static void draw_arp(void);
static void draw_trkset(void);
static void ui_draw_footer(void);

/* edit/card/arp internals (ui_edit.c) */
static void ui_draw_edit_panel(void);
static void ui_draw_drum_card(void);
static void ui_arp_encoder(uint8_t enc, int8_t det);
static void ui_edit_encoder(uint8_t enc, int8_t det);
static void ui_grid_encoder(uint8_t enc, int8_t det);
static void ui_home_encoder(uint8_t enc, int8_t det);
static void trkset_encoder(uint8_t enc, int8_t det);
static uint8_t trkset_button(uint8_t btn);

/* routing + lifecycle (ui.c / ui_edit.c / ui_input.c) */
static void ui_encoder(uint8_t enc, int8_t det);
static void ui_button(uint8_t btn, uint8_t down, uint8_t released);
static void panel_key_handler(uint8_t key, uint8_t down);
static int  cell_ticks(uint8_t div);              /* ui_screens.c: ticks per step */
static void ui_leds(uint32_t *litK, uint32_t *dimK, uint32_t *litB, uint32_t *dimB);
static void ui_leds_sync(void);                   /* ui.c: the four masks -> plat_lights */
static void panel_key_up(uint8_t key);
static void ui_input_poll(void);
static void ui_draw(void);
static void ui_init(void);
static void ds_state_init(void);
static void cursor_move(int8_t n);
static uint8_t key_black_ordinal(uint8_t key);
static uint8_t key_white_index(uint8_t key);
static uint8_t key_white_id(uint8_t ord);
static uint8_t key_black_id(uint8_t ord);

/* the SAVE (jam) menu lives in ui_screens.c next to the footer that draws it;
 * ui_edit.c's routing opens/works it. It has no idle timeout any more: a menu
 * with options that can silently expire is exactly what docs/UI_GRAMMAR.md
 * forbids (John 2026-10-31: OCT+ 确认并关闭, OCT- 取消并关闭). */
/* GLOBAL's page lives here rather than in ui_global.c because ui_screens.c is
 * compiled BEFORE ui_global.c in the unity build (app.c includes ui_screens at
 * 644, ui_global at 648) and the shared footer painter needs to know which page
 * it is painting. Without that, page 2 called ui_draw_footer() - which blitted
 * "ROW EDIT" to the LCD - and then painted "DEVICE" over it and blitted again:
 * two transfers of the same band every frame, which the host cannot show (it
 * keeps the last frame) and a real LCD shows as a flicker (John 2026-10-30:
 * "最底部有些闪烁，似乎footer和scanning重叠了"). */
/* The PRESET popup: turn PRESETS and the chosen voice fills the middle of the
 * screen in FONT_M for a few seconds, then the page comes back. State lives here
 * for the same reason gl_page does - ui_draw() (ui.c, compiled at app.c:642)
 * has to test it before ui_preset.c defines it at :650. */
static uint16_t pz_ttl;                  /* frames left: 0 = no popup */
static uint8_t  pz_trk;                  /* the track it named (the popup is about one) */
static uint8_t  pz_prog;                 /* and the program, so a probe can read it */
static uint8_t gl_page;                  /* GLOBAL: 0 = settings, 1 = the radio */
static uint8_t gl_row;                   /* the row K1 is on (both pages) */
static uint8_t jam_menu;
static uint8_t jam_menu_sel;
/* The GLOBAL page (ui_global.c, included after every other page because ui.c
 * and ui_edit.c both call into it while ui_global itself needs their state:
 * glo_held, key_shift, midi_vel, txt_*, ui_draw_footer). */
static uint8_t global_button(uint8_t btn, uint8_t released);
static void global_encoder(uint8_t enc, int8_t det);
static uint8_t global_key(uint8_t key, uint8_t down);
static void global_frame(void);
static uint8_t global_save(uint8_t btn);
static void draw_global(void);

/* the PRESET popup (ui_preset.c) */
static void preset_encoder(uint8_t enc, int8_t det);
static void preset_draw(void);
static uint16_t preset_ticks(void);
static void preset_popup_text(char *b);
static void ui_select_track(uint8_t t);
static void preset_text(char *b, uint8_t prog, uint8_t is_kit);

static void jam_menu_act(uint8_t which);
static void jam_menu_close(void);
static void jam_menu_draw(void);

/* text builders (ui_text.c) */
static char *txt_next(void);
static char *txt_trk(const char *prefix, uint8_t one_based);
static char *txt_st(uint8_t one_based);
static char *txt_join(const char *a, const char *b);
static char *txt_vgp(uint8_t vel, uint8_t gate_raw, uint8_t prob);
static char *txt_knr(uint8_t k, uint8_t n, uint8_t rot);
static void strcpy_(char *dst, const char *src);

#ifdef PLAT_HOST
/* libc: the host build has no bsp/chip/libc.c, so canvas_extra.h defines this */
static void fmt_int(char *b, int32_t v);
#endif

/* gfx (fm1-base device/gfx.c; cv_disc is ours, canvas_extra.h) */
static void cv_pset(int32_t x, int32_t y, uint16_t c);
static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c);
static void cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c);
static void cv_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c);
static void cv_begin(uint32_t w, uint32_t h, uint16_t bg);
static void cv_blit(uint32_t x, uint32_t y);
static uint16_t hue12(uint8_t note);

/* note name table (emu NAMES) */
static const char *const NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *ui_note_name(uint8_t n) { return NOTE_NAMES[n % 12]; }
