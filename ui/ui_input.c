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
    uint32_t keys, btn = plat_buttons();
    uint32_t press = btn & ~ui_prev_btn, release = ui_prev_btn & ~btn;
    uint32_t kdown, kup;
    uint32_t i;

    /* APP_TOUCH is on (app.h): the 20 analog keys' presses carry a real
     * velocity. The key mask is touch_keys(), not plat_keys(): a sensed key's
     * bit only sets when its VELOCITY EVENT arrived, so the edge below never
     * runs ahead of the reading (raw plat_keys fires the edge first and
     * touch_vel would answer 100 — the template's input() has this same
     * shape). Top-row keys have no converter: plat_keys passes through. */
    touch_poll();
    keys = touch_keys();
    kdown = keys & ~ui_prev_keys;
    kup = ui_prev_keys & ~keys;

    ui_prev_btn = btn;
    ui_prev_keys = keys;

    for (i = 0; i < NB; i++) {
        uint32_t bit = 1u << i;
        /* GLO is a modifier, not an action: it tracks its own live state on the
         * press and release edges and is never dispatched to ui_button, so
         * holding it cannot fire a page change of its own. ui_encoder and the
         * key handler read glo_held while the button is down, which is why this
         * has to be set here rather than in ui_button (which only ever sees the
         * release edge). Ported from the old tree's ui_input.c, where the same
         * two lines were the only thing making GLO+K1..K4 mean anything. */
        if (i == B_GLO) {
            if (press & bit)
                glo_held = 1;
            if (release & bit)
                glo_held = 0;
            continue;
        }
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
        if (kdown & bit) {
            /* the key's own velocity when the hardware sensed it (the 7
             * top-row keys have no converter: touch_vel reads their last
             * press too, from the LED-line probe); fall back to 100 */
            uint8_t v = touch_vel(i);
            if (!v) v = 100;
            midi_vel = v;
            panel_key_handler((uint8_t)i, 1);
        }
        if (kup & bit)
            panel_key_up((uint8_t)i);
    }
}
