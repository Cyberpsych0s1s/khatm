#define _POSIX_C_SOURCE 200809L
#include "khatm.h"
#include "plat.h"
#include <ctype.h>

/* Spaced review. Cards are parsed from the .md (syl.c) and scheduled by
 * folding `review` log events through SM-2. A card is only reviewable once
 * its chapter is sealed   you retain what you have finished reading. */

/* SM-2, lightly adapted. grade 0 again · 1 hard · 2 good · 3 easy. */
void srs_apply(Card *c, int grade, time_t when) {
    static const int qmap[4] = { 2, 3, 4, 5 };   /* -> SM-2 quality */
    if (grade < 0) grade = 0;
    if (grade > 3) grade = 3;
    int q = qmap[grade];
    if (c->ef < 1.3) c->ef = 2.5;

    if (q < 3) {                 /* lapse: back to the start */
        c->reps = 0;
        c->interval = 1;
    } else {
        if (c->reps == 0)       c->interval = 1;
        else if (c->reps == 1)  c->interval = 6;
        else                    c->interval = (int)(c->interval * c->ef + 0.5);
        if (c->interval < 1) c->interval = 1;
        c->reps++;
    }
    double ef = c->ef + (0.1 - (5 - q) * (0.08 + (5 - q) * 0.02));
    c->ef = ef < 1.3 ? 1.3 : ef;
    c->last_review = when;
    c->due = day_start(when) + (time_t)c->interval * 86400;
    c->seen = 1;
}

int card_due(State *st, Card *c) {
    return !c->seen || c->due <= st->now;
}

int cards_due_count(State *st, int book) {
    int n = 0;
    for (int b = 0; b < st->nbooks; b++) {
        if (book >= 0 && b != book) continue;
        Book *bk = &st->books[b];
        for (int c = 0; c < bk->nchs; c++) {
            if (!bk->chs[c].done_at) continue;     /* sealed chapters only */
            Chapter *ch = &bk->chs[c];
            for (int k = 0; k < ch->ncards; k++)
                if (card_due(st, &ch->cards[k])) n++;
        }
    }
    return n;
}

int ev_review(State *st, Ref chref, Card *c, int grade) {
    char ts[64], rb[256], line[512];
    fmt_ts(st->now, ts, sizeof ts);
    ref_str(st, chref, rb, sizeof rb);
    snprintf(line, sizeof line, "%s review %s card=%08x g=%d",
             ts, rb, c->id, grade);
    if (log_append(st, line)) return -1;
    srs_apply(c, grade, st->now);
    return 0;
}

/* ------------------------------------------------------------------ */

typedef struct DueCard { Ref ch; Card *c; } DueCard;

static int gather_due(State *st, int book, DueCard **out, int *cap) {
    int n = 0;
    for (int b = 0; b < st->nbooks; b++) {
        if (book >= 0 && b != book) continue;
        Book *bk = &st->books[b];
        for (int c = 0; c < bk->nchs; c++) {
            if (!bk->chs[c].done_at) continue;
            Chapter *ch = &bk->chs[c];
            for (int k = 0; k < ch->ncards; k++)
                if (card_due(st, &ch->cards[k])) {
                    DueCard *d = VPUSH(*out, n, *cap);
                    d->ch = (Ref){ b, c };
                    d->c  = &ch->cards[k];
                }
        }
    }
    return n;
}

static int grade_of(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    switch (tolower((unsigned char)*s)) {
    case 'a': case '1': return 0;
    case 'h': case '2': return 1;
    case 'e': case '4': return 3;
    default:            return 2;   /* good (incl. a bare Enter) */
    }
}

int cmd_cards(State *st, int argc, char **argv) {
    int book = -1;
    if (argc >= 1) {
        Ref r;
        if (resolve_target(st, argv[0], &r, 1)) return 1;
        book = r.book;
    }
    int total = 0, due = 0, locked = 0;
    char d[32];
    for (int b = 0; b < st->nbooks; b++) {
        if (book >= 0 && b != book) continue;
        Book *bk = &st->books[b];
        for (int c = 0; c < bk->nchs; c++) {
            Chapter *ch = &bk->chs[c];
            int sealed = ch->done_at != 0;
            for (int k = 0; k < ch->ncards; k++) {
                Card *cd = &ch->cards[k];
                total++;
                const char *tag, *col;
                if (!sealed)               { tag = "locked"; col = CDIM;    locked++; }
                else if (!cd->seen)        { tag = "new   "; col = CCYAN;   due++; }
                else if (cd->due <= st->now){ tag = "due   "; col = CYELLOW; due++; }
                else {
                    fmt_date(cd->due, d, sizeof d);
                    tag = d; col = CGREEN;
                }
                printf("  %s%s%s  %s/%d  %s\n", col, tag, CRESET,
                       bk->id, c + 1, cd->front);
            }
        }
    }
    if (!total) {
        printf("  no cards yet   add `? front :: back` lines under a chapter\n");
        return 0;
    }
    printf("\n  %d card%s   %s%d due%s, %d locked (chapter unsealed)\n",
           total, total == 1 ? "" : "s",
           due ? CYELLOW : "", due, due ? CRESET : "", locked);
    return 0;
}

int cmd_review(State *st, int argc, char **argv) {
    int book = -1;
    if (argc >= 1) {
        Ref r;
        if (resolve_target(st, argv[0], &r, 1)) return 1;
        book = r.book;
    }

    if (g_json) {
        printf("{\"ok\":true,\"due\":%d}\n", cards_due_count(st, book));
        return 0;
    }

    DueCard *due = NULL; int cap = 0;
    int n = gather_due(st, book, &due, &cap);
    if (!n) {
        printf("  nothing due to review");
        int locked = 0;
        for (int b = 0; b < st->nbooks; b++)
            for (int c = 0; c < st->books[b].nchs; c++)
                if (!st->books[b].chs[c].done_at)
                    locked += st->books[b].chs[c].ncards;
        if (locked)
            printf("   (%d card%s waiting on unsealed chapters)",
                   locked, locked == 1 ? "" : "s");
        printf("\n");
        free(due);
        return 0;
    }

    /* No tty to drive the reveal: list what is due and stop. */
    if (!plat_stdin_tty()) {
        printf("  %d card%s due   run `khatm review` in a terminal\n",
               n, n == 1 ? "" : "s");
        free(due);
        return 0;
    }

    printf("  %s%d card%s due%s   Enter reveals · grade "
           "[a]gain [h]ard [g]ood [e]asy · q quits\n",
           CBOLD, n, n == 1 ? "" : "s", CRESET);

    int done = 0, total = n, guard = n * 6 + 10;
    char buf[64];
    for (int i = 0; i < total; i++) {
        Card *cd = due[i].c;
        printf("\n  %s%s%s\n", CCYAN, cd->front, CRESET);
        printf("  %s[%d/%d]%s ", CDIM, i + 1, total, CRESET);
        fflush(stdout);
        if (!fgets(buf, sizeof buf, stdin)) break;
        if (buf[0] == 'q' || buf[0] == 'Q') break;
        printf("  %s→%s %s\n", CGREEN, CRESET, cd->back);
        printf("  grade: ");
        fflush(stdout);
        if (!fgets(buf, sizeof buf, stdin)) break;
        if (buf[0] == 'q' || buf[0] == 'Q') break;
        int g = grade_of(buf);
        Ref chref = due[i].ch;       /* capture before any realloc below */
        ev_review(st, chref, cd, g);
        done++;
        if (g == 0 && total < guard) {       /* again: see it again now */
            DueCard *d = VPUSH(due, total, cap);
            d->ch = chref; d->c = cd;
        }
    }
    free(due);

    int left = cards_due_count(st, book);
    printf("\n  reviewed %d   %d still due\n", done, left);
    return 0;
}
