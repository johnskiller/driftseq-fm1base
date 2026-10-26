/* SPDX-License-Identifier: MPL-2.0 */
/* canvas_extra.h — the two drawing helpers DriftSeq needs that fm1-base's
 * device/gfx.c does not provide.
 *
 * fm1-base supplies cv_begin, cv_band, cv_blit, cv_blit_from, cv_pset,
 * cv_rect, cv_line, cv_text and text_w — all MPL-2.0, all on the same
 * .pool strip with the same byte-swapped pixels. That covers 6 of the 7
 * drawing helpers DriftSeq's painters call (cv_rect 75 times, cv_text 48,
 * cv_begin 9, cv_blit 9, cv_line 1, cv_pset 1). Only cv_disc is missing, and
 * the UI needs it 3 times.
 *
 * Why this file exists rather than including DriftSeq's old gfx_body.h:
 * that file is SPDX GPL-3.0-only, copyright Leo Kuroshita / Hügelton
 * Instruments. Copying cv_disc out of it would put GPL code back in an image
 * whose whole purpose is to be distributable with JieLi's binary Bluetooth
 * libraries. The algorithm is a filled circle rasterised by horizontal spans,
 * which is arithmetic, not protected expression — so it is written fresh here.
 *
 * Nothing else from gfx_body.h is needed: blend, hue12, cv_frame_begin,
 * strcpy_ and fmt_int all live in DriftSeq's own ui_*.c / libc.c, not in the
 * vendored Felucca file. fmt_int is re-implemented below for the same reason
 * (it came from Felucca's libc.c).
 *
 * Include AFTER fm1-base's device/gfx.c, so cv_rect is visible. */
#pragma once

/* Integer square root, floor. Newton's method from a bit-length seed; no
 * libm (the pi32v2 build is freestanding and has no sqrt). */
static uint32_t isqrt_u(uint32_t v)
{
    uint32_t x, bit;
    if (v < 2u)
        return v;
    /* seed: one bit above half the significant bits */
    bit = 1u << 30;
    while (bit > v)
        bit >>= 2;
    x = bit;
    for (;;) {
        uint32_t nx = (x + v / x) >> 1;
        if (nx >= x)
            break;
        x = nx;
    }
    return x;
}

/* A filled disc: horizontal spans, one per row. r == 0 draws one pixel. */
static void cv_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t y;
    if (r < 0)
        return;
    for (y = -r; y <= r; y++) {
        int32_t hw = (int32_t)isqrt_u((uint32_t)(r * r - y * y));
        cv_rect(cx - hw, cy + y, hw * 2 + 1, 1, c);
    }
}

/* A filled rounded rectangle with a radius of `r` px. r == 0 is cv_rect.
 *
 * The header's track-number pill is the only caller. The first version cut the
 * corners by plotting one pixel per row along a diagonal, which left the corner
 * as a detached dot with background showing between it and the body -- John saw
 * it on hardware as "切角内加了黑点". This walks the rows instead: for each of
 * the r rows at the top and bottom it computes the overhang from the circle
 * equation and fills the row that much narrower, so the arc is solid and joins
 * the body with no gap.
 *
 * Integer only, and the mid-row is filled wide so the widest row is exact. */
static void cv_round_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    int32_t i;
    if (w <= 0 || h <= 0)
        return;
    if (r <= 0 || r * 2 >= h || r * 2 >= w) {
        cv_rect(x, y, w, h, c);
        return;
    }
    for (i = 0; i < r; i++) {
        /* distance from the row to the corner centre, in half-pixels so the
         * radius test stays integer: overhang = r - sqrt(r^2 - (r-i-1/2)^2) */
        int32_t dy2 = (2 * (r - i) - 1);            /* 2*dy, dy = r-i-0.5 */
        int32_t d2 = (4 * r * r - dy2 * dy2);       /* 4*(r^2 - dy^2) */
        /* (isqrt+1)/2 rounds to nearest instead of truncating. Truncating gave
         * insets of 3,1,1,1 for r=4 -- three rows with the same overhang, which
         * is a chamfer, not an arc. */
        int32_t inset = r - (int32_t)(isqrt_u((uint32_t)(d2 > 0 ? d2 : 0)) + 1u) / 2;
        if (inset < 0)
            inset = 0;
        cv_rect(x + inset, y + i, w - inset * 2, 1, c);
        cv_rect(x + inset, y + h - 1 - i, w - inset * 2, 1, c);
    }
    cv_rect(x, y + r, w, h - r * 2, c);
}

/* Width of `s` in font `f`, in pixels, using the same advance table cv_text
 * walks. Needed because cv_text returns the end x but callers that lay out
 * right-aligned or centred text have to know the width before drawing.
 * Lower case folds to upper case exactly as glyph() in gfx.c does, so the two
 * cannot disagree about which slot to read. */
static int32_t cv_text_w(const felucca_font_t *f, const char *s)
{
    int32_t w = 0;
    for (; *s; s++) {
        uint32_t ch = (uint8_t)*s;
        if (ch >= 'a' && ch <= 'z' && f->last < 'a')
            ch -= 32u;
        if (ch < f->first || ch > f->last || (ch >= 127u && ch < 160u))
            ch = '?';
        w += f->adv[ch - f->first];
    }
    return w;
}

/* Signed decimal into b, no padding.
 *
 * The device build already has this: bsp/chip/libc.c defines fmt_int (void
 * return) and it is visible in the unity build, so defining it again there is a
 * conflicting-types error. The --web build has no libc.c at all, so it needs
 * one. PLAT_HOST is what fm1-base defines for every hosted build. */
#ifndef PLAT_HOST
/* (the platform's own) */
#else
static void fmt_int(char *b, int32_t v)
{
    char t[12];
    int32_t n = 0, i;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do {
        t[n++] = (char)('0' + (int32_t)(u % 10u));
        u /= 10u;
    } while (u);
    if (v < 0)
        t[n++] = '-';
    for (i = 0; i < n; i++)
        b[i] = t[n - 1 - i];
    b[n] = 0;
}
#endif
