#define _POSIX_C_SOURCE 200809L
#include "khatm.h"
#include <math.h>

static Chapter *CH(State *st, Ref r) { return &st->books[r.book].chs[r.ch]; }

double ch_weight(State *st, Ref r) {
    Book *bk = &st->books[r.book];
    Chapter *ch = CH(st, r);
    if (ch->est_pages > 0) return ch->est_pages;
    if (ch->est_min > 0) return ch->est_min / 3.0;
    if (bk->total_pages > 0 && bk->nchs > 0)
        return bk->total_pages / bk->nchs;
    return 10.0;
}

double ch_progress(State *st, Ref r) {
    Chapter *ch = CH(st, r);
    if (ch->done_at) return 1.0;
    double p = 0;
    if (ch->est_pages > 0 && ch->pages > 0)
        p = ch->pages / ch->est_pages;
    else if (ch->est_min > 0 && ch->minutes > 0)
        p = ch->minutes / ch->est_min;
    else if (ch->minutes > 0)
        p = 0.25;
    return p > 0.99 ? 0.99 : p;
}

double book_progress(State *st, int b) {
    Book *bk = &st->books[b];
    double total = 0, got = 0;
    for (int c = 0; c < bk->nchs; c++) {
        Ref r = { b, c };
        double w = ch_weight(st, r);
        total += w;
        got += w * ch_progress(st, r);
    }
    return total > 0 ? got / total : 0;
}

double book_remaining_pages(State *st, int b) {
    Book *bk = &st->books[b];
    double rem = 0;
    for (int c = 0; c < bk->nchs; c++) {
        Ref r = { b, c };
        rem += ch_weight(st, r) * (1.0 - ch_progress(st, r));
    }
    return rem;
}

static double velocity(State *st, int b, int days, int want_pages) {
    time_t since = day_start(st->now) - (time_t)days * 86400;
    double sum = 0;
    for (int i = 0; i < st->nsess; i++) {
        Session *s = &st->sess[i];
        if (s->at < since) continue;
        if (b >= 0 && s->ch.book != b) continue;
        sum += want_pages ? s->pages : s->minutes;
    }
    return sum / days;
}

double velocity_pages(State *st, int b, int days) {
    return velocity(st, b, days, 1);
}
double velocity_min(State *st, int b, int days) {
    return velocity(st, b, days, 0);
}

int goal_covers(State *st, Goal *g, Ref ch) {
    (void)st;
    if (g->target.book != ch.book) return 0;
    return g->target.ch == -1 || g->target.ch == ch.ch;
}

void goals_refresh(State *st) {
    for (int g = 0; g < st->ngoals; g++) {
        Goal *go = &st->goals[g];
        if (go->status == GOAL_DROPPED) continue;
        time_t finished = 0;
        if (go->target.ch >= 0) {
            finished = CH(st, go->target)->done_at;
        } else {
            Book *bk = &st->books[go->target.book];
            finished = 1;
            for (int c = 0; c < bk->nchs; c++) {
                time_t d = bk->chs[c].done_at;
                if (!d) { finished = 0; break; }
                if (d > finished) finished = d;
            }
        }
        if (finished && (finished <= go->by || finished == 1)) {
            go->status = GOAL_KEPT;
            go->resolved_at = finished;
        } else if (st->now > go->by) {
            go->status = GOAL_MISSED;
        } else {
            go->status = GOAL_OPEN;
        }
    }
}

double kept_rate(State *st, int *kept, int *missed, int *open_) {
    int k = 0, m = 0, o = 0;
    for (int g = 0; g < st->ngoals; g++) {
        switch (st->goals[g].status) {
        case GOAL_KEPT:   k++; break;
        case GOAL_MISSED: m++; break;
        case GOAL_OPEN:   o++; break;
        }
    }
    if (kept) *kept = k;
    if (missed) *missed = m;
    if (open_) *open_ = o;
    return (k + m) ? (double)k / (k + m) : -1;
}

static long day_key(time_t t) {
    return (long)(day_start(t) / 86400);
}

static int has_session_on(State *st, long key) {
    for (int i = 0; i < st->nsess; i++)
        if (day_key(st->sess[i].at) == key) return 1;
    return 0;
}

int streak_current(State *st) {
    long today = day_key(st->now);
    long d = today;
    if (!has_session_on(st, d)) d--;
    int n = 0;
    while (has_session_on(st, d)) { n++; d--; }
    return n;
}

int streak_longest(State *st) {
    if (!st->nsess) return 0;
    int best = 0, cur = 0;
    long prev = -2;
    for (int i = 0; i < st->nsess; i++) {
        long k = day_key(st->sess[i].at);
        if (k == prev) continue;
        cur = (k == prev + 1) ? cur + 1 : 1;
        if (cur > best) best = cur;
        prev = k;
    }
    return best;
}

double biggest_week_min(State *st) {
    double best = 0;
    for (int i = 0; i < st->nsess; i++) {
        time_t start = day_start(st->sess[i].at);
        time_t end = start + 7 * 86400;
        double sum = 0;
        for (int j = i; j < st->nsess && st->sess[j].at < end; j++)
            sum += st->sess[j].minutes;
        if (sum > best) best = sum;
    }
    return best;
}
