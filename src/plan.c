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

int plan_total_chs(State *st) {
    int n = 0;
    for (int b = 0; b < st->nbooks; b++) n += st->books[b].nchs;
    return n;
}

static int ch_index(State *st, Ref r) {
    int base = 0;
    for (int b = 0; b < r.book; b++) base += st->books[b].nchs;
    return base + r.ch;
}

/* Days of focused work left in a chapter at the global 4-week pace  
 * at least 1: a blocking chapter always costs its dependents a day. */
static int est_days(State *st, Ref r, double vel) {
    if (vel <= 0) return 1;
    double rem = ch_weight(st, r) * (1.0 - ch_progress(st, r));
    int d = (int)(rem / vel + 0.999);
    return d < 1 ? 1 : d;
}

void plan_implied(State *st, time_t *out) {
    double vel = velocity_pages(st, -1, 28);
    int total = plan_total_chs(st);

    for (int b = 0, i = 0; b < st->nbooks; b++)
        for (int c = 0; c < st->books[b].nchs; c++, i++) {
            Ref r = { b, c };
            out[i] = 0;
            if (st->books[b].chs[c].done_at) continue;
            time_t d = nearest_goal_by(st, r);
            time_t bd = st->books[b].deadline;
            if (bd && (!d || bd < d)) d = bd;
            out[i] = d;
        }

    /* Relax backwards through needs edges until fixpoint: a prerequisite
     * inherits its dependent's deadline minus the dependent's estimated
     * days of work. Pass count is bounded so a dep cycle cannot hang it. */
    for (int pass = 0; pass < total; pass++) {
        int changed = 0;
        for (int b = 0, i = 0; b < st->nbooks; b++)
            for (int c = 0; c < st->books[b].nchs; c++, i++) {
                Chapter *ch = &st->books[b].chs[c];
                if (ch->done_at || !out[i]) continue;
                time_t inherit = out[i] -
                    (time_t)est_days(st, (Ref){ b, c }, vel) * 86400;
                for (int k = 0; k < ch->nneeds; k++) {
                    Ref nr = ch->needs[k];
                    if (CH(st, nr)->done_at) continue;
                    int j = ch_index(st, nr);
                    if (!out[j] || inherit < out[j]) {
                        out[j] = inherit;
                        changed = 1;
                    }
                }
            }
        if (!changed) break;
    }
}

static void work_dfs(State *st, Ref r, char *mark, double *sum) {
    int i = ch_index(st, r);
    if (mark[i] || CH(st, r)->done_at) return;
    mark[i] = 1;
    *sum += ch_weight(st, r) * (1.0 - ch_progress(st, r));
    Chapter *ch = CH(st, r);
    for (int k = 0; k < ch->nneeds; k++)
        work_dfs(st, ch->needs[k], mark, sum);
}

double plan_goal_work(State *st, Ref r, double *prereq_out) {
    int total = plan_total_chs(st);
    char *mark = xmalloc((size_t)(total ? total : 1));
    memset(mark, 0, (size_t)(total ? total : 1));
    double target = 0, prereq = 0;

    if (r.ch >= 0) {
        Chapter *ch = CH(st, r);
        if (!ch->done_at) {
            mark[ch_index(st, r)] = 1;
            target = ch_weight(st, r) * (1.0 - ch_progress(st, r));
        }
        for (int k = 0; k < ch->nneeds; k++)
            work_dfs(st, ch->needs[k], mark, &prereq);
    } else {
        Book *bk = &st->books[r.book];
        for (int c = 0; c < bk->nchs; c++) {
            Ref cr = { r.book, c };
            if (bk->chs[c].done_at) continue;
            mark[ch_index(st, cr)] = 1;
            target += ch_weight(st, cr) * (1.0 - ch_progress(st, cr));
        }
        for (int c = 0; c < bk->nchs; c++) {
            Chapter *ch = &bk->chs[c];
            if (ch->done_at) continue;
            for (int k = 0; k < ch->nneeds; k++)
                work_dfs(st, ch->needs[k], mark, &prereq);
        }
    }
    free(mark);
    if (prereq_out) *prereq_out = prereq;
    return target + prereq;
}

int plan_next(State *st, NextPick *out, int max) {
    int n = 0;
    int total = plan_total_chs(st);
    time_t *implied = NULL;
    if (total) {
        implied = xmalloc((size_t)total * sizeof *implied);
        plan_implied(st, implied);
    }
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
            time_t direct = gby;
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
                if (!direct || bk->deadline < direct)
                    direct = bk->deadline;
            }
            time_t imp = implied ? implied[ch_index(st, r)] : 0;
            if (imp && (!direct || imp < direct)) {
                int d = days_between(st->now, imp);
                if (d < 0) d = 0;
                score += 150.0 / (d + 1);
                WHY("%sblocks work due in %dd", wl ? ", " : "", d);
            }
            double prog = ch_progress(st, r);
            if (prog >= 0.99) {
                score += 60.0;
                WHY("%sfully read   seal it to finish", wl ? ", " : "");
            } else if (prog > 0.05) {
                score += 50.0 * prog;
                WHY("%s%.0f%% read   close the loop", wl ? ", " : "",
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
    free(implied);
    return n;
}
