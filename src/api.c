#define _POSIX_C_SOURCE 200809L
#include "khatm.h"

int g_json = 0;

static void js(const char *s) {
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        default:
            if (c < 0x20) printf("\\u%04x", c);
            else putchar(c);
        }
    }
    putchar('"');
}

static void jkey(const char *k) { js(k); putchar(':'); }

static void jdate(time_t t) {
    if (!t) { fputs("null", stdout); return; }
    char b[32];
    fmt_date(t, b, sizeof b);
    js(b);
}

static void jts(time_t t) {
    if (!t) { fputs("null", stdout); return; }
    char b[64];
    fmt_ts(t, b, sizeof b);
    js(b);
}

void api_err(const char *msg) {
    fputs("{\"ok\":false,\"error\":", stdout);
    js(msg);
    fputs("}\n", stdout);
}

static const char *goal_status_str(int s) {
    switch (s) {
    case GOAL_KEPT:    return "kept";
    case GOAL_MISSED:  return "missed";
    case GOAL_DROPPED: return "dropped";
    default:           return "open";
    }
}

static void j_goal(State *st, Goal *go) {
    char rb[64];
    putchar('{');
    jkey("target"); js(ref_str(st, go->target, rb, sizeof rb));
    putchar(','); jkey("book"); js(st->books[go->target.book].id);
    putchar(','); jkey("chapter");
    if (go->target.ch >= 0) printf("%d", go->target.ch + 1);
    else fputs("null", stdout);
    putchar(','); jkey("set_at"); jts(go->set_at);
    putchar(','); jkey("by"); jdate(go->by);
    putchar(','); jkey("status"); js(goal_status_str(go->status));
    if (go->status == GOAL_OPEN) {
        double work = go->target.ch >= 0
            ? ch_weight(st, go->target) * (1 - ch_progress(st, go->target))
            : book_remaining_pages(st, go->target.book);
        int left = days_between(st->now, go->by);
        putchar(','); jkey("days_left"); printf("%d", left);
        putchar(','); jkey("need_per_day");
        printf("%.2f", work / (left < 1 ? 1 : left));
    }
    if (go->status == GOAL_KEPT) {
        putchar(','); jkey("kept_at"); jts(go->resolved_at);
    }
    putchar('}');
}

static void j_chapter(State *st, int b, int c) {
    Book *bk = &st->books[b];
    Chapter *ch = &bk->chs[c];
    Ref r = { b, c };
    char rb[64];
    putchar('{');
    jkey("n"); printf("%d", c + 1);
    putchar(','); jkey("ref"); js(ref_str(st, r, rb, sizeof rb));
    putchar(','); jkey("title"); js(ch->title);
    putchar(','); jkey("section");
    if (ch->section >= 0) js(bk->secs[ch->section].title);
    else fputs("null", stdout);
    putchar(','); jkey("est_pages");
    if (ch->est_pages > 0) printf("%g", ch->est_pages);
    else fputs("null", stdout);
    putchar(','); jkey("est_min");
    if (ch->est_min > 0) printf("%g", ch->est_min);
    else fputs("null", stdout);
    putchar(','); jkey("minutes"); printf("%g", ch->minutes);
    putchar(','); jkey("pages"); printf("%g", ch->pages);
    putchar(','); jkey("progress"); printf("%.4f", ch_progress(st, r));
    putchar(','); jkey("done"); fputs(ch->done_at ? "true" : "false", stdout);
    putchar(','); jkey("done_at"); jts(ch->done_at > 1 ? ch->done_at : 0);
    const char *status = ch->done_at ? "sealed"
                       : !deps_met(st, r) ? "blocked"
                       : ch->minutes > 0 ? "in_progress" : "ready";
    putchar(','); jkey("status"); js(status);
    putchar(','); jkey("needs"); putchar('[');
    for (int i = 0; i < ch->nneeds; i++) {
        if (i) putchar(',');
        js(ref_str(st, ch->needs[i], rb, sizeof rb));
    }
    putchar(']');
    putchar('}');
}

static void j_book(State *st, int b) {
    Book *bk = &st->books[b];
    putchar('{');
    jkey("id"); js(bk->id);
    putchar(','); jkey("title"); js(bk->title);
    putchar(','); jkey("path"); js(bk->path);
    putchar(','); jkey("deadline"); jdate(bk->deadline);
    putchar(','); jkey("progress"); printf("%.4f", book_progress(st, b));
    putchar(','); jkey("remaining_pages");
    printf("%.1f", book_remaining_pages(st, b));
    putchar(','); jkey("velocity_pages_28d");
    printf("%.3f", velocity_pages(st, b, 28));
    putchar(','); jkey("chapters"); putchar('[');
    for (int c = 0; c < bk->nchs; c++) {
        if (c) putchar(',');
        j_chapter(st, b, c);
    }
    putchar(']');
    putchar('}');
}

int cmd_dump(State *st, int argc, char **argv) {
    (void)argc; (void)argv;
    goals_refresh(st);

    double total = 0, today = 0;
    long tk = day_start(st->now) / 86400;
    for (int i = 0; i < st->nsess; i++) {
        total += st->sess[i].minutes;
        if (day_start(st->sess[i].at) / 86400 == tk)
            today += st->sess[i].minutes;
    }
    int k, m, o;
    double rate = kept_rate(st, &k, &m, &o);

    putchar('{');
    jkey("api"); fputs("1", stdout);
    putchar(','); jkey("now"); jts(st->now);
    putchar(','); jkey("root"); js(st->root);

    putchar(','); jkey("stats"); putchar('{');
    jkey("streak"); printf("%d", streak_current(st));
    putchar(','); jkey("longest_streak"); printf("%d", streak_longest(st));
    putchar(','); jkey("biggest_week_min"); printf("%.1f", biggest_week_min(st));
    putchar(','); jkey("total_min"); printf("%.1f", total);
    putchar(','); jkey("today_min"); printf("%.1f", today);
    putchar(','); jkey("velocity_pages_28d");
    printf("%.3f", velocity_pages(st, -1, 28));
    putchar(','); jkey("goals_kept"); printf("%d", k);
    putchar(','); jkey("goals_missed"); printf("%d", m);
    putchar(','); jkey("goals_open"); printf("%d", o);
    putchar(','); jkey("kept_rate");
    if (rate >= 0) printf("%.4f", rate);
    else fputs("null", stdout);
    putchar('}');

    putchar(','); jkey("suggestions"); putchar('[');
    NextPick picks[3];
    int np = plan_next(st, picks, 3);
    for (int i = 0; i < np; i++) {
        char rb[64];
        if (i) putchar(',');
        putchar('{');
        jkey("ref"); js(ref_str(st, picks[i].r, rb, sizeof rb));
        putchar(','); jkey("title");
        js(st->books[picks[i].r.book].chs[picks[i].r.ch].title);
        putchar(','); jkey("reason"); js(picks[i].reason);
        putchar(','); jkey("score"); printf("%.2f", picks[i].score);
        putchar('}');
    }
    putchar(']');

    putchar(','); jkey("books"); putchar('[');
    for (int b = 0; b < st->nbooks; b++) {
        if (b) putchar(',');
        j_book(st, b);
    }
    putchar(']');

    putchar(','); jkey("goals"); putchar('[');
    for (int g = 0; g < st->ngoals; g++) {
        if (g) putchar(',');
        j_goal(st, &st->goals[g]);
    }
    putchar(']');

    fputs("}\n", stdout);
    return 0;
}

int api_session_json(State *st, Ref r, double min, double pages,
                     int prev_streak, double prev_week) {
    char rb[64];
    fputs("{\"ok\":true,\"event\":\"session\",", stdout);
    jkey("ref"); js(ref_str(st, r, rb, sizeof rb));
    putchar(','); jkey("minutes"); printf("%g", min);
    putchar(','); jkey("pages"); printf("%g", pages);
    putchar(','); jkey("chapter_progress");
    printf("%.4f", ch_progress(st, r));
    putchar(','); jkey("streak"); printf("%d", streak_current(st));
    int rec_streak = streak_current(st) > prev_streak &&
                     streak_current(st) >= 3 &&
                     streak_current(st) == streak_longest(st);
    int rec_week = biggest_week_min(st) > prev_week && prev_week > 0 &&
                   biggest_week_min(st) >= 120;
    putchar(','); jkey("record_streak");
    fputs(rec_streak ? "true" : "false", stdout);
    putchar(','); jkey("record_week");
    fputs(rec_week ? "true" : "false", stdout);
    fputs("}\n", stdout);
    return 0;
}

int api_done_json(State *st, Ref r, int already) {
    char rb[64];
    Book *bk = &st->books[r.book];
    fputs("{\"ok\":true,\"event\":\"done\",", stdout);
    jkey("ref"); js(ref_str(st, r, rb, sizeof rb));
    putchar(','); jkey("already_sealed");
    fputs(already ? "true" : "false", stdout);
    if (!already) {
        int done = 0;
        for (int c = 0; c < bk->nchs; c++)
            if (bk->chs[c].done_at) done++;
        int kept = 0;
        for (int g = 0; g < st->ngoals; g++)
            if (st->goals[g].status == GOAL_KEPT &&
                st->goals[g].resolved_at == st->now &&
                goal_covers(st, &st->goals[g], r))
                kept = 1;
        putchar(','); jkey("unblocked"); printf("%d", count_unblocks(st, r));
        putchar(','); jkey("goal_kept"); fputs(kept ? "true" : "false", stdout);
        putchar(','); jkey("book_progress");
        printf("%.4f", book_progress(st, r.book));
        putchar(','); jkey("khatma");
        fputs(done == bk->nchs ? "true" : "false", stdout);
    }
    fputs("}\n", stdout);
    return 0;
}

int api_goal_json(State *st, Ref r, time_t by) {
    char rb[64];
    double work = r.ch >= 0
        ? ch_weight(st, r) * (1 - ch_progress(st, r))
        : book_remaining_pages(st, r.book);
    int days = days_between(st->now, by);
    if (days < 1) days = 1;
    double vel = velocity_pages(st, -1, 28);
    int k, m;
    double rate = kept_rate(st, &k, &m, NULL);

    fputs("{\"ok\":true,\"event\":\"goal\",", stdout);
    jkey("target"); js(ref_str(st, r, rb, sizeof rb));
    putchar(','); jkey("by"); jdate(by);
    putchar(','); jkey("days"); printf("%d", days);
    putchar(','); jkey("need_per_day"); printf("%.2f", work / days);
    putchar(','); jkey("velocity_pages_28d"); printf("%.3f", vel);
    putchar(','); jkey("kept_rate");
    if (rate >= 0) printf("%.4f", rate);
    else fputs("null", stdout);
    fputs("}\n", stdout);
    return 0;
}

int api_drop_json(State *st, Ref r) {
    char rb[64];
    fputs("{\"ok\":true,\"event\":\"drop-goal\",", stdout);
    jkey("target"); js(ref_str(st, r, rb, sizeof rb));
    fputs("}\n", stdout);
    return 0;
}
