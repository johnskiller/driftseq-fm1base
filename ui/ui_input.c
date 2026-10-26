/* ui_input.c — panel events into the UI (part of the UI's unity section).
 *
 * The old tree needed this file because its HAL reported a raw 14-button matrix
 * and 7 encoders by matrix position, so the UI carried calibration tables
 * (btn_role/enc_role) to turn positions into roles. fm1-base has no such layer:
 * plat_buttons() and plat_keys() already return the logical mask, and plat_enc()
 * already returns detents for a role. So there is nothing to translate, only
 * edges to derive.
 *
 * Logical ids: buttons = plat.h B_*, encoders = EN_*, keys = 0..NKEYS-1, which
 * are notes 53..79 (F3..G5).
 */

static uint32_t ui_prev_btn;                     /* masks as of the last poll */
static uint32_t ui_prev_keys;

/* Called once per frame (app_frame) and between frames (app_input_only), so a
 * press shorter than a frame is still seen as a press: the edge is whatever
 * differs from the previous call, not from the previous frame. */
static void ui_input_poll(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys();
    uint32_t press = btn & ~ui_prev_btn, release = ui_prev_btn & ~btn;
    uint32_t kdown = keys & ~ui_prev_keys, kup = ui_prev_keys & ~keys;
    uint32_t i;

    ui_prev_btn = btn;
    ui_prev_keys = keys;

    for (i = 0; i < NB; i++) {
        uint32_t bit = 1u << i;
        if (press & bit)
            ui_button((uint8_t)i, 1, 0);
        if (release & bit)
            ui_button((uint8_t)i, 0, 1);
    }

    /* Encoders report accumulated detents since the last read, so each detent
     * becomes one step: a fast turn must not be collapsed into one. */
    for (i = 0; i < NE; i++) {
        int32_t d = plat_enc((int32_t)i);
        while (d > 0) { ui_encoder((uint8_t)i, 1); d--; }
        while (d < 0) { ui_encoder((uint8_t)i, -1); d++; }
    }

    for (i = 0; i < NKEYS; i++) {
        uint32_t bit = 1u << i;
        if (kdown & bit)
            panel_key_handler((uint8_t)i, 1);
        if (kup & bit)
            panel_key_up((uint8_t)i);
    }
}
