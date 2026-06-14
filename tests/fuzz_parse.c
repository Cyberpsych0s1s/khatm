/* libFuzzer harness for khatm's parsers.
 *
 *   make fuzz && ./fuzz_parse -max_total_time=30 corpus/
 *
 * Each input is fed to the syllabus parser and then, against the resulting
 * (fuzzed) books, to the log replayer   the two gnarliest byte-eaters in the
 * codebase. The state is freed every iteration so leak detection stays useful.
 */
#define _POSIX_C_SOURCE 200809L
#include "khatm.h"
#include <stdint.h>

static void free_state(State *st) {
    for (int b = 0; b < st->nbooks; b++) {
        Book *bk = &st->books[b];
        free(bk->id); free(bk->title); free(bk->path);
        for (int c = 0; c < bk->nchs; c++) {
            Chapter *ch = &bk->chs[c];
            free(ch->title);
            for (int i = 0; i < ch->nneeds_raw; i++) free(ch->needs_raw[i]);
            free(ch->needs_raw);
            free(ch->needs);
            for (int k = 0; k < ch->ncards; k++) {
                free(ch->cards[k].front);
                free(ch->cards[k].back);
            }
            free(ch->cards);
        }
        free(bk->chs);
        for (int s = 0; s < bk->nsecs; s++) {
            Section *se = &bk->secs[s];
            free(se->title);
            for (int i = 0; i < se->nneeds_raw; i++) free(se->needs_raw[i]);
            free(se->needs_raw);
        }
        free(bk->secs);
        for (int t = 0; t < bk->ntags; t++) free(bk->tags[t]);
        free(bk->tags);
    }
    free(st->books);
    free(st->goals);
    free(st->sess);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    State st = {0};
    st.now = 1700000000;            /* fixed clock so dates are deterministic */
    char root[] = ".";
    st.root = root;

    FILE *f = fmemopen((void *)data, size, "r");
    if (f) { syl_parse_stream(&st, f, NULL, "fuzz"); fclose(f); }
    syl_resolve_all(&st, NULL);

    FILE *g = fmemopen((void *)data, size, "r");
    if (g) { log_parse_stream(&st, g); fclose(g); }
    goals_refresh(&st);

    /* exercise a few consumers across the fuzzed structures */
    NextPick picks[3];
    plan_next(&st, picks, 3);
    cards_due_count(&st, -1);
    for (int b = 0; b < st.nbooks; b++) book_progress(&st, b);

    free_state(&st);
    return 0;
}
