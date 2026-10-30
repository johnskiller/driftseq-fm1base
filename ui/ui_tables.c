/* ui_tables.c — drum lane palette + GM tables + key identity (part of ui.c
 * unity section). Lives before ui_glue consumers; shared by all ui files. */
#define RGB565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* Drum lanes: NINE instruments (03 §3, John 2026-10-19: "9 差不多够了").
 * The keybed has 11 black keys, so the last 2 stay reserved for a v1.1
 * percussion expansion — pressing them selects nothing rather than erroring
 * (§3: "选不满不报错").
 *
 * Lane ORDER is the 808 panel mental model (kick first, then snare, hats,
 * cymbals, toms), and the lane index — not the MIDI note — is the colour key:
 * GM slot shuffling moves the colour with the instrument. */
#define NDRUM 9
static const uint8_t GM_DRUMS[NDRUM] = {36, 38, 42, 46, 49, 45, 41, 43, 51};
static const char GM_SHORT[NDRUM][5] = {"KCK", "SNR", "CHH", "OHH", "CUP",
                                        "TOM", "RID", "PERC", "SHK"};
/* 11 colours: lanes use the first NDRUM, the rest keep the 16-track hue cycle
 * (HOME hero) from colliding on only 9 hues. */
static const uint16_t D_PAL[11] = {          /* drum lane identity colors (emu DPAL) */
    RGB565(224, 92, 92), RGB565(224, 139, 60), RGB565(217, 201, 74), RGB565(127, 217, 74),
    RGB565(74, 217, 168), RGB565(74, 197, 217), RGB565(92, 127, 224), RGB565(154, 92, 224),
    RGB565(224, 92, 168), RGB565(224, 92, 127), RGB565(201, 217, 107),
};
#define TRACK_HUE(i) D_PAL[(i) % 11]

/* alpha blend two RGB565 colors: out = fg*(255-a)/255 + bg*a/255
 * (a=0 keeps fg, a=255 keeps bg — emu globalAlpha over bg) */
static uint16_t blend(uint16_t fg, uint16_t bg, uint8_t a)
{
    uint32_t r1 = (fg >> 11) & 31u, g1 = (fg >> 5) & 63u, b1 = fg & 31u;
    uint32_t r2 = (bg >> 11) & 31u, g2 = (bg >> 5) & 63u, b2 = bg & 31u;
    return (uint16_t)(((r1 * (255u - a) + r2 * a) / 255u) << 11 |
                      ((g1 * (255u - a) + g2 * a) / 255u) << 5 |
                      ((b1 * (255u - a) + b2 * a) / 255u));
}

/* GM Level 1 program names, abbreviated to what fits. FONT_T is caps-only and
 * the value column of TRACK SET's PROGRAM row starts at x=96 (17 characters),
 * while the PRESET popup has the whole 240 px at FONT_M (10). One table, two
 * readers, so it lives here with the other shared tables rather than in the file
 * that happened to need it first. */
static const char *const GM_PROG[128] = {
    /*   0-  7 piano */      "ACOUSTIC GRAND", "BRIGHT PIANO", "ELECTRIC GRAND", "HONKY-TONK",
                             "ELECTRIC PIANO", "E.PIANO 2", "HARPSICHORD", "CLAVINET",
    /*   8- 15 chromatic */  "CELESTA", "GLOCKENSPIEL", "MUSIC BOX", "VIBRAPHONE",
                             "MARIMBA", "XYLOPHONE", "TUBULAR BELLS", "DULCIMER",
    /*  16- 23 organ */      "DRAWBAR ORGAN", "PERCUSSIVE ORG", "ROCK ORGAN", "CHURCH ORGAN",
                             "REED ORGAN", "ACCORDION", "HARMONICA", "TANGO ACCORDION",
    /*  24- 31 guitar */     "NYLON GUITAR", "STEEL GUITAR", "JAZZ GUITAR", "CLEAN GUITAR",
                             "MUTED GUITAR", "OVERDRIVE GT", "DISTORTION GT", "GT HARMONICS",
    /*  32- 39 bass */       "ACOUSTIC BASS", "FINGERED BASS", "PICKED BASS", "FRETLESS BASS",
                             "SLAP BASS 1", "SLAP BASS 2", "SYNTH BASS 1", "SYNTH BASS 2",
    /*  40- 47 strings */    "VIOLIN", "VIOLA", "CELLO", "CONTRABASS",
                             "TREMOLO STRINGS", "PIZZICATO", "ORCH HARP", "TIMPANI",
    /*  48- 55 ensemble */   "STRING ENSEMBLE", "STRINGS 2", "SYNTH STRINGS", "SYNTH STR 2",
                             "CHOIR AAHS", "VOICE OOHS", "SYNTH VOICE", "ORCHESTRA HIT",
    /*  56- 63 brass */      "TRUMPET", "TROMBONE", "TUBA", "MUTED TRUMPET",
                             "FRENCH HORN", "BRASS SECTION", "SYNTH BRASS 1", "SYNTH BRASS 2",
    /*  64- 71 reed */       "SOPRANO SAX", "ALTO SAX", "TENOR SAX", "BARITONE SAX",
                             "OBOE", "ENGLISH HORN", "BASSOON", "CLARINET",
    /*  72- 79 pipe */       "PICCOLO", "FLUTE", "RECORDER", "PAN FLUTE",
                             "BLOWN BOTTLE", "SHAKUHACHI", "WHISTLE", "OCARINA",
    /*  80- 87 synth lead */ "SQUARE LEAD", "SAWTOOTH LEAD", "CALLIOPE LEAD", "CHIFF LEAD",
                             "CHARANG LEAD", "VOICE LEAD", "FIFTHS LEAD", "BASS + LEAD",
    /*  88- 95 synth pad */  "NEW AGE PAD", "WARM PAD", "POLYSYNTH PAD", "CHOIR PAD",
                             "BOWED PAD", "METALLIC PAD", "HALO PAD", "SWEEP PAD",
    /*  96-103 synth fx */   "RAIN FX", "SOUNDTRACK FX", "CRYSTAL FX", "ATMOSPHERE FX",
                             "BRIGHTNESS FX", "GOBLINS FX", "ECHOES FX", "SCI-FI FX",
    /* 104-111 ethnic */     "SITAR", "BANJO", "SHAMISEN", "KOTO",
                             "KALIMBA", "BAG PIPE", "FIDDLE", "SHANAI",
    /* 112-119 percussive */ "TINKLE BELL", "AGOGO", "STEEL DRUMS", "WOODBLOCK",
                             "TAIKO DRUM", "MELODIC TOM", "SYNTH DRUM", "REV CYMBAL",
    /* 120-127 sound fx */   "FRET NOISE", "BREATH NOISE", "SEASHORE", "BIRD TWEET",
                             "TELEPHONE", "HELICOPTER", "APPLAUSE", "GUNSHOT"
};
