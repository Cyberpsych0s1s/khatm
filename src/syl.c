#define _POSIX_C_SOURCE 200809L
#include "khatm.h"
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

static void extract_needs(char *line, char ***raw, int *n, int *cap) {
    char *open = NULL;
    for (char *p = line; *p; p++)
        if (*p == '[' && (strncmp(p + 1, "needs:", 6) == 0 ||
                          strncmp(p + 1, "needs ", 6) == 0))
            open = p;
    if (!open) return;
    char *close = strchr(open, ']');
    if (!close) return;
    *close = 0;
    char *list = open + 7;
    *open = 0;
    char *tok = strtok(list, ",");
    while (tok) {
        char *t = trim(tok);
        if (*t) *VPUSH(*raw, *n, *cap) = xstrdup(t);
        tok = strtok(NULL, ",");
    }
}

static void extract_estimate(char *line, double *pages, double *min) {
    char *t = strrchr(line, '~');
    if (!t) return;
    char *p = t + 1;
    double num = strtod(p, &p);
    if (p == t + 1 || num <= 0) return;
    while (*p == ' ') p++;
    if (*p == 'p') { *pages = num; }
    else if (*p == 'h') {
        *min = num * 60; p++;
        double m2 = strtod(p, &p);
        if (m2 > 0 && *p == 'm') *min += m2;
        else p--;
    }
    else if (*p == 'm') { *min = num; }
    else return;
    *t = 0;
}

static void parse_meta(Book *bk, char *line, time_t now) {
    char *tok = strtok(line, " \t");
    while (tok) {
        char *eq = strchr(tok, '=');
        if (eq) {
            *eq = 0;
            const char *val = eq + 1;
            if (ieq(tok, "deadline")) {
                time_t t = parse_when(val, now);
                if (t) bk->deadline = t;
            } else if (ieq(tok, "pages")) {
                bk->total_pages = strtod(val, NULL);
            } else if (ieq(tok, "id")) {
                free(bk->id);
                bk->id = xstrdup(val);
            }
        }
        tok = strtok(NULL, " \t");
    }
}

/* FNV-1a (32-bit): stable identity for a card from its front text. */
static unsigned fnv1a(const char *s) {
    unsigned h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

static int parse_book(State *st, const char *path, const char *stem) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    Book bk = {0};
    bk.id = xstrdup(stem);
    bk.path = xstrdup(path);
    struct stat sb;
    if (stat(path, &sb) == 0) bk.mtime = sb.st_mtime;

    char line[1024];
    int cur_sec = -1, cur_ch = -1;
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line);
        if (!*s) continue;

        if (s[0] == '#' && s[1] == '#') {
            Section sec = {0};
            char *t = trim(s + 2);
            extract_needs(t, &sec.needs_raw, &sec.nneeds_raw, &sec.cneeds_raw);
            sec.title = xstrdup(trim(t));
            *VPUSH(bk.secs, bk.nsecs, bk.csecs) = sec;
            cur_sec = bk.nsecs - 1;
        } else if (s[0] == '#') {
            if (!bk.title) bk.title = xstrdup(trim(s + 1));
        } else if (strncmp(s, "meta:", 5) == 0) {
            parse_meta(&bk, s + 5, st->now);
        } else if ((s[0] == '-' || s[0] == '*') && s[1] == ' ' && s[2] == '[') {
            Chapter ch = {0};
            ch.section = cur_sec;
            ch.done_in_file = (s[3] == 'x' || s[3] == 'X');
            char *t = strchr(s, ']');
            if (!t) continue;
            t = trim(t + 1);
            extract_needs(t, &ch.needs_raw, &ch.nneeds_raw, &ch.cneeds_raw);
            extract_estimate(t, &ch.est_pages, &ch.est_min);
            ch.title = xstrdup(trim(t));
            if (ch.done_in_file) ch.done_at = bk.mtime ? bk.mtime : 1;
            *VPUSH(bk.chs, bk.nchs, bk.cchs) = ch;
            cur_ch = bk.nchs - 1;
        } else if (s[0] == '?') {
            /* `? front :: back`   a review card under the current chapter */
            char *sep = strstr(s, "::");
            if (cur_ch < 0 || !sep) continue;
            *sep = 0;
            char *front = trim(s + 1), *back = trim(sep + 2);
            if (!*front || !*back) continue;
            Card cd = {0};
            cd.front = xstrdup(front);
            cd.back  = xstrdup(back);
            cd.id    = fnv1a(front);
            cd.ef    = 2.5;
            Chapter *cc = &bk.chs[cur_ch];
            *VPUSH(cc->cards, cc->ncards, cc->ccards) = cd;
        }
    }
    fclose(f);

    if (!bk.title) bk.title = xstrdup(stem);
    if (bk.nchs == 0) {
        free(bk.id); free(bk.title); free(bk.path);
        free(bk.secs); free(bk.chs);
        return 0;
    }
    *VPUSH(st->books, st->nbooks, st->cbooks) = bk;
    return 0;
}

int syl_load(State *st) {
    char dirpath[512];
    snprintf(dirpath, sizeof dirpath, "%s/books", st->root);
    DIR *d = opendir(dirpath);
    if (!d) return -1;

    char **names = NULL; int nn = 0, cn = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t len = strlen(e->d_name);
        if (len > 3 && strcmp(e->d_name + len - 3, ".md") == 0)
            *VPUSH(names, nn, cn) = xstrdup(e->d_name);
    }
    closedir(d);
    for (int i = 0; i < nn; i++)
        for (int j = i + 1; j < nn; j++)
            if (strcmp(names[j], names[i]) < 0) {
                char *t = names[i]; names[i] = names[j]; names[j] = t;
            }

    for (int i = 0; i < nn; i++) {
        char path[1024], stem[256];
        snprintf(path, sizeof path, "%s/%s", dirpath, names[i]);
        snprintf(stem, sizeof stem, "%.*s",
                 (int)(strlen(names[i]) - 3), names[i]);
        parse_book(st, path, stem);
        free(names[i]);
    }
    free(names);
    return 0;
}

static int find_book(State *st, const char *id) {
    for (int b = 0; b < st->nbooks; b++)
        if (ieq(st->books[b].id, id)) return b;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Syllabus writers. Append-only by design: khatm creates files and    */
/* adds lines, it never rewrites what the user wrote. Each writer also */
/* updates the in-memory state so the TUI sees the change live.        */
/* ------------------------------------------------------------------ */

static int id_ok(const char *id) {
    if (!*id) return 0;
    for (const char *p = id; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_')
            return 0;
    return strlen(id) < 64;
}

static int title_ok(const char *t) {
    if (!*t || strlen(t) > 200) return 0;
    /* these would change how the line parses back */
    return !strpbrk(t, "~[]\n") && t[0] != '#' && t[0] != '-';
}

/* "12p" | "90m" | "2h" | "1h30m"   the forms extract_estimate reads */
static int est_parse(const char *s, double *pages, double *min) {
    *pages = 0; *min = 0;
    if (!s || !*s) return 0;
    char *p;
    double num = strtod(s, &p);
    if (p == s || num <= 0) return -1;
    if (*p == 'p' && !p[1]) { *pages = num; return 0; }
    if (*p == 'm' && !p[1]) { *min = num; return 0; }
    if (*p == 'h') {
        *min = num * 60;
        if (!p[1]) return 0;
        double m2 = strtod(p + 1, &p);
        if (m2 > 0 && *p == 'm' && !p[1]) { *min += m2; return 0; }
    }
    return -1;
}

/* Append a line, inserting a newline first if the file doesn't end
 * with one (a hand-edited file may not). */
static int append_line(const char *path, const char *line) {
    int need_nl = 0;
    FILE *r = fopen(path, "rb");
    if (r) {
        if (fseek(r, -1L, SEEK_END) == 0) {
            int c = fgetc(r);
            need_nl = c != '\n' && c != EOF;
        }
        fclose(r);
    }
    FILE *f = fopen(path, "a");
    if (!f) return -1;
    fprintf(f, "%s%s\n", need_nl ? "\n" : "", line);
    fclose(f);
    return 0;
}

#define ERR(...) (snprintf(err, errn, __VA_ARGS__), -1)

int syl_book_new(State *st, const char *id, const char *title,
                 double pages, time_t deadline, char *err, size_t errn) {
    if (!id_ok(id))
        return ERR("book id must be letters, digits, - or _ (it becomes "
                   "the filename and the ref prefix)");
    if (!title_ok(title))
        return ERR("that title would not parse back cleanly "
                   "(avoid ~ [ ] and leading # or -)");
    if (find_book(st, id) >= 0)
        return ERR("book \"%s\" already exists", id);

    char path[1024];
    snprintf(path, sizeof path, "%s/books/%s.md", st->root, id);
    FILE *f = fopen(path, "r");
    if (f) { fclose(f); return ERR("%s already exists", path); }
    if (!(f = fopen(path, "w")))
        return ERR("cannot create %s", path);
    fprintf(f, "# %s\n", title);
    if (pages > 0 || deadline) {
        fputs("meta:", f);
        if (pages > 0) fprintf(f, " pages=%g", pages);
        if (deadline) {
            char d[32];
            fmt_date(deadline, d, sizeof d);
            fprintf(f, " deadline=%s", d);
        }
        fputc('\n', f);
    }
    fclose(f);

    Book bk = {0};
    bk.id = xstrdup(id);
    bk.title = xstrdup(title);
    bk.path = xstrdup(path);
    bk.deadline = deadline;
    bk.total_pages = pages;
    bk.mtime = st->now;
    *VPUSH(st->books, st->nbooks, st->cbooks) = bk;
    return 0;
}

/* Resolve id to a loaded book, or to a syllabus file that exists but
 * holds no chapters yet (parse_book skips those). *path_out always set
 * on success; returns the book index or -1 (file-only). -2 = no book. */
static int writable_book(State *st, const char *id,
                         char *path_out, size_t pn) {
    int b = find_book(st, id);
    if (b >= 0) {
        snprintf(path_out, pn, "%s", st->books[b].path);
        return b;
    }
    if (!id_ok(id)) return -2;
    snprintf(path_out, pn, "%s/books/%s.md", st->root, id);
    FILE *f = fopen(path_out, "r");
    if (!f) return -2;
    fclose(f);
    return -1;
}

int syl_book_add(State *st, const char *id, const char *title,
                 const char *est, const char *needs,
                 char *err, size_t errn) {
    char path[1024];
    int b = writable_book(st, id, path, sizeof path);
    if (b == -2)
        return ERR("no book \"%s\"   khatm book new %s \"Title\"", id, id);
    if (!title_ok(title))
        return ERR("that title would not parse back cleanly "
                   "(avoid ~ [ ] and leading # or -)");
    double ep = 0, em = 0;
    if (est && *est && est_parse(est, &ep, &em))
        return ERR("bad estimate \"%s\" (try 12p, 90m, 2h)", est);
    if (needs && strpbrk(needs, "[]\n"))
        return ERR("needs list cannot contain [ or ]");

    char line[512];
    int n = snprintf(line, sizeof line, "- [ ] %s", title);
    if (est && *est)
        n += snprintf(line + n, sizeof line - (size_t)n, " ~%s", est);
    if (needs && *needs)
        snprintf(line + n, sizeof line - (size_t)n, " [needs: %s]", needs);
    if (append_line(path, line))
        return ERR("cannot write to %s", path);

    if (b >= 0) {
        Book *bk = &st->books[b];
        Chapter ch = {0};
        ch.title = xstrdup(title);
        ch.section = bk->nsecs - 1;     /* appended → last section */
        ch.est_pages = ep;
        ch.est_min = em;
        if (needs && *needs) {
            char *list = xstrdup(needs);
            for (char *tok = strtok(list, ","); tok;
                 tok = strtok(NULL, ",")) {
                char *t = trim(tok);
                if (*t) *VPUSH(ch.needs_raw, ch.nneeds_raw,
                               ch.cneeds_raw) = xstrdup(t);
            }
            free(list);
        }
        *VPUSH(bk->chs, bk->nchs, bk->cchs) = ch;
        Chapter *pc = &bk->chs[bk->nchs - 1];
        for (int i = 0; i < pc->nneeds_raw; i++)
            syl_resolve_token(st, b, pc->needs_raw[i], pc);
    }
    return 0;
}

int syl_book_section(State *st, const char *id, const char *title,
                     const char *needs, char *err, size_t errn) {
    char path[1024];
    int b = writable_book(st, id, path, sizeof path);
    if (b == -2)
        return ERR("no book \"%s\"   khatm book new %s \"Title\"", id, id);
    if (!title_ok(title))
        return ERR("that title would not parse back cleanly "
                   "(avoid ~ [ ] and leading # or -)");
    if (needs && strpbrk(needs, "[]\n"))
        return ERR("needs list cannot contain [ or ]");

    char line[512];
    int n = snprintf(line, sizeof line, "## %s", title);
    if (needs && *needs)
        snprintf(line + n, sizeof line - (size_t)n, " [needs: %s]", needs);
    if (append_line(path, line))
        return ERR("cannot write to %s", path);

    if (b >= 0) {
        Section sec = {0};
        sec.title = xstrdup(title);
        if (needs && *needs) {
            char *list = xstrdup(needs);
            for (char *tok = strtok(list, ","); tok;
                 tok = strtok(NULL, ",")) {
                char *t = trim(tok);
                if (*t) *VPUSH(sec.needs_raw, sec.nneeds_raw,
                               sec.cneeds_raw) = xstrdup(t);
            }
            free(list);
        }
        *VPUSH(st->books[b].secs, st->books[b].nsecs,
               st->books[b].csecs) = sec;
    }
    return 0;
}

#undef ERR

static int resolve_local(State *st, int b, const char *tok, Chapter *ch,
                         int append) {
    Book *bk = &st->books[b];
    char *end;
    long n = strtol(tok, &end, 10);
    if (*tok && !*end && n >= 1 && n <= bk->nchs) {
        if (append) *VPUSH(ch->needs, ch->nneeds, ch->cneeds) =
            (Ref){ b, (int)n - 1 };
        return 1;
    }
    for (int s = 0; s < bk->nsecs; s++) {
        if (ieq(bk->secs[s].title, tok)) {
            int added = 0;
            for (int c = 0; c < bk->nchs; c++)
                if (bk->chs[c].section == s && &bk->chs[c] != ch) {
                    if (append) *VPUSH(ch->needs, ch->nneeds, ch->cneeds) =
                        (Ref){ b, c };
                    added++;
                }
            return added;
        }
    }
    for (int c = 0; c < bk->nchs; c++)
        if (&bk->chs[c] != ch && icontains(bk->chs[c].title, tok)) {
            if (append) *VPUSH(ch->needs, ch->nneeds, ch->cneeds) =
                (Ref){ b, c };
            return 1;
        }
    return 0;
}

static int resolve_token(State *st, int b, const char *tok, Chapter *ch,
                         int append) {
    const char *slash = strchr(tok, '/');
    if (slash) {
        char bookid[256];
        snprintf(bookid, sizeof bookid, "%.*s", (int)(slash - tok), tok);
        int ob = find_book(st, bookid);
        if (ob < 0) return -1;
        return resolve_local(st, ob, slash + 1, ch, append) > 0 ? 0 : -1;
    }
    return resolve_local(st, b, tok, ch, append) > 0 ? 0 : -1;
}

int syl_resolve_token(State *st, int b, const char *tok, Chapter *ch) {
    return resolve_token(st, b, tok, ch, 1);
}

int syl_need_resolves(State *st, int b, const char *tok, Chapter *ch) {
    return resolve_token(st, b, tok, ch, 0) == 0;
}

void syl_resolve_all(State *st, FILE *complain) {
    for (int b = 0; b < st->nbooks; b++) {
        Book *bk = &st->books[b];
        for (int c = 0; c < bk->nchs; c++) {
            Chapter *ch = &bk->chs[c];
            for (int i = 0; i < ch->nneeds_raw; i++)
                if (syl_resolve_token(st, b, ch->needs_raw[i], ch) && complain)
                    fprintf(complain,
                            "  %s/%d: unresolved needs \"%s\"\n",
                            bk->id, c + 1, ch->needs_raw[i]);
            if (ch->section >= 0) {
                Section *sec = &bk->secs[ch->section];
                int first_of_sec = (c == 0 || bk->chs[c - 1].section != ch->section);
                for (int i = 0; i < sec->nneeds_raw; i++)
                    if (syl_resolve_token(st, b, sec->needs_raw[i], ch) &&
                        complain && first_of_sec)
                        fprintf(complain,
                                "  %s section \"%s\": unresolved needs \"%s\"\n",
                                bk->id, sec->title, sec->needs_raw[i]);
            }
        }
    }
}
