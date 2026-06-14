#define _POSIX_C_SOURCE 200809L
#include "khatm.h"
#include "plat.h"
#include <ctype.h>

static Chapter *CH(State *st, Ref r) { return &st->books[r.book].chs[r.ch]; }

static int iprefix(const char *s, const char *pfx) {
    while (*pfx) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*pfx))
            return 0;
        s++; pfx++;
    }
    return 1;
}

/* Book lookup: exact id, else unique case-insensitive prefix.
 * Returns the index, -1 if nothing matches, -2 if the prefix is ambiguous. */
static int find_book(State *st, const char *id) {
    for (int b = 0; b < st->nbooks; b++)
        if (ieq(st->books[b].id, id)) return b;
    int hit = -1, n = 0;
    for (int b = 0; b < st->nbooks; b++)
        if (iprefix(st->books[b].id, id)) { hit = b; n++; }
    return n == 1 ? hit : n ? -2 : -1;
}

int resolve_target(State *st, const char *arg, Ref *out, int allow_book) {
    char bookid[256];
    const char *slash = strchr(arg, '/');
    if (slash) {
        snprintf(bookid, sizeof bookid, "%.*s", (int)(slash - arg), arg);
        int b = find_book(st, bookid);
        if (b == -2) {
            fprintf(stderr, "khatm: book \"%s\" is ambiguous\n", bookid);
            return -1;
        }
        if (b < 0) {
            fprintf(stderr, "khatm: no book \"%s\"\n", bookid);
            return -1;
        }
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

    for (int b = 0; b < st->nbooks; b++)
        if (ieq(st->books[b].id, arg)) {
            if (!allow_book) {
                fprintf(stderr,
                        "khatm: \"%s\" is a book   name a chapter "
                        "(%s/3) for this command\n", arg, arg);
                return -1;
            }
            *out = (Ref){ b, -1 };
            return 0;
        }

    /* "knr5": a book id (or unique prefix of one) glued to a chapter
     * number   the zero-friction logging form. */
    size_t alen = strlen(arg), digs = alen;
    while (digs > 0 && isdigit((unsigned char)arg[digs - 1])) digs--;
    if (digs > 0 && digs < alen) {
        snprintf(bookid, sizeof bookid, "%.*s", (int)digs, arg);
        int b = find_book(st, bookid);
        if (b >= 0) {
            long n = strtol(arg + digs, NULL, 10);
            if (n >= 1 && n <= st->books[b].nchs) {
                *out = (Ref){ b, (int)n - 1 };
                return 0;
            }
            fprintf(stderr, "khatm: %s has chapters 1..%d\n",
                    st->books[b].id, st->books[b].nchs);
            return -1;
        }
    }

    int pb = find_book(st, arg);
    if (pb >= 0) {
        if (!allow_book) {
            fprintf(stderr,
                    "khatm: \"%s\" is the book %s   name a chapter "
                    "(%s/3) for this command\n", arg, st->books[pb].id,
                    st->books[pb].id);
            return -1;
        }
        *out = (Ref){ pb, -1 };
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
    if (pb == -2) {
        fprintf(stderr, "khatm: \"%s\" matches several books:", arg);
        for (int b = 0; b < st->nbooks; b++)
            if (iprefix(st->books[b].id, arg))
                fprintf(stderr, " %s", st->books[b].id);
        fprintf(stderr, "\n");
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
        printf("  %s★ new record:%s longest streak   %d days\n",
               CMAGENTA, CRESET, s);
    double w = biggest_week_min(st);
    if (w > prev_week && prev_week > 0 && w >= 120)
        printf("  %s★ new record:%s biggest week   %.1f hours\n",
               CMAGENTA, CRESET, w / 60.0);
}

static const char SAMPLE_BOOK[] =
"# The C Programming Language\n"
"meta: pages=272\n"
"\n"
"## Basics\n"
"- [ ] A Tutorial Introduction ~30p\n"
"  ? how do you print text in C :: printf from <stdio.h>\n"
"  ? what does main return :: an int, the process exit status\n"
"- [ ] Types, Operators and Expressions ~22p\n"
"- [ ] Control Flow ~14p\n"
"\n"
"## The Core [needs: Basics]\n"
"- [ ] Functions and Program Structure ~24p\n"
"- [ ] Pointers and Arrays ~30p\n"
"  ? what is a dangling pointer :: a pointer to memory that was freed\n"
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
    printf("  add books as %s/books/<id>.md   then: khatm next\n", st->root);
    return 0;
}

/* khatm book new <id> [title...] [--pages N] [--deadline WHEN]
 * khatm book add <id> <title...> [--est 12p] [--needs LIST]
 * khatm book section <id> <title...> [--needs LIST]
 * khatm book path <id> */
int cmd_book(State *st, int argc, char **argv) {
    static const char *use =
        "usage: khatm book new <id> [title] [--pages N] [--deadline D]\n"
        "       khatm book add <id> <title> [--est 12p|2h] [--needs ...]\n"
        "       khatm book section <id> <title> [--needs ...]\n"
        "       khatm book path <id>\n";
    if (argc < 2) { fputs(use, stderr); return 1; }
    const char *sub = argv[0], *id = argv[1];

    if (strcmp(sub, "path") == 0) {
        Ref r;
        if (resolve_target(st, id, &r, 1) || r.ch != -1) {
            if (g_json) api_err("no such book");
            return 1;
        }
        printf("%s\n", st->books[r.book].path);
        return 0;
    }

    /* gather: positionals after id join into the title; flags take values */
    char title[256] = "";
    const char *est = NULL, *needs = NULL, *deadline_s = NULL;
    double pages = 0;
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        const char **val = NULL;
        if (strcmp(a, "--est") == 0) val = &est;
        else if (strcmp(a, "--needs") == 0) val = &needs;
        else if (strcmp(a, "--deadline") == 0) val = &deadline_s;
        else if (strcmp(a, "--pages") == 0) {
            if (i + 1 >= argc || parse_nonneg_double(argv[++i], &pages)) {
                fprintf(stderr, "khatm: bad pages value\n");
                if (g_json) api_err("bad pages");
                return 1;
            }
            continue;
        } else if (a[0] == '-' && a[1] == '-') {
            fprintf(stderr, "khatm: unknown option \"%s\"\n", a);
            if (g_json) api_err("unknown option");
            return 1;
        } else {
            size_t len = strlen(title);
            snprintf(title + len, sizeof title - len, "%s%s",
                     len ? " " : "", a);
            continue;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "khatm: %s needs a value\n", a);
            if (g_json) api_err("missing value");
            return 1;
        }
        *val = argv[++i];
    }

    char err[256] = "";
    if (strcmp(sub, "new") == 0) {
        time_t by = 0;
        if (deadline_s) {
            by = parse_when(deadline_s, st->now);
            if (!by) {
                fprintf(stderr, "khatm: bad deadline \"%s\"\n", deadline_s);
                if (g_json) api_err("bad date");
                return 1;
            }
        }
        if (syl_book_new(st, id, *title ? title : id, pages, by,
                         err, sizeof err)) {
            fprintf(stderr, "khatm: %s\n", err);
            if (g_json) api_err(err);
            return 1;
        }
        if (g_json) return api_book_json("book-new", id, 0);
        printf("  book %s started   now add chapters:\n"
               "  khatm book add %s \"Chapter title\" --est 12p\n", id, id);
        return 0;
    }
    if (strcmp(sub, "add") == 0 || strcmp(sub, "section") == 0) {
        if (!*title) {
            fprintf(stderr, "khatm: give the %s a title\n",
                    sub[0] == 'a' ? "chapter" : "section");
            if (g_json) api_err("missing title");
            return 1;
        }
        int rc = sub[0] == 'a'
            ? syl_book_add(st, id, title, est, needs, err, sizeof err)
            : syl_book_section(st, id, title, needs, err, sizeof err);
        if (rc) {
            fprintf(stderr, "khatm: %s\n", err);
            if (g_json) api_err(err);
            return 1;
        }
        int b = -1;
        for (int i = 0; i < st->nbooks; i++)
            if (ieq(st->books[i].id, id)) { b = i; break; }
        int chn = b >= 0 ? st->books[b].nchs : 1;
        if (sub[0] == 'a') {
            if (g_json) return api_book_json("book-add", id, chn);
            printf("  added %s/%d   %s\n", b >= 0 ? st->books[b].id : id,
                   chn, title);
            if (needs) printf("  (khatm doctor verifies the needs)\n");
        } else {
            if (g_json) return api_book_json("book-section", id, 0);
            printf("  section \"%s\" opened   chapters added now belong "
                   "to it\n", title);
        }
        return 0;
    }
    fputs(use, stderr);
    if (g_json) api_err("unknown book subcommand");
    return 1;
}

int cmd_books(State *st, int argc, char **argv) {
    (void)argc; (void)argv;
    if (!st->nbooks) {
        printf("  no books yet   drop a syllabus in %s/books/\n", st->root);
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
        printf("  nothing to study   all chapters sealed or blocked.\n");
        printf("  (khatm doctor will show anything stuck.)\n");
        return 0;
    }
    char rb[64];
    Chapter *ch = CH(st, picks[0].r);
    printf("\n  %sstudy now:%s %s   %s%s%s\n", CBOLD, CRESET,
           ref_str(st, picks[0].r, rb, sizeof rb), CBOLD, ch->title, CRESET);
    printf("  %swhy:%s %s\n", CDIM, CRESET, picks[0].reason);
    if (ch->est_min > 0) {
        double left = ch->est_min - ch->minutes;
        if (left > 0) printf("  ~%.0f min left at your estimates\n", left);
    } else if (ch->est_pages > 0) {
        printf("  ~%.0f pages left\n", ch->est_pages - ch->pages);
    }
    for (int i = 1; i < n; i++) {
        printf("  %salso ready:%s %s   %s %s(%s)%s\n", CDIM, CRESET,
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
    printf("  logged %.0f min%s on %s   %s\n",
           min, pages > 0 ? "" : "", ref_str(st, r, rb, sizeof rb), ch->title);
    double prog = ch_progress(st, r);
    if (prog > 0 && prog < 1) {
        printf("  chapter: ");
        ui_bar(prog, 24);
        printf(" %.0f%%", prog * 100);
        if (prog >= 0.8)
            printf("  %s  nearly there, one push seals it%s", CGREEN, CRESET);
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
        fprintf(stderr,
                "usage: khatm study <chapter> [--pomo [25/5]]\n");
        return 1;
    }
    int pomo = 0, pw = 25, pb = 5;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pomo") == 0) {
            pomo = 1;
            if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0])) {
                int w, b;
                if (sscanf(argv[++i], "%d/%d", &w, &b) != 2 ||
                    w < 1 || w > 240 || b < 1 || b > 60) {
                    fprintf(stderr, "khatm: bad pomodoro spec \"%s\" "
                            "(try 25/5: focus/break minutes)\n", argv[i]);
                    return 1;
                }
                pw = w; pb = b;
            }
        } else {
            fprintf(stderr, "khatm: unknown option \"%s\"\n", argv[i]);
            return 1;
        }
    }
    Ref r;
    if (resolve_target(st, argv[0], &r, 0)) return 1;
    Chapter *ch = CH(st, r);
    char rb[64], buf[64];
    printf("  studying %s   %s\n", ref_str(st, r, rb, sizeof rb), ch->title);
    double min;
    if (pomo) {
        printf("  pomodoro %d/%d   Enter at any time ends the session.\n",
               pw, pb);
        double focus = 0;
        int nfoc = 1;
        for (;;) {
            printf("  ◌ focus %d   %d min\n", nfoc, pw);
            fflush(stdout);
            time_t t0 = time(NULL);
            int got = plat_wait_enter(pw * 60000);
            focus += difftime(time(NULL), t0) / 60.0;
            if (got) break;
            printf("\a  ☕ break   %d min (breaks are not logged)\n", pb);
            fflush(stdout);
            if (plat_wait_enter(pb * 60000)) break;
            printf("\a");
            nfoc++;
        }
        min = focus;
    } else {
        printf("  press Enter when you stop.\n");
        time_t start = time(NULL);
        (void)fgets(buf, sizeof buf, stdin);
        min = difftime(time(NULL), start) / 60.0;
    }
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
        int rc = ev_dropgoal(st, r);
        if (rc > 0) {
            if (g_json) api_err("no open goal on that target");
            else fprintf(stderr, "khatm: no open goal on %s\n",
                         ref_str(st, r, rb, sizeof rb));
            return 1;
        }
        if (rc) {
            if (g_json) api_err("could not write to the log");
            return 1;
        }
        if (g_json) return api_drop_json(st, r);
        printf("  goal on %s dropped   no judgment, it never counts "
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
        if (!any) printf("  no open goals   khatm goal <target> --by <when>\n");
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

    goals_refresh(st);
    double prereq = 0;
    double work = plan_goal_work(st, r, &prereq);
    int days = days_between(st->now, deadline);
    if (days < 1) days = 1;
    double need = work / days;
    double vel = velocity_pages(st, -1, 28);

    /* what the other open promises already claim per day */
    double committed = 0;
    for (int i = 0; i < st->ngoals; i++) {
        Goal *go = &st->goals[i];
        if (go->status != GOAL_OPEN) continue;
        int dd = days_between(st->now, go->by);
        committed += plan_goal_work(st, go->target, NULL)
                   / (dd < 1 ? 1 : dd);
    }
    int steep = vel > 0 && need > vel * 1.5;
    int overload = vel > 0 && committed > 0 && committed + need > vel * 1.3;

    char rb[64], dl[32];
    ref_str(st, r, rb, sizeof rb);
    fmt_date(deadline, dl, sizeof dl);

    /* the price, before the promise */
    if (!g_json) {
        printf("  the price of %s by %s (%d day%s):\n",
               rb, dl, days, days == 1 ? "" : "s");
        printf("  required pace: %s~%.0f pages/day%s", CBOLD, need, CRESET);
        if (prereq >= 0.5)
            printf(" (incl. ~%.0f p of unsealed prerequisites)", prereq);
        if (vel > 0)
            printf("  (your 4-week pace: %.*f/day   %s)",
                   vel < 10 ? 1 : 0, vel,
                   need <= vel ? "comfortably yours" :
                   need <= vel * 1.5 ? "a stretch" : "steep");
        printf("\n");
        if (overload)
            printf("  %s⚠ with your other promises that is %.0f pages/day "
                   "in total   above your pace.%s\n",
                   CYELLOW, committed + need, CRESET);
        if ((steep || overload) && plat_stdin_tty()) {
            printf("  promise anyway? [y/N] ");
            fflush(stdout);
            char ans[16] = "";
            if (!fgets(ans, sizeof ans, stdin)) ans[0] = 0;
            char *a = trim(ans);
            if (*a != 'y' && *a != 'Y') {
                printf("  no promise made   re-aim and come back.\n");
                return 0;
            }
        }
    }

    if (ev_goal(st, r, deadline)) {
        if (g_json) api_err("could not write to the log");
        return 1;
    }
    if (g_json) return api_goal_json(st, r, deadline);

    printf("  promised: %s by %s\n", rb, dl);
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
        printf("  %snote:%s prerequisites of %s are not all sealed   "
               "sealing anyway.\n", CYELLOW, CRESET,
               ref_str(st, r, rb, sizeof rb));
    }

    if (ev_done(st, r)) {
        if (g_json) api_err("could not write to the log");
        return 1;
    }
    if (g_json) return api_done_json(st, r, 0);

    Book *bk = &st->books[r.book];
    int all = 1;
    for (int c = 0; c < bk->nchs; c++)
        if (!bk->chs[c].done_at) { all = 0; break; }

    if (!quiet && cere_should_play())
        cere_play(st, r, all, 1);   /* the cinematic; the receipt below persists */

    ui_seal(st, r, quiet);

    char d[32];
    for (int g = 0; g < st->ngoals; g++) {
        Goal *go = &st->goals[g];
        if (go->status == GOAL_KEPT && go->resolved_at == st->now &&
            goal_covers(st, go, r)) {
            fmt_date(go->by, d, sizeof d);
            printf("   %s✓ goal kept%s   promised by %s\n",
                   CGREEN, CRESET, d);
        }
    }
    show_kept_rate(st);

    if (all) ui_khatma(st, r.book);
    return 0;
}

int cmd_edit(State *st, int argc, char **argv) {
    char path[1024];
    if (argc >= 1) {
        Ref r;
        if (resolve_target(st, argv[0], &r, 1)) return 1;
        snprintf(path, sizeof path, "%s", st->books[r.book].path);
    } else {
        snprintf(path, sizeof path, "%s/books", st->root);
    }
    /* the path is built from $KHATM_DIR/$HOME and an id-checked book id, but
     * guard the few chars that would break the quoted shell word anyway */
    if (strpbrk(path, "\"`$\n")) {
        fprintf(stderr, "khatm: refusing to edit a path with shell-unsafe "
                        "characters: %s\n", path);
        return 1;
    }
    const char *ed = getenv("VISUAL");
    if (!ed || !*ed) ed = getenv("EDITOR");
    if (!ed || !*ed)
#ifdef _WIN32
        ed = "notepad";
#else
        ed = "vi";
#endif
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "%s \"%s\"", ed, path);
    int rc = system(cmd);
    return rc == 0 ? 0 : 1;
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
        double cal = book_calib_pages(st, b);
        if (cal == 1.0) cal = book_calib_min(st, b);
        if (cal < 0.99 || cal > 1.01)
            printf("  %s(estimates calibrated ×%.2f from your sealed "
                   "chapters)%s\n", CDIM, cal, CRESET);
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
                    printf("  %s%d days late at this pace   hold %.0f p/day "
                           "to make it%s\n", CYELLOW, -slack,
                           need, CRESET);
                }
            }
        } else if (rem > 0) {
            printf("  no recent sessions   log one and pace appears.\n");
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

    printf("\n  %skhatm%s   ", CBOLD, CRESET);
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
    double vel = velocity_pages(st, -1, 28);
    double committed = 0;
    for (int g = 0; g < st->ngoals; g++) {
        Goal *go = &st->goals[g];
        if (go->status != GOAL_OPEN) continue;
        if (!any) printf("\n  %sopen promises:%s\n", CBOLD, CRESET);
        any = 1;
        double w = plan_goal_work(st, go->target, NULL);
        int left = days_between(st->now, go->by);
        int dd = left < 1 ? 1 : left;
        committed += w / dd;
        fmt_date(go->by, d, sizeof d);
        printf("   %-14s by %s   %s%dd left%s, ~%.0f pages to go "
               "(~%.1f/day)\n",
               ref_str(st, go->target, rb, sizeof rb), d,
               left <= 1 ? CRED : left <= 3 ? CYELLOW : CGREEN, left, CRESET,
               w, w / dd);
    }
    if (any && vel > 0)
        printf("   %sall promises: ~%.1f p/day · your pace %.1f   %s%s\n",
               committed <= vel ? CGREEN : CYELLOW, committed, vel,
               committed <= vel ? "on track" : "over-committed", CRESET);

    NextPick pick[1];
    if (plan_next(st, pick, 1) > 0) {
        printf("\n  %sstudy now:%s %s   %s %s(%s)%s\n", CBOLD, CRESET,
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
        printf("  %s!%s missed promise: %s by %s   re-promise "
               "(khatm goal) or drop it (khatm goal --drop)\n",
               CYELLOW, CRESET, ref_str(st, go->target, rb, sizeof rb), d);
        issues++;
    }

    double vel = velocity_pages(st, -1, 28);
    if (vel > 0)
        for (int g = 0; g < st->ngoals; g++) {
            Goal *go = &st->goals[g];
            if (go->status != GOAL_OPEN) continue;
            int dd = days_between(st->now, go->by);
            if (dd < 1) dd = 1;
            double need = plan_goal_work(st, go->target, NULL) / dd;
            if (need > vel * 2) {
                fmt_date(go->by, d, sizeof d);
                printf("  %s!%s at-risk promise: %s by %s needs "
                       "%.0f pages/day   your pace is %.1f\n",
                       CYELLOW, CRESET,
                       ref_str(st, go->target, rb, sizeof rb), d, need, vel);
                issues++;
            }
        }

    for (int b = 0; b < st->nbooks; b++) {
        Book *bk = &st->books[b];
        int n;
        double bias = book_bias_min(st, b, &n);
        if (bias > 1.25)
            printf("  %s~%s %s: you under-estimate time by ~%.0f%% "
                   "(across %d sealed chapters)\n", CCYAN, CRESET,
                   bk->id, (bias - 1) * 100, n);
        else if (bias > 0 && bias < 0.8)
            printf("  %s~%s %s: you over-estimate time by ~%.0f%%   "
                   "you are faster than you think\n", CCYAN, CRESET,
                   bk->id, (1 - bias) * 100);
    }

    for (int b = 0; b < st->nbooks; b++) {
        Book *bk = &st->books[b];
        if (!bk->deadline) {
            int undone = 0;
            for (int c = 0; c < bk->nchs; c++)
                if (!bk->chs[c].done_at) undone++;
            if (undone)
                printf("  %s·%s %s has no deadline   pace forecasts will "
                       "be weaker (meta: deadline=...)\n", CDIM, CRESET,
                       bk->id);
        }
    }

    if (!issues) printf("  %s✓%s no problems found\n", CGREEN, CRESET);
    printf("\n");
    return 0;
}
