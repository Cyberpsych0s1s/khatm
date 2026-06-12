#define _POSIX_C_SOURCE 200809L
#include "khatm.h"

int g_color = 0;

const char *C(const char *code) { return g_color ? code : ""; }

void ui_bar(double frac, int width) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    int full = (int)(frac * width + 0.5);
    printf("%s", frac >= 1.0 ? CGREEN : CCYAN);
    for (int i = 0; i < width; i++)
        fputs(i < full ? "█" : (g_color ? "\033[2m·\033[22m" : "·"), stdout);
    printf("%s", CRESET);
}

static void center(const char *s, int width) {
    int len = 0;
    for (const char *p = s; *p; p++)
        if (((unsigned char)*p & 0xC0) != 0x80) len++;
    int pad = (width - len) / 2;
    if (pad < 0) pad = 0;
    printf("%*s%s%*s", pad, "", s, width - len - pad > 0 ? width - len - pad : 0, "");
}

void ui_seal(State *st, Ref r, int quiet) {
    Book *bk = &st->books[r.book];
    Chapter *ch = &bk->chs[r.ch];
    char title[64];
    snprintf(title, sizeof title, "%.60s", ch->title);

    if (!quiet) {
        printf("\n");
        printf("%s   ╔════════════════════════════════════╗%s\n", CYELLOW, CRESET);
        printf("%s   ║%s%s", CYELLOW, CRESET, CBOLD);
        center("✦  S E A L E D  ✦", 36);
        printf("%s%s║%s\n", CRESET, CYELLOW, CRESET);
        printf("%s   ║%s", CYELLOW, CRESET);
        center(title, 36);
        printf("%s║%s\n", CYELLOW, CRESET);
        printf("%s   ╚════════════════════════════════════╝%s\n", CYELLOW, CRESET);
    } else {
        printf("%s✦ sealed:%s %s/%d %s\n", CGREEN, CRESET,
               bk->id, r.ch + 1, ch->title);
    }

    if (ch->est_min > 0 && ch->minutes > 0) {
        double ratio = ch->minutes / ch->est_min;
        printf("   time: %.0f min vs ~%.0f estimated (%s%.0f%%%s)\n",
               ch->minutes, ch->est_min,
               ratio <= 1.0 ? CGREEN : CYELLOW, ratio * 100, CRESET);
    } else if (ch->minutes > 0) {
        printf("   time: %.0f min logged\n", ch->minutes);
    }
    if (ch->pages > 0)
        printf("   pages: %.0f\n", ch->pages);

    int unb = count_unblocks(st, r);
    if (unb > 0)
        printf("   %sunblocked %d chapter%s%s\n", CCYAN, unb,
               unb == 1 ? "" : "s", CRESET);

    int done = 0;
    for (int c = 0; c < bk->nchs; c++)
        if (bk->chs[c].done_at) done++;
    printf("   %s: %d/%d chapters  ", bk->title, done, bk->nchs);
    ui_bar(book_progress(st, r.book), 24);
    printf("\n");
}

void ui_khatma(State *st, int b) {
    Book *bk = &st->books[b];
    printf("\n%s%s", CBOLD, CGREEN);
    printf("   ════════════════ ✦ ✦ ✦ ════════════════\n");
    printf("                K H A T M A\n");
    printf("   ");
    center(bk->title, 40);
    printf("\n");
    printf("              the book is sealed\n");
    printf("   ════════════════ ✦ ✦ ✦ ════════════════%s\n", CRESET);

    double mins = 0, pages = 0;
    time_t first = 0, last = 0;
    for (int i = 0; i < st->nsess; i++) {
        if (st->sess[i].ch.book != b) continue;
        mins += st->sess[i].minutes;
        pages += st->sess[i].pages;
        if (!first || st->sess[i].at < first) first = st->sess[i].at;
        if (st->sess[i].at > last) last = st->sess[i].at;
    }
    if (mins > 0) {
        printf("   %.1f hours", mins / 60.0);
        if (pages > 0) printf(", %.0f pages", pages);
        if (first && last > first)
            printf(", over %d days", days_between(first, last) + 1);
        printf("\n");
    }
    printf("   it is on your shelf now   %skhatm shelf%s\n", CBOLD, CRESET);
}

void ui_shelf(State *st) {
    int shelved[256], n = 0;
    for (int b = 0; b < st->nbooks && n < 256; b++) {
        Book *bk = &st->books[b];
        int all = bk->nchs > 0;
        for (int c = 0; c < bk->nchs; c++)
            if (!bk->chs[c].done_at) { all = 0; break; }
        if (all) shelved[n++] = b;
    }

    printf("\n  %syour shelf%s   %d book%s sealed\n\n", CBOLD, CRESET,
           n, n == 1 ? "" : "s");
    if (!n) {
        printf("  %s(empty   every chapter of a book sealed puts its spine"
               " here, forever)%s\n\n", CDIM, CRESET);
        return;
    }

    static const char *spine_colors[] = {
        "\033[41;97m", "\033[44;97m", "\033[42;30m",
        "\033[45;97m", "\033[43;30m", "\033[46;30m",
    };
    int i = 0;
    while (i < n) {
        int row_cols = 0, start = i;
        while (i < n) {
            int w = (int)strlen(st->books[shelved[i]].title) + 4;
            if (w > 30) w = 30;
            if (row_cols + w > 70 && i > start) break;
            row_cols += w;
            i++;
        }
        printf("  ");
        for (int j = start; j < i; j++) {
            Book *bk = &st->books[shelved[j]];
            char t[27];
            snprintf(t, sizeof t, "%.26s", bk->title);
            printf("%s▐ %s ▌%s ", g_color ? spine_colors[j % 6] : "",
                   t, g_color ? "\033[0m" : "");
        }
        printf("\n  ");
        for (int k = 0; k < row_cols + 2; k++) fputs("═", stdout);
        printf("\n");
    }
    printf("\n");
}

void ui_heatmap(State *st, int weeks) {
    double day_min[400] = {0};
    long today = day_start(st->now) / 86400;
    int span = weeks * 7;
    if (span > 399) span = 399;

    for (int i = 0; i < st->nsess; i++) {
        long k = day_start(st->sess[i].at) / 86400;
        long off = today - k;
        if (off >= 0 && off < span)
            day_min[span - 1 - off] += st->sess[i].minutes;
    }

    struct tm tm = *localtime(&st->now);
    int today_wd = (tm.tm_wday + 6) % 7;

    static const char *rows[7] =
        { "mon", "tue", "wed", "thu", "fri", "sat", "sun" };
    printf("\n");
    for (int wd = 0; wd < 7; wd++) {
        printf("  %s%s%s ", CDIM, rows[wd], CRESET);
        for (int w = 0; w < weeks; w++) {
            int back = (weeks - 1 - w) * 7 + (today_wd - wd);
            if (back < 0 || back >= span) { printf("  "); continue; }
            double m = day_min[span - 1 - back];
            const char *cell, *col = "";
            if (m <= 0)        { cell = "· "; col = CDIM; }
            else if (m < 15)   { cell = "░░"; col = C("\033[32;2m"); }
            else if (m < 45)   { cell = "▒▒"; col = CGREEN; }
            else if (m < 90)   { cell = "▓▓"; col = CGREEN; }
            else               { cell = "██"; col = C("\033[92m"); }
            printf("%s%s%s", col, cell, CRESET);
        }
        printf("\n");
    }
    printf("  %sless · ░ ▒ ▓ █ more (minutes/day, last %d weeks)%s\n\n",
           CDIM, weeks, CRESET);
}
