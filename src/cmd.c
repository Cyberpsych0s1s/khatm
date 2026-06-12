#define _POSIX_C_SOURCE 200809L
#include "khatm.h"
#include "plat.h"
#include <ctype.h>

static Chapter *CH(State *st, Ref r) { return &st->books[r.book].chs[r.ch]; }

int resolve_target(State *st, const char *arg, Ref *out, int allow_book) {
    char bookid[256];
    const char *slash = strchr(arg, '/');
    if (slash) {
        snprintf(bookid, sizeof bookid, "%.*s", (int)(slash - arg), arg);
        for (int b = 0; b < st->nbooks; b++) {
            if (!ieq(st->books[b].id, bookid)) continue;
            const char *rest = slash + 1;
            char *end;
            long n = strtol(rest, &end, 10);
            if (*rest && !*end) {
                if (n < 1 || n > st->books[b].nchs) {
                    fprintf(stderr, "khatm: %s has chapters 1..%d\n",
                            st->books[b].id, st->books[b].nchs);
                    return -1;
                }
                *out = (Ref){ b, (int)n - 1 };
                return 0;
            }
            for (int c = 0; c < st->books[b].nchs; c++)
                if (icontains(st->books[b].chs[c].title, rest)) {
                    *out = (Ref){ b, c };
                    return 0;
                }
            fprintf(stderr, "khatm: no chapter matching \"%s\" in %s\n",
                    rest, st->books[b].id);
            return -1;
        }
        fprintf(stderr, "khatm: no book \"%s\"\n", bookid);
        return -1;
    }

    for (int b = 0; b < st->nbooks; b++)
        if (ieq(st->books[b].id, arg)) {
            if (!allow_book) {
                fprintf(stderr,
                        "khatm: \"%s\" is a book — name a chapter "
                        "(%s/3) for this command\n", arg, arg);
                return -1;
            }
            *out = (Ref){ b, -1 };
            return 0;
        }

    Ref hits[8]; int nh = 0;
    for (int b = 0; b < st->nbooks; b++)
        for (int c = 0; c < st->books[b].nchs; c++)
            if (icontains(st->books[b].chs[c].title, arg) && nh < 8)
                hits[nh++] = (Ref){ b, c };
    if (nh == 1) { *out = hits[0]; return 0; }
    if (nh > 1) {
        fprintf(stderr, "khatm: \"%s\" is ambiguous:\n", arg);
        for (int i = 0; i < nh; i++) {
            char rb[64];
            fprintf(stderr, "  %-12s %s\n", ref_str(st, hits[i], rb, sizeof rb),
                    CH(st, hits[i])->title);
        }
        return -1;
    }
    fprintf(stderr, "khatm: nothing matches \"%s\"\n", arg);
    return -1;
}

static void show_kept_rate(State *st) {
    int k, m, o;
    double r = kept_rate(st, &k, &m, &o);
    if (r >= 0)
        printf("  your word, kept: %s%.0f%%%s over %d goal%s%s\n",
               r >= 0.8 ? CGREEN : r >= 0.5 ? CYELLOW : CRED,
               r * 100, CRESET, k + m, (k + m) == 1 ? "" : "s",
               o ? "" : "");
}

void announce_records(State *st, int prev_streak, double prev_week) {
    int s = streak_current(st);
    if (s > prev_streak && s >= 3 && s == streak_longest(st))
        printf("  %s★ new record:%s longest streak — %d days\n",
               CMAGENTA, CRESET, s);
    double w = biggest_week_min(st);
    if (w > prev_week && prev_week > 0 && w >= 120)
        printf("  %s★ new record:%s biggest week — %.1f hours\n",
               CMAGENTA, CRESET, w / 60.0);
}

static const char SAMPLE_BOOK[] =
"# The C Programming Language\n"
"meta: pages=272\n"
"\n"
"## Basics\n"
"- [ ] A Tutorial Introduction ~30p\n"
"- [ ] Types, Operators and Expressions ~22p\n"
"- [ ] Control Flow ~14p\n"
"\n"
"## The Core [needs: Basics]\n"
"- [ ] Functions and Program Structure ~24p\n"
"- [ ] Pointers and Arrays ~30p\n"
"- [ ] Structures ~24p\n"
"\n"
"## The Rest\n"
"- [ ] Input and Output ~20p [needs: 4]\n"
"- [ ] The UNIX System Interface ~20p [needs: 4]\n";

int cmd_init(State *st, int argc, char **argv) {
    (void)argc; (void)argv;
    char path[1024];
    if (plat_mkdir(st->root)) {
        fprintf(stderr, "khatm: cannot create %s\n", st->root);
        return 1;
    }
    snprintf(path, sizeof path, "%s/books", st->root);
    if (plat_mkdir(path)) {
        fprintf(stderr, "khatm: cannot create %s\n", path);
        return 1;
    }
    snprintf(path, sizeof path, "%s/books/knr.md", st->root);
    FILE *f = fopen(path, "r");
    if (f) { fclose(f); }
    else if ((f = fopen(path, "w"))) {
        fputs(SAMPLE_BOOK, f);
        fclose(f);
        printf("  wrote a sample syllabus: %s\n", path);
    }
    printf("  khatm root ready at %s\n", st->root);
    printf("  add books as %s/books/<id>.md — then: khatm next\n", st->root);
    return 0;
}

int cmd_books(State *st, int argc, char **argv) {
    (void)argc; (void)argv;
    if (!st->nbooks) {
        printf("  no books yet — drop a syllabus in %s/books/\n", st->root);
        return 0;
    }
    printf("\n");
    for (int b = 0; b < st->nbooks; b++) {
        Book *bk = &st->books[b];
        int done = 0;
        for (int c = 0; c < bk->nchs; c++)
            if (bk->chs[c].done_at) done++;
        printf("  %s%-10s%s %-34.34s ", CBOLD, bk->id, CRESET, bk->title);
        ui_bar(book_progress(st, b), 20);
        printf(" %2d/%-2d", done, bk->nchs);
        if (bk->deadline) {
            char d[32];
            fmt_date(bk->deadline, d, sizeof d);
            int left = days_between(st->now, bk->deadline);
            printf("  %sdue %s (%dd)%s",
                   left < 7 ? CYELLOW : CDIM, d, left, CRESET);
        }
        printf("\n");
    }
    printf("\n");
    return 0;
}

int cmd_next(State *st, int argc, char **argv) {
    (void)argc; (void)argv;
    NextPick picks[3];
    int n = plan_next(st, picks, 3);
    if (!n) {
        printf("  nothing to study — all chapters sealed or blocked.\n");
        printf("  (khatm doctor will show anything stuck.)\n");
        return 0;
    }
    char rb[64];
    Chapter *ch = CH(st, picks[0].r);
    printf("\n  %sstudy now:%s %s — %s%s%s\n", CBOLD, CRESET,
           ref_str(st, picks[0].r, rb, sizeof rb), CBOLD, ch->title, CRESET);
    printf("  %swhy:%s %s\n", CDIM, CRESET, picks[0].reason);
    if (ch->est_min > 0) {
        double left = ch->est_min - ch->minutes;
        if (left > 0) printf("  ~%.0f min left at your estimates\n", left);
    } else if (ch->est_pages > 0) {
        printf("  ~%.0f pages left\n", ch->est_pages - ch->pages);
    }
    for (int i = 1; i < n; i++) {
        printf("  %salso ready:%s %s — %s %s(%s)%s\n", CDIM, CRESET,
               ref_str(st, picks[i].r, rb, sizeof rb),
               CH(st, picks[i].r)->title, CDIM, picks[i].reason, CRESET);
    }
    printf("\n");
    return 0;
}

static void after_session(State *st, Ref r, double min, double pages,
                          int prev_streak, double prev_week) {
    Chapter *ch = CH(st, r);
    char rb[64];
    printf("  logged %.0f min%s on %s — %s\n",
           min, pages > 0 ? "" : "", ref_str(st, r, rb, sizeof rb), ch->title);
    double prog = ch_progress(st, r);
    if (prog > 0 && prog < 1) {
        printf("  chapter: ");
        ui_bar(prog, 24);
        printf(" %.0f%%", prog * 100);
        if (prog >= 0.8)
            printf("  %s— nearly there, one push seals it%s", CGREEN, CRESET);
        printf("\n");
    }
    int s = streak_current(st);
    if (s >= 2) printf("  streak: %s%d days%s\n", CGREEN, s, CRESET);
    announce_records(st, prev_streak, prev_week);
}

int cmd_log(State *st, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage: khatm log <chapter> <duration> [--pages N] "
                "[--date YYYY-MM-DD]\n");
        return 1;
    }
    Ref r;
    if (resolve_target(st, argv[0], &r, 0)) {
        if (g_json) api_err("target not found");
        return 1;
    }
    double min = parse_duration_min(argv[1]);
    if (min <= 0) {
        fprintf(stderr, "khatm: bad duration \"%s\" (try 35m, 1h30m)\n",
                argv[1]);
        if (g_json) api_err("bad duration");
        return 1;
    }
    double pages = 0;
    time_t when = st->now;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--pages") == 0) {
            if (i + 1 >= argc || parse_nonneg_double(argv[++i], &pages)) {
                fprintf(stderr, "khatm: bad pages value\n");
                if (g_json) api_err("bad pages");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--date") == 0 && i + 1 < argc) {
            time_t t = parse_when(argv[++i], st->now);
            if (!t) {
                fprintf(stderr, "khatm: bad date\n");
                if (g_json) api_err("bad date");
                return 1;
            }
            when = t - 43199;
        } else if (strcmp(argv[i], "--date") == 0) {
            fprintf(stderr, "khatm: --date needs a value\n");
            if (g_json) api_err("missing date");
            return 1;
        } else {
            fprintf(stderr, "khatm: unknown option \"%s\"\n", argv[i]);
            if (g_json) api_err("unknown option");
            return 1;
        }
    }
    int prev_streak = streak_current(st);
    double prev_week = biggest_week_min(st);
    if (ev_session(st, r, min, pages, when)) {
        if (g_json) api_err("could not write to the log");
        return 1;
    }
    if (g_json) return api_session_json(st, r, min, pages,
                                        prev_streak, prev_week);
    after_session(st, r, min, pages, prev_streak, prev_week);
    return 0;
}

int cmd_study(State *st, int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "usage: khatm study <chapter>\n");
        return 1;
    }
    Ref r;
    if (resolve_target(st, argv[0], &r, 0)) return 1;
    Chapter *ch = CH(st, r);
    char rb[64];
    printf("  studying %s — %s\n", ref_str(st, r, rb, sizeof rb), ch->title);
    printf("  press Enter when you stop.\n");
    time_t start = time(NULL);
    char buf[64];
    (void)fgets(buf, sizeof buf, stdin);
    double min = difftime(time(NULL), start) / 60.0;
    if (min < 1) min = 1;
    printf("  %.0f min. pages read (Enter for none): ", min);
    fflush(stdout);
    double pages = 0;
    if (fgets(buf, sizeof buf, stdin)) {
        char *s = trim(buf);
        if (*s && parse_nonneg_double(s, &pages)) pages = 0;
    }

    int prev_streak = streak_current(st);
    double prev_week = biggest_week_min(st);
    if (ev_session(st, r, min, pages, st->now)) return 1;
    after_session(st, r, min, pages, prev_streak, prev_week);
    return 0;
}

int cmd_goal(State *st, int argc, char **argv) {
    if (argc >= 1 && strcmp(argv[0], "--drop") == 0) {
        if (argc != 2) { fprintf(stderr, "usage: khatm goal --drop <target>\n"); return 1; }
        Ref r;
        if (resolve_target(st, argv[1], &r, 1)) {
            if (g_json) api_err("target not found");
            return 1;
        }
        char rb[64];
        if (ev_dropgoal(st, r)) {
            if (g_json) api_err("could not write to the log");
            return 1;
        }
        if (g_json) return api_drop_json(st, r);
        printf("  goal on %s dropped — no judgment, it never counts "
               "against your word.\n", ref_str(st, r, rb, sizeof rb));
        return 0;
    }
    if (argc < 1) {
        if (g_json) return cmd_dump(st, 0, NULL);
        goals_refresh(st);
        int any = 0;
        char rb[64], d[32];
        for (int g = 0; g < st->ngoals; g++) {
            Goal *go = &st->goals[g];
            if (go->status != GOAL_OPEN) continue;
            fmt_date(go->by, d, sizeof d);
            int left = days_between(st->now, go->by);
            printf("  %s by %s (%s%dd left%s)\n",
                   ref_str(st, go->target, rb, sizeof rb), d,
                   left <= 1 ? CRED : left <= 3 ? CYELLOW : CGREEN,
                   left, CRESET);
            any = 1;
        }
        if (!any) printf("  no open goals — khatm goal <target> --by <when>\n");
        show_kept_rate(st);
        return 0;
    }

    const char *by = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--by") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "khatm: --by needs a value\n");
                if (g_json) api_err("missing date");
                return 1;
            }
            by = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "khatm: unknown option \"%s\"\n", argv[i]);
            if (g_json) api_err("unknown option");
            return 1;
        } else if (!by) {
            by = argv[i];
        } else {
            fprintf(stderr, "khatm: too many arguments\n");
            if (g_json) api_err("too many arguments");
            return 1;
        }
    }
    if (!by) {
        fprintf(stderr, "usage: khatm goal <target> --by <when>\n");
        return 1;
    }
    Ref r;
    if (resolve_target(st, argv[0], &r, 1)) {
        if (g_json) api_err("target not found");
        return 1;
    }
    time_t deadline = parse_when(by, st->now);
    if (!deadline) {
        fprintf(stderr,
                "khatm: bad date \"%s\" (try 2026-07-01, friday, +5d)\n", by);
        if (g_json) api_err("bad date");
        return 1;
    }
    if (deadline <= st->now) {
        fprintf(stderr, "khatm: that deadline is already past\n");
        if (g_json) api_err("deadline already past");
        return 1;
    }

    double work = 0;
    if (r.ch >= 0)
        work = ch_weight(st, r) * (1.0 - ch_progress(st, r));
    else
        work = book_remaining_pages(st, r.book);
    int days = days_between(st->now, deadline);
    if (days < 1) days = 1;
    double need = work / days;
    double vel = velocity_pages(st, -1, 28);

    char rb[64], dl[32];
    fmt_date(deadline, dl, sizeof dl);
    if (ev_goal(st, r, deadline)) {
        if (g_json) api_err("could not write to the log");
        return 1;
    }
    if (g_json) return api_goal_json(st, r, deadline);
    ref_str(st, r, rb, sizeof rb);

    printf("  promised: %s by %s (%d day%s)\n",
           rb, dl, days, days == 1 ? "" : "s");
    printf("  required pace: %s~%.0f pages/day%s", CBOLD, need, CRESET);
    if (vel > 0) {
        printf("  (your 4-week pace: %.*f/day — %s)",
               vel < 10 ? 1 : 0, vel,
               need <= vel ? "comfortably yours" :
               need <= vel * 1.5 ? "a stretch" : "steep — sure?");
    }
    printf("\n");
    double committed = 0;
    for (int i = 0; i < st->ngoals; i++) {
        Goal *go = &st->goals[i];
        if (go->status != GOAL_OPEN) continue;
        double w = go->target.ch >= 0
            ? ch_weight(st, go->target) * (1.0 - ch_progress(st, go->target))
            : book_remaining_pages(st, go->target.book);
        int dd = days_between(st->now, go->by);
        committed += w / (dd < 1 ? 1 : dd);
    }
    if (vel > 0 && committed > vel * 1.3)
        printf("  %s⚠ all open goals together need %.0f pages/day — "
               "above your pace.%s\n", CYELLOW, committed, CRESET);
    show_kept_rate(st);
    return 0;
}

int cmd_done(State *st, int argc, char **argv) {
    int quiet = 0;
    const char *target = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0) quiet = 1;
        else if (argv[i][0] == '-') {
            fprintf(stderr, "khatm: unknown option \"%s\"\n", argv[i]);
            if (g_json) api_err("unknown option");
            return 1;
        } else if (!target) {
            target = argv[i];
        } else {
            fprintf(stderr, "khatm: too many arguments\n");
            if (g_json) api_err("too many arguments");
            return 1;
        }
    }
    if (!target) {
        fprintf(stderr, "usage: khatm done <chapter> [-q]\n");
        return 1;
    }
    Ref r;
    if (resolve_target(st, target, &r, 0)) {
        if (g_json) api_err("target not found");
        return 1;
    }
    Chapter *ch = CH(st, r);
    char rb[64];
    if (ch->done_at) {
        if (g_json) return api_done_json(st, r, 1);
        printf("  %s is already sealed.\n", ref_str(st, r, rb, sizeof rb));
        return 0;
    }
    if (!g_json && !deps_met(st, r)) {
        printf("  %snote:%s prerequisites of %s are not all sealed — "
               "sealing anyway.\n", CYELLOW, CRESET,
               ref_str(st, r, rb, sizeof rb));
    }

    if (ev_done(st, r)) {
        if (g_json) api_err("could not write to the log");
        return 1;
    }
    if (g_json) return api_done_json(st, r, 0);
    ui_seal(st, r, quiet);

    char d[32];
    for (int g = 0; g < st->ngoals; g++) {
        Goal *go = &st->goals[g];
        if (go->status == GOAL_KEPT && go->resolved_at == st->now &&
            goal_covers(st, go, r)) {
            fmt_date(go->by, d, sizeof d);
            printf("   %s✓ goal kept%s — promised by %s\n",
                   CGREEN, CRESET, d);
        }
    }
    show_kept_rate(st);

    Book *bk = &st->books[r.book];
    int all = 1;
    for (int c = 0; c < bk->nchs; c++)
        if (!bk->chs[c].done_at) { all = 0; break; }
    if (all) ui_khatma(st, r.book);
    return 0;
}

int cmd_pace(State *st, int argc, char **argv) {
    int from = 0, to = st->nbooks;
    if (argc >= 1) {
        Ref r;
        if (resolve_target(st, argv[0], &r, 1)) return 1;
        from = r.book; to = r.book + 1;
    }
    for (int b = from; b < to; b++) {
        Book *bk = &st->books[b];
        double rem = book_remaining_pages(st, b);
        double vp = velocity_pages(st, b, 28);
        if (vp <= 0) vp = velocity_pages(st, -1, 28);
        printf("\n  %s%s%s\n", CBOLD, bk->title, CRESET);
        printf("  progress: ");
        ui_bar(book_progress(st, b), 30);
        printf(" %.0f%%\n", book_progress(st, b) * 100);
        printf("  remaining: ~%.0f pages-equivalent\n", rem);
        if (vp > 0 && rem > 0) {
            int days = (int)(rem / vp + 0.999);
            char d[32];
            fmt_date(st->now + (time_t)days * 86400, d, sizeof d);
            printf("  at your 4-week pace (%.1f p/day): done %s%s%s\n",
                   vp, CBOLD, d, CRESET);
            if (bk->deadline) {
                int slack = days_between(st->now, bk->deadline) - days;
                if (slack >= 0)
                    printf("  %s%d days ahead of the %s deadline%s\n",
                           CGREEN, slack, "book", CRESET);
                else {
                    int dleft = days_between(st->now, bk->deadline);
                    double need = rem / (dleft < 1 ? 1 : dleft);
                    printf("  %s%d days late at this pace — hold %.0f p/day "
                           "to make it%s\n", CYELLOW, -slack,
                           need, CRESET);
                }
            }
        } else if (rem > 0) {
            printf("  no recent sessions — log one and pace appears.\n");
        }
    }
    printf("\n");
    return 0;
}

int cmd_status(State *st, int argc, char **argv) {
    (void)argc; (void)argv;
    goals_refresh(st);

    double today_min = 0;
    long tk = day_start(st->now) / 86400;
    for (int i = 0; i < st->nsess; i++)
        if (day_start(st->sess[i].at) / 86400 == tk)
            today_min += st->sess[i].minutes;

    printf("\n  %skhatm%s — ", CBOLD, CRESET);
    char d[32];
    fmt_date(st->now, d, sizeof d);
    printf("%s\n\n", d);
    printf("  today: %.0f min", today_min);
    int s = streak_current(st);
    if (s > 0) printf("   streak: %s%d day%s%s", CGREEN, s,
                      s == 1 ? "" : "s", CRESET);
    printf("\n");
    show_kept_rate(st);

    int any = 0;
    char rb[64];
    for (int g = 0; g < st->ngoals; g++) {
        Goal *go = &st->goals[g];
        if (go->status != GOAL_OPEN) continue;
        if (!any) printf("\n  %sopen promises:%s\n", CBOLD, CRESET);
        any = 1;
        double w = go->target.ch >= 0
            ? ch_weight(st, go->target) * (1.0 - ch_progress(st, go->target))
            : book_remaining_pages(st, go->target.book);
        int left = days_between(st->now, go->by);
        fmt_date(go->by, d, sizeof d);
        printf("   %-14s by %s — %s%dd left%s, ~%.0f pages to go\n",
               ref_str(st, go->target, rb, sizeof rb), d,
               left <= 1 ? CRED : left <= 3 ? CYELLOW : CGREEN, left, CRESET,
               w);
    }

    NextPick pick[1];
    if (plan_next(st, pick, 1) > 0) {
        printf("\n  %sstudy now:%s %s — %s %s(%s)%s\n", CBOLD, CRESET,
               ref_str(st, pick[0].r, rb, sizeof rb),
               CH(st, pick[0].r)->title, CDIM, pick[0].reason, CRESET);
    }
    printf("\n");
    return 0;
}

int cmd_shelf(State *st, int argc, char **argv) {
    (void)argc; (void)argv;
    ui_shelf(st);
    return 0;
}

int cmd_graph(State *st, int argc, char **argv) {
    int weeks = 26;
    if (argc >= 1) {
        int w = atoi(argv[0]);
        if (w >= 1 && w <= 52) weeks = w;
    }
    ui_heatmap(st, weeks);
    return 0;
}

static int cycle_dfs(State *st, Ref r, char *mark) {
    int base = 0;
    for (int b = 0; b < r.book; b++) base += st->books[b].nchs;
    int idx = base + r.ch;
    if (mark[idx] == 1) return 1;
    if (mark[idx] == 2) return 0;
    mark[idx] = 1;
    Chapter *ch = CH(st, r);
    for (int i = 0; i < ch->nneeds; i++)
        if (cycle_dfs(st, ch->needs[i], mark)) return 1;
    mark[idx] = 2;
    return 0;
}

int cmd_doctor(State *st, int argc, char **argv) {
    (void)argc; (void)argv;
    goals_refresh(st);
    int issues = 0;
    printf("\n");

    for (int b = 0; b < st->nbooks; b++)
        for (int c = 0; c < st->books[b].nchs; c++) {
            Chapter *ch = &st->books[b].chs[c];
            for (int i = 0; i < ch->nneeds_raw; i++)
                if (!syl_need_resolves(st, b, ch->needs_raw[i], ch)) {
                    printf("  %s✗%s %s/%d \"%s\": unresolved needs \"%s\"\n",
                           CRED, CRESET, st->books[b].id, c + 1,
                           ch->title, ch->needs_raw[i]);
                    issues++;
                }
            if (ch->section < 0) continue;
            Section *sec = &st->books[b].secs[ch->section];
            int first_of_sec = c == 0 || st->books[b].chs[c - 1].section != ch->section;
            if (!first_of_sec) continue;
            for (int i = 0; i < sec->nneeds_raw; i++)
                if (!syl_need_resolves(st, b, sec->needs_raw[i], ch)) {
                    printf("  %s✗%s %s section \"%s\": unresolved needs \"%s\"\n",
                           CRED, CRESET, st->books[b].id, sec->title,
                           sec->needs_raw[i]);
                    issues++;
                }
        }

    int total = 0;
    for (int b = 0; b < st->nbooks; b++) total += st->books[b].nchs;
    if (total > 0) {
        char *mark = xmalloc((size_t)total);
        memset(mark, 0, (size_t)total);
        for (int b = 0; b < st->nbooks; b++)
            for (int c = 0; c < st->books[b].nchs; c++)
                if (cycle_dfs(st, (Ref){ b, c }, mark)) {
                    printf("  %s✗%s dependency cycle involving %s/%d\n",
                           CRED, CRESET, st->books[b].id, c + 1);
                    issues++;
                    memset(mark, 0, (size_t)total);
                }
        free(mark);
    }

    char rb[64], d[32];
    for (int g = 0; g < st->ngoals; g++) {
        Goal *go = &st->goals[g];
        if (go->status != GOAL_MISSED) continue;
        int already_done = go->target.ch >= 0 && CH(st, go->target)->done_at;
        if (already_done) continue;
        fmt_date(go->by, d, sizeof d);
        printf("  %s!%s missed promise: %s by %s — re-promise "
               "(khatm goal) or drop it (khatm goal --drop)\n",
               CYELLOW, CRESET, ref_str(st, go->target, rb, sizeof rb), d);
        issues++;
    }

    for (int b = 0; b < st->nbooks; b++) {
        Book *bk = &st->books[b];
        double sum = 0; int n = 0;
        for (int c = 0; c < bk->nchs; c++) {
            Chapter *ch = &bk->chs[c];
            if (ch->done_at && ch->est_min > 0 && ch->minutes > 0) {
                sum += ch->minutes / ch->est_min;
                n++;
            }
        }
        if (n >= 2) {
            double bias = sum / n;
            if (bias > 1.25)
                printf("  %s~%s %s: you under-estimate time by ~%.0f%% "
                       "(across %d sealed chapters)\n", CCYAN, CRESET,
                       bk->id, (bias - 1) * 100, n);
            else if (bias < 0.8)
                printf("  %s~%s %s: you over-estimate time by ~%.0f%% — "
                       "you are faster than you think\n", CCYAN, CRESET,
                       bk->id, (1 - bias) * 100);
        }
    }

    for (int b = 0; b < st->nbooks; b++) {
        Book *bk = &st->books[b];
        if (!bk->deadline) {
            int undone = 0;
            for (int c = 0; c < bk->nchs; c++)
                if (!bk->chs[c].done_at) undone++;
            if (undone)
                printf("  %s·%s %s has no deadline — pace forecasts will "
                       "be weaker (meta: deadline=...)\n", CDIM, CRESET,
                       bk->id);
        }
    }

    if (!issues) printf("  %s✓%s no problems found\n", CGREEN, CRESET);
    printf("\n");
    return 0;
}
