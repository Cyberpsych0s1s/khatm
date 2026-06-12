#define _POSIX_C_SOURCE 200809L
#include "khatm.h"

static Chapter *CH(State *st, Ref r) { return &st->books[r.book].chs[r.ch]; }

int deps_met(State *st, Ref r) {
    Chapter *ch = CH(st, r);
    for (int i = 0; i < ch->nneeds; i++)
        if (!CH(st, ch->needs[i])->done_at) return 0;
    return 1;
}

int count_unblocks(State *st, Ref r) {
    int n = 0;
    for (int b = 0; b < st->nbooks; b++)
        for (int c = 0; c < st->books[b].nchs; c++) {
            Chapter *ch = &st->books[b].chs[c];
            if (ch->done_at) continue;
            for (int i = 0; i < ch->nneeds; i++)
                if (ch->needs[i].book == r.book && ch->needs[i].ch == r.ch) {
                    n++;
                    break;
                }
        }
    return n;
}

static time_t nearest_goal_by(State *st, Ref r) {
    time_t best = 0;
    for (int g = 0; g < st->ngoals; g++) {
        Goal *go = &st->goals[g];
        if (go->status != GOAL_OPEN || !goal_covers(st, go, r)) continue;
        if (!best || go->by < best) best = go->by;
    }
    return best;
}

int plan_next(State *st, NextPick *out, int max) {
    int n = 0;
    for (int b = 0; b < st->nbooks; b++) {
        Book *bk = &st->books[b];
        for (int c = 0; c < bk->nchs; c++) {
            Chapter *ch = &bk->chs[c];
            Ref r = { b, c };
            if (ch->done_at || !deps_met(st, r)) continue;

            double score = 1.0;
            char why[160] = "";
            size_t wl = 0;
            #define WHY(...) do { \
                int _k = snprintf(why + wl, sizeof why - wl, __VA_ARGS__); \
                if (_k > 0) wl += (size_t)_k; \
                if (wl >= sizeof why) wl = sizeof why - 1; \
            } while (0)

            time_t gby = nearest_goal_by(st, r);
            if (gby) {
                int d = days_between(st->now, gby);
                if (d < 0) d = 0;
                score += 200.0 / (d + 1);
                WHY("goal due in %dd", d);
            }
            if (bk->deadline) {
                int d = days_between(st->now, bk->deadline);
                if (d < 0) d = 0;
                score += 60.0 / (d + 1);
                WHY("%sbook due in %dd", wl ? ", " : "", d);
            }
            double prog = ch_progress(st, r);
            if (prog >= 0.99) {
                score += 60.0;
                WHY("%sfully read — seal it to finish", wl ? ", " : "");
            } else if (prog > 0.05) {
                score += 50.0 * prog;
                WHY("%s%.0f%% read — close the loop", wl ? ", " : "",
                    prog * 100);
            }
            int unb = count_unblocks(st, r);
            if (unb > 0) {
                score += 6.0 * unb;
                WHY("%sunblocks %d chapter%s", wl ? ", " : "", unb,
                    unb == 1 ? "" : "s");
            }
            score += (double)(bk->nchs - c) * 0.05;
            if (!wl) WHY("next in %s", bk->title);
            #undef WHY

            int pos = n < max ? n : max - 1;
            if (n < max) n++;
            else if (score <= out[max - 1].score) continue;
            while (pos > 0 && out[pos - 1].score < score) {
                out[pos] = out[pos - 1];
                pos--;
            }
            out[pos].r = r;
            out[pos].score = score;
            snprintf(out[pos].reason, sizeof out[pos].reason, "%s", why);
        }
    }
    return n;
}
