#define _POSIX_C_SOURCE 200809L
#include "khatm.h"
#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#endif

const char *log_path(State *st, char *buf, size_t n) {
    if ((size_t)snprintf(buf, n, "%s/log.txt", st->root) >= n)
        return NULL;
    return buf;
}

int log_append(State *st, const char *line) {
    char path[512];
    const char *p = log_path(st, path, sizeof path);
    if (!p) {
        fprintf(stderr, "khatm: log path too long\n");
        return -1;
    }
    FILE *f = fopen(p, "a");
    if (!f) {
        fprintf(stderr, "khatm: cannot open %s for append\n", path);
        return -1;
    }
    int bad = fprintf(f, "%s\n", line) < 0;
    if (fclose(f) == EOF) bad = 1;
    if (bad) {
        fprintf(stderr, "khatm: write to %s failed\n", path);
        return -1;
    }
    return 0;
}

static int parse_ref(State *st, const char *tok, Ref *out) {
    char bookid[256];
    const char *slash = strchr(tok, '/');
    int chn = -1;
    if (slash) {
        snprintf(bookid, sizeof bookid, "%.*s", (int)(slash - tok), tok);
        chn = atoi(slash + 1);
    } else {
        snprintf(bookid, sizeof bookid, "%s", tok);
    }
    for (int b = 0; b < st->nbooks; b++) {
        if (!ieq(st->books[b].id, bookid)) continue;
        if (chn == -1) { *out = (Ref){ b, -1 }; return 0; }
        if (chn >= 1 && chn <= st->books[b].nchs) {
            *out = (Ref){ b, chn - 1 };
            return 0;
        }
        return -1;
    }
    return -1;
}

static double kv_num(const char *line, const char *key, double dflt) {
    size_t kl = strlen(key);
    for (const char *p = line; *p; p++)
        if (strncmp(p, key, kl) == 0 && p[kl] == '=' &&
            (p == line || p[-1] == ' '))
            return strtod(p + kl + 1, NULL);
    return dflt;
}

static const char *kv_str(const char *line, const char *key,
                          char *buf, size_t n) {
    size_t kl = strlen(key);
    for (const char *p = line; *p; p++)
        if (strncmp(p, key, kl) == 0 && p[kl] == '=' &&
            (p == line || p[-1] == ' ')) {
            const char *v = p + kl + 1, *e = v;
            while (*e && *e != ' ' && *e != '\n') e++;
            snprintf(buf, n, "%.*s", (int)(e - v), v);
            return buf;
        }
    return NULL;
}

int log_load(State *st) {
    char path[512];
    const char *p = log_path(st, path, sizeof path);
    if (!p) {
        fprintf(stderr, "khatm: log path too long\n");
        return -1;
    }
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    log_parse_stream(st, f);
    fclose(f);
    return 0;
}

/* Replay an open log stream into st (sessions/goals/cards). Exposed in
 * khatm.h for the fuzz harness; the caller owns f. */
void log_parse_stream(State *st, FILE *f) {
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line);
        if (!*s || *s == '#') continue;

        char tsbuf[64], ev[32], target[256];
        if (sscanf(s, "%63s %31s %255s", tsbuf, ev, target) < 3) continue;
        time_t at = parse_ts(tsbuf);
        if (!at) continue;

        Ref r;
        if (strcmp(ev, "session") == 0) {
            if (parse_ref(st, target, &r) || r.ch < 0) continue;
            Session ses = { at, r,
                            kv_num(s, "min", 0), kv_num(s, "pages", 0) };
            if (ses.minutes <= 0) continue;
            *VPUSH(st->sess, st->nsess, st->csess) = ses;
            Chapter *ch = &st->books[r.book].chs[r.ch];
            ch->minutes += ses.minutes;
            ch->pages   += ses.pages;
            if (at > ch->last_session) ch->last_session = at;
        } else if (strcmp(ev, "done") == 0) {
            if (parse_ref(st, target, &r) || r.ch < 0) continue;
            /* Last done line wins, mirroring ev_done's overwrite   replay
             * must land on the same state the live process had. A log
             * timestamp also always beats the syllabus-file placeholder
             * (done_at == 1 or == file mtime). */
            st->books[r.book].chs[r.ch].done_at = at;
        } else if (strcmp(ev, "goal") == 0) {
            if (parse_ref(st, target, &r)) continue;
            char by[64];
            if (!kv_str(s, "by", by, sizeof by)) continue;
            time_t deadline = parse_when(by, at);
            if (!deadline) continue;
            Goal g = { at, deadline, r, GOAL_OPEN, 0 };
            *VPUSH(st->goals, st->ngoals, st->cgoals) = g;
        } else if (strcmp(ev, "review") == 0) {
            if (parse_ref(st, target, &r) || r.ch < 0) continue;
            char cb[32];
            if (!kv_str(s, "card", cb, sizeof cb)) continue;
            unsigned id = (unsigned)strtoul(cb, NULL, 16);
            int grade = (int)kv_num(s, "g", -1);
            if (grade < 0) continue;
            /* chronological log => folding in read order replays SM-2 */
            Chapter *ch = &st->books[r.book].chs[r.ch];
            for (int k = 0; k < ch->ncards; k++)
                if (ch->cards[k].id == id) {
                    srs_apply(&ch->cards[k], grade, at);
                    break;
                }
        } else if (strcmp(ev, "drop-goal") == 0) {
            if (parse_ref(st, target, &r)) continue;
            /* set= identifies the exact goal by its set_at timestamp.
             * Lines written before set= existed fall back to the old
             * newest-match scan. */
            char setbuf[64];
            time_t set_at = 0;
            if (kv_str(s, "set", setbuf, sizeof setbuf))
                set_at = parse_ts(setbuf);
            for (int g = st->ngoals - 1; g >= 0; g--)
                if (st->goals[g].status == GOAL_OPEN &&
                    st->goals[g].target.book == r.book &&
                    st->goals[g].target.ch == r.ch &&
                    (!set_at || st->goals[g].set_at == set_at)) {
                    st->goals[g].status = GOAL_DROPPED;
                    break;
                }
        }
    }

    for (int i = 1; i < st->nsess; i++) {
        Session key = st->sess[i];
        int j = i - 1;
        while (j >= 0 && st->sess[j].at > key.at) {
            st->sess[j + 1] = st->sess[j];
            j--;
        }
        st->sess[j + 1] = key;
    }
}

const char *ref_str(State *st, Ref r, char *buf, size_t n) {
    if (r.ch < 0) snprintf(buf, n, "%s", st->books[r.book].id);
    else snprintf(buf, n, "%s/%d", st->books[r.book].id, r.ch + 1);
    return buf;
}

/* Run $KHATM_DIR/hooks/<event> if it exists and is executable, git-style.
 * Data is passed via the environment (KHATM_EVENT/ROOT/BOOK/REF/CHAPTER), so
 * there is no shell and nothing to escape. POSIX only; a no-op on Windows. */
#ifndef _WIN32
static void run_hook(State *st, const char *event, Ref r) {
    char path[768];
    if ((size_t)snprintf(path, sizeof path, "%s/hooks/%s", st->root, event)
        >= sizeof path) return;
    if (access(path, X_OK) != 0) return;

    char rb[256];
    setenv("KHATM_EVENT", event, 1);
    setenv("KHATM_ROOT", st->root, 1);
    setenv("KHATM_BOOK", st->books[r.book].id, 1);
    if (r.ch >= 0) {
        setenv("KHATM_REF", ref_str(st, r, rb, sizeof rb), 1);
        setenv("KHATM_CHAPTER", st->books[r.book].chs[r.ch].title, 1);
    } else {
        unsetenv("KHATM_REF");
        unsetenv("KHATM_CHAPTER");
    }
    pid_t pid = fork();
    if (pid == 0) { execl(path, event, (char *)NULL); _exit(127); }
    if (pid > 0) { int s; waitpid(pid, &s, 0); }
}
#else
static void run_hook(State *st, const char *event, Ref r) {
    (void)st; (void)event; (void)r;   /* hooks are a POSIX feature */
}
#endif

int ev_session(State *st, Ref r, double min, double pages, time_t when) {
    char ts[64], rb[256], line[512];
    fmt_ts(when, ts, sizeof ts);
    ref_str(st, r, rb, sizeof rb);
    if (pages > 0)
        snprintf(line, sizeof line, "%s session %s min=%g pages=%g",
                 ts, rb, min, pages);
    else
        snprintf(line, sizeof line, "%s session %s min=%g", ts, rb, min);
    if (log_append(st, line)) return -1;

    Session ses = { when, r, min, pages };
    *VPUSH(st->sess, st->nsess, st->csess) = ses;
    for (int i = st->nsess - 1;
         i > 0 && st->sess[i - 1].at > st->sess[i].at; i--) {
        Session t = st->sess[i - 1];
        st->sess[i - 1] = st->sess[i];
        st->sess[i] = t;
    }
    Chapter *ch = &st->books[r.book].chs[r.ch];
    ch->minutes += min;
    ch->pages += pages;
    if (when > ch->last_session) ch->last_session = when;
    return 0;
}

int ev_done(State *st, Ref r) {
    char ts[64], rb[256], line[512];
    fmt_ts(st->now, ts, sizeof ts);
    snprintf(line, sizeof line, "%s done %s",
             ts, ref_str(st, r, rb, sizeof rb));
    if (log_append(st, line)) return -1;
    st->books[r.book].chs[r.ch].done_at = st->now;
    goals_refresh(st);

    run_hook(st, "post-seal", r);
    Book *bk = &st->books[r.book];
    int all = 1;
    for (int c = 0; c < bk->nchs; c++)
        if (!bk->chs[c].done_at) { all = 0; break; }
    if (all) run_hook(st, "post-khatma", (Ref){ r.book, -1 });
    return 0;
}

int ev_goal(State *st, Ref r, time_t by) {
    char ts[64], rb[256], dl[32], line[512];
    fmt_ts(st->now, ts, sizeof ts);
    fmt_date(by, dl, sizeof dl);
    snprintf(line, sizeof line, "%s goal %s by=%s",
             ts, ref_str(st, r, rb, sizeof rb), dl);
    if (log_append(st, line)) return -1;
    Goal g = { st->now, by, r, GOAL_OPEN, 0 };
    *VPUSH(st->goals, st->ngoals, st->cgoals) = g;
    goals_refresh(st);
    run_hook(st, "post-goal", r);
    return 0;
}

int ev_dropgoal_g(State *st, int g) {
    Goal *go = &st->goals[g];
    char ts[64], setts[64], rb[256], line[512];
    fmt_ts(st->now, ts, sizeof ts);
    fmt_ts(go->set_at, setts, sizeof setts);
    snprintf(line, sizeof line, "%s drop-goal %s set=%s",
             ts, ref_str(st, go->target, rb, sizeof rb), setts);
    if (log_append(st, line)) return -1;
    go->status = GOAL_DROPPED;
    return 0;
}

int ev_dropgoal(State *st, Ref r) {
    for (int g = st->ngoals - 1; g >= 0; g--)
        if (st->goals[g].status == GOAL_OPEN &&
            st->goals[g].target.book == r.book &&
            st->goals[g].target.ch == r.ch)
            return ev_dropgoal_g(st, g);
    return 1;
}
