/* tools/density.c — how full is a real pattern, and what would each packing
 * scheme cost?
 *
 * The struct today is unconditional: 64 steps x 4 voices per track, 16 tracks,
 * whether the demo uses them or not. Before arguing about a format, measure the
 * thing being packed. Build:
 *
 *   cc -O2 -I engine tools/density.c engine/driftseq.c engine/demo_pattern.c -lm
 */
#include <stdio.h>
#include <string.h>
#include "driftseq.h"

static void report(const char *name, ds_state *s)
{
    int t, i, vi;
    long used_steps = 0, used_voices = 0, nondefault = 0;
    printf("\n=== %s ===\n", name);
    printf("%-4s %-7s %-5s %-7s %-7s %-8s\n",
           "trk", "loop", "div", "onSteps", "voices", "meta?");
    for (t = 0; t < DS_TRACKS; t++) {
        ds_track *tr = &s->pat.track[t];
        int on = 0, voc = 0, md = 0;
        for (i = 0; i < DS_STEPS; i++) {
            ds_step *st = &tr->steps[i];
            if (st->on) on++;
            for (vi = 0; vi < st->nvoices; vi++)
                if (st->v[vi].note != 0xFF) voc++;
            if (st->on || st->nvoices) md++;
        }
        used_steps += md; used_voices += voc; nondefault += md;
        if (md)
            printf("%-4d %-7d %-5d %-7d %-7d %s\n", t, tr->loop_len, tr->division,
                   on, voc, tr->eu_on ? "euclid" : "");
    }
    printf("\nsteps holding anything : %ld of %d  (%.1f%%)\n",
           used_steps, DS_TRACKS * DS_STEPS, 100.0 * used_steps / (DS_TRACKS * DS_STEPS));
    printf("voices present        : %ld of %d\n", used_voices, DS_TRACKS * DS_STEPS * DS_VOICES);
    printf("tracks with anything  : ");
    { int n = 0; for (t = 0; t < DS_TRACKS; t++) {
        int md = 0; for (i = 0; i < DS_STEPS; i++) if (s->pat.track[t].steps[i].on || s->pat.track[t].steps[i].nvoices) md = 1;
        n += md; } printf("%d of %d\n", n, DS_TRACKS); }
    printf("\nstruct today          : %zu B per pattern (%zu B per track, %zu B per step)\n",
           sizeof(ds_pattern), sizeof(ds_track), sizeof(ds_step));
}

int main(void)
{
    static ds_state s;
    ds_load_demo(&s);
    report("the demo pattern", &s);

    /* and an empty pattern: what does "nothing" cost today? */
    ds_init(&s, 1);
    report("an empty pattern", &s);
    return 0;
}
