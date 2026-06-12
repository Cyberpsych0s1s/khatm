#include "khatm.h"
#include "plat.h"
#include <stdarg.h>

enum {
    P_GOLD = 178, P_GOLDHI = 214,
    P_INK  = 252, P_MUT = 245, P_FAINT = 239,
    P_TEAL = 73,  P_SAGE = 108, P_ROSE = 167, P_AMBER = 179,
    P_SKY  = 110, P_SELBG = 236, P_BARBG = 234,
};

static const char *fg(int n) {
    static char b[10][24]; static int i;
    if (!g_color) return "";
    i = (i + 1) % 10;
    snprintf(b[i], sizeof b[i], "\033[38;5;%dm", n);
    return b[i];
}
static const char *bgc(int n) {
    static char b[4][24]; static int i;
    if (!g_color) return "";
    i = (i + 1) & 3;
    snprintf(b[i], sizeof b[i], "\033[48;5;%dm", n);
    return b[i];
}
#define TR    (g_color ? "\033[0m" : "")
#define TB    (g_color ? "\033[1m" : "")
#define TDIM  (g_color ? "\033[2m" : "")

static char   ob[1 << 17];
static size_t olen;

static void op(const char *fmt, ...) {
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof b) n = sizeof b - 1;
    if (olen + (size_t)n < sizeof ob) {
        memcpy(ob + olen, b, (size_t)n);
        olen += (size_t)n;
    }
}
static void omove(int r, int c) { op("\033[%d;%dH", r, c); }
static void oflush(void) {
    if (olen) { plat_out(ob, olen); olen = 0; }
}

static int dlen(const char *s) {
    int n = 0;
    while (*s) {
        if (*s == '\033') {
            while (*s && *s != 'm') s++;
            if (*s) s++;
            continue;
        }
        if (((unsigned char)*s & 0xC0) != 0x80) n++;
        s++;
    }
    return n;
}

static const char *fit(const char *s, int w) {
    static char b[8][256]; static int bi;
    bi = (bi + 1) & 7;
    char *d = b[bi];
    int cw = 0; size_t o = 0;
    for (; *s && o < sizeof b[0] - 4; s++) {
        if (((unsigned char)*s & 0xC0) != 0x80) {
            if (cw == w) break;
            cw++;
        }
        d[o++] = *s;
    }
    d[o] = 0;
    return d;
}

static const char *sbar(double frac, int cells, int color) {
    static char b[4][512]; static int bi;
    bi = (bi + 1) & 3;
    char *d = b[bi]; size_t o = 0;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    int full = (int)(frac * cells + 0.5);
    o += (size_t)snprintf(d + o, 64, "%s", fg(frac >= 1.0 ? P_GOLD : color));
    for (int i = 0; i < cells && o < 440; i++)
        o += (size_t)snprintf(d + o, 8, "%s", i < full ? "█" : "░");
    snprintf(d + o, 64, "%s", TR);
    return d;
}

enum { V_HOME, V_BOOKS, V_CHAPTERS, V_GOALS, V_STATS, V_STUDY };

typedef struct UI {
    State *st;
    int w, h;
    int view;
    int sel_book, sel_ch, sel_goal, scroll;
    Ref suggestion; int has_suggestion;
    Ref study; time_t study_start;
    char toast[200]; time_t toast_until; int toast_color;
    int quit;
} UI;

static Chapter *CH(State *st, Ref r) { return &st->books[r.book].chs[r.ch]; }

static void toastf(UI *ui, int color, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ui->toast, sizeof ui->toast, fmt, ap);
    va_end(ap);
    ui->toast_color = color;
    ui->toast_until = time(NULL) + 4;
}

static void d_header(UI *ui) {
    State *st = ui->st;
    char left[64], right[160], d[32];
    snprintf(left, sizeof left, " %s▌%s%s khatm%s %s" KHATM_AR "%s",
             fg(P_GOLDHI), TR, TB, TR, fg(P_GOLD), TR);
    fmt_date(st->now, d, sizeof d);
    int k, m;
    double rate = kept_rate(st, &k, &m, NULL);
    int s = streak_current(st);
    double today = 0;
    long tk = day_start(st->now) / 86400;
    for (int i = 0; i < st->nsess; i++)
        if (day_start(st->sess[i].at) / 86400 == tk)
            today += st->sess[i].minutes;
    size_t o = 0;
    if (s > 0)
        o += (size_t)snprintf(right + o, sizeof right - o, "streak %dd · ", s);
    if (rate >= 0)
        o += (size_t)snprintf(right + o, sizeof right - o,
                              "word %.0f%% · ", rate * 100);
    if (today > 0)
        o += (size_t)snprintf(right + o, sizeof right - o,
                              "today %.0fm · ", today);
    snprintf(right + o, sizeof right - o, "%s ", d);

    omove(1, 1);
    int pad = ui->w - dlen(left) - dlen(right);
    if (pad < 1) pad = 1;
    op("%s%s%s%*s%s%s%s\033[K", bgc(P_BARBG), left, fg(P_MUT),
       pad, "", right, TR, "");
}

static void d_tabs(UI *ui) {
    static const char *names[4] = { "home", "books", "goals", "stats" };
    int cur = ui->view == V_CHAPTERS ? 1 :
              ui->view == V_STUDY ? -1 :
              ui->view == V_HOME ? 0 :
              ui->view == V_BOOKS ? 1 :
              ui->view == V_GOALS ? 2 : 3;
    omove(3, 1);
    op("  ");
    for (int i = 0; i < 4; i++) {
        if (i == cur)
            op("%s%s%d %s%s   ", TB, fg(P_GOLDHI), i + 1, names[i], TR);
        else
            op("%s%d %s%s   ", fg(P_FAINT), i + 1, names[i], TR);
    }
    if (ui->view == V_STUDY)
        op("%s· studying%s", fg(P_TEAL), TR);
    op("\033[K");
    omove(4, 1);
    op("%s", fg(P_FAINT));
    for (int i = 0; i < ui->w; i++) op("─");
    op("%s\033[K", TR);
}

static void d_footer(UI *ui, const char *hints) {
    if (ui->toast[0] && time(NULL) <= ui->toast_until) {
        omove(ui->h - 1, 1);
        op("  %s%s%s\033[K", fg(ui->toast_color), ui->toast, TR);
    } else {
        omove(ui->h - 1, 1);
        op("\033[K");
    }
    omove(ui->h, 1);
    op("  %s%s%s\033[K", fg(P_FAINT), fit(hints, ui->w - 4), TR);
}

static void d_heat(UI *ui, int row, int col, int weeks) {
    State *st = ui->st;
    double dm[400] = {0};
    long today = day_start(st->now) / 86400;
    int span = weeks * 7;
    if (span > 399) span = 399;
    for (int i = 0; i < st->nsess; i++) {
        long off = today - day_start(st->sess[i].at) / 86400;
        if (off >= 0 && off < span) dm[span - 1 - off] += st->sess[i].minutes;
    }
    struct tm tm = *localtime(&st->now);
    int twd = (tm.tm_wday + 6) % 7;
    static const char *rows[7] = { "mon","tue","wed","thu","fri","sat","sun" };
    for (int wd = 0; wd < 7; wd++) {
        omove(row + wd, col);
        op("%s%s%s ", fg(P_FAINT), rows[wd], TR);
        for (int w = 0; w < weeks; w++) {
            int back = (weeks - 1 - w) * 7 + (twd - wd);
            if (back < 0 || back >= span) { op("  "); continue; }
            double m = dm[span - 1 - back];
            if (m <= 0)      op("%s· %s", fg(P_FAINT), TR);
            else if (m < 15) op("%s░░%s", fg(65), TR);
            else if (m < 45) op("%s▒▒%s", fg(P_SAGE), TR);
            else if (m < 90) op("%s▓▓%s", fg(71), TR);
            else             op("%s██%s", fg(P_GOLD), TR);
        }
        op("\033[K");
    }
}

static int open_goal_idx(State *st, int *out, int max) {
    int n = 0;
    for (int g = 0; g < st->ngoals && n < max; g++)
        if (st->goals[g].status == GOAL_OPEN) out[n++] = g;
    return n;
}

static void d_home(UI *ui) {
    State *st = ui->st;
    int row = 6;
    char rb[64], d[32];

    NextPick pick[1];
    ui->has_suggestion = plan_next(st, pick, 1) > 0;
    omove(row++, 1);
    op("   %s%sstudy now%s\033[K", TB, fg(P_MUT), TR);
    if (ui->has_suggestion) {
        ui->suggestion = pick[0].r;
        Chapter *ch = CH(st, ui->suggestion);
        omove(row++, 1);
        op("   %s▌%s %s%s%s — %s%s\033[K", fg(P_GOLDHI), TR, TB,
           ref_str(st, ui->suggestion, rb, sizeof rb), TR,
           fit(ch->title, ui->w - 20), "");
        omove(row++, 1);
        op("   %s▌%s %s%s%s\033[K", fg(P_GOLDHI), TR, fg(P_MUT),
           fit(pick[0].reason, ui->w - 8), TR);
        omove(row++, 1);
        double prog = ch_progress(st, ui->suggestion);
        if (prog >= 0.99)
            op("   %s▌%s %s %s%s[d] seal it%s  %s· [enter] study more%s\033[K",
               fg(P_GOLDHI), TR, sbar(prog, 18, P_TEAL),
               TB, fg(P_GOLD), TR, fg(P_FAINT), TR);
        else if (prog > 0)
            op("   %s▌%s %s %.0f%%  %s[enter] start%s\033[K",
               fg(P_GOLDHI), TR, sbar(prog, 18, P_TEAL), prog * 100,
               fg(P_FAINT), TR);
        else
            op("   %s▌%s %s[enter] start%s\033[K", fg(P_GOLDHI), TR,
               fg(P_FAINT), TR);
    } else if (!st->nbooks) {
        omove(row++, 1);
        op("   no books yet — add a syllabus at %s/books/<id>.md\033[K",
           st->root);
        row++;
    } else {
        omove(row++, 1);
        op("   all chapters sealed or blocked — see 4 stats, or doctor "
           "in the CLI\033[K");
        row++;
    }
    row++;

    int idx[32];
    int n = open_goal_idx(st, idx, 32);
    omove(row++, 1);
    op("   %s%spromises%s\033[K", TB, fg(P_MUT), TR);
    if (!n) {
        omove(row++, 1);
        op("   %snone open — press g on a chapter to give your word%s\033[K",
           fg(P_FAINT), TR);
    }
    for (int i = 0; i < n && row < ui->h - 11; i++) {
        Goal *go = &st->goals[idx[i]];
        int left = days_between(st->now, go->by);
        fmt_date(go->by, d, sizeof d);
        double work = go->target.ch >= 0
            ? ch_weight(st, go->target) * (1 - ch_progress(st, go->target))
            : book_remaining_pages(st, go->target.book);
        int dd = left < 1 ? 1 : left;
        omove(row++, 1);
        op("   %-12s %sby %s%s  %s%2dd left%s   ~%.0f p/day to keep it\033[K",
           ref_str(st, go->target, rb, sizeof rb),
           fg(P_MUT), d, TR,
           fg(left <= 1 ? P_ROSE : left <= 3 ? P_AMBER : P_SAGE), left, TR,
           work / dd);
    }
    row++;

    int weeks = (ui->w - 12) / 2;
    if (weeks > 16) weeks = 16;
    if (row + 8 < ui->h - 1 && weeks > 4) {
        omove(row++, 1);
        op("   %s%slast %d weeks%s\033[K", TB, fg(P_MUT), weeks, TR);
        d_heat(ui, row, 4, weeks);
    }
}

static void d_books(UI *ui) {
    State *st = ui->st;
    int row = 6;
    if (ui->sel_book >= st->nbooks) ui->sel_book = st->nbooks - 1;
    if (ui->sel_book < 0) ui->sel_book = 0;
    if (!st->nbooks) {
        omove(row, 1);
        op("   no books — add a syllabus at %s/books/<id>.md\033[K", st->root);
        return;
    }
    for (int b = 0; b < st->nbooks && row < ui->h - 2; b++) {
        Book *bk = &st->books[b];
        int done = 0;
        for (int c = 0; c < bk->nchs; c++)
            if (bk->chs[c].done_at) done++;
        int sel = b == ui->sel_book;
        omove(row++, 1);
        op(" %s%s%s ", fg(P_GOLDHI), sel ? "▌" : " ", TR);
        if (sel) op("%s%s", bgc(P_SELBG), TB);
        int tw = ui->w - 48;
        if (tw < 10) tw = 10;
        op("%-8s %-*s %s %2d/%-2d",
           bk->id, tw, fit(bk->title, tw),
           sbar(book_progress(st, b), 14, P_TEAL), done, bk->nchs);
        if (bk->deadline) {
            char d[32];
            fmt_date(bk->deadline, d, sizeof d);
            int left = days_between(st->now, bk->deadline);
            op("  %sdue %s · %dd%s",
               fg(left < 7 ? P_AMBER : P_MUT), d, left, TR);
        }
        op("%s\033[K", TR);
    }
}

static void d_chapters(UI *ui) {
    State *st = ui->st;
    Book *bk = &st->books[ui->sel_book];
    char rb[64];
    if (ui->sel_ch >= bk->nchs) ui->sel_ch = bk->nchs - 1;
    if (ui->sel_ch < 0) ui->sel_ch = 0;

    int done = 0;
    for (int c = 0; c < bk->nchs; c++)
        if (bk->chs[c].done_at) done++;
    omove(6, 1);
    op("   %s‹%s %s%s%s   %s%d/%d sealed · ~%.0fp left%s\033[K",
       fg(P_FAINT), TR, TB, fit(bk->title, ui->w - 40), TR,
       fg(P_MUT), done, bk->nchs, book_remaining_pages(st, ui->sel_book), TR);

    int top = 8, vis = ui->h - 2 - top;
    if (vis < 3) vis = 3;
    if (ui->sel_ch < ui->scroll) ui->scroll = ui->sel_ch;
    if (ui->sel_ch >= ui->scroll + vis) ui->scroll = ui->sel_ch - vis + 1;
    if (ui->scroll < 0) ui->scroll = 0;

    for (int i = 0; i < vis; i++) {
        int c = ui->scroll + i;
        if (c >= bk->nchs) break;
        Chapter *ch = &bk->chs[c];
        Ref r = { ui->sel_book, c };
        int sel = c == ui->sel_ch;
        omove(top + i, 1);
        op(" %s%s%s ", fg(P_GOLDHI), sel ? "▌" : " ", TR);
        if (sel) op("%s", bgc(P_SELBG));

        const char *glyph, *gcol;
        if (ch->done_at)            { glyph = "✦"; gcol = fg(P_GOLD); }
        else if (!deps_met(st, r))  { glyph = "⊘"; gcol = fg(P_FAINT); }
        else if (ch->minutes > 0)   { glyph = "◐"; gcol = fg(P_TEAL); }
        else                        { glyph = "○"; gcol = fg(P_MUT); }
        op("%s%s%s %s%2d%s  ", gcol, glyph, TR, sel ? TB : fg(P_MUT),
           c + 1, TR);

        int tw = ui->w - 38;
        if (tw < 12) tw = 12;
        op("%s%-*s%s ", ch->done_at ? TDIM : sel ? TB : "",
           tw, fit(ch->title, tw), TR);

        if (ch->est_pages > 0)      op("%s%4.0fp%s ", fg(P_MUT), ch->est_pages, TR);
        else if (ch->est_min > 0)   op("%s%4.0fm%s ", fg(P_MUT), ch->est_min, TR);
        else                        op("      ");

        if (ch->done_at) {
            op("%ssealed%s", fg(P_GOLD), TR);
        } else if (!deps_met(st, r)) {
            for (int k = 0; k < ch->nneeds; k++)
                if (!CH(st, ch->needs[k])->done_at) {
                    op("%sneeds %s%s", fg(P_FAINT),
                       ref_str(st, ch->needs[k], rb, sizeof rb), TR);
                    break;
                }
        } else if (ch->minutes > 0) {
            double p = ch_progress(st, r);
            op("%s %.0f%%", sbar(p, 8, P_TEAL), p * 100);
        }
        op("%s\033[K", TR);
    }
}

static void d_goals(UI *ui) {
    State *st = ui->st;
    int row = 6;
    char rb[64], d[32];
    int idx[64];
    int n = open_goal_idx(st, idx, 64);
    if (ui->sel_goal >= n) ui->sel_goal = n - 1;
    if (ui->sel_goal < 0) ui->sel_goal = 0;

    int k, m;
    double rate = kept_rate(st, &k, &m, NULL);
    omove(row++, 1);
    op("   %s%sopen promises%s", TB, fg(P_MUT), TR);
    if (rate >= 0)
        op("   %syour word, kept: %s%.0f%%%s of %d%s",
           fg(P_MUT), fg(rate >= 0.8 ? P_SAGE : rate >= 0.5 ? P_AMBER : P_ROSE),
           rate * 100, fg(P_MUT), k + m, TR);
    op("\033[K");
    row++;

    if (!n) {
        omove(row++, 1);
        op("   %snone — press g on a chapter or book to give your "
           "word%s\033[K", fg(P_FAINT), TR);
    }
    for (int i = 0; i < n && row < ui->h - 8; i++) {
        Goal *go = &st->goals[idx[i]];
        int left = days_between(st->now, go->by);
        fmt_date(go->by, d, sizeof d);
        double work = go->target.ch >= 0
            ? ch_weight(st, go->target) * (1 - ch_progress(st, go->target))
            : book_remaining_pages(st, go->target.book);
        int sel = i == ui->sel_goal;
        omove(row++, 1);
        op(" %s%s%s ", fg(P_GOLDHI), sel ? "▌" : " ", TR);
        if (sel) op("%s", bgc(P_SELBG));
        const char *title = go->target.ch >= 0
            ? CH(st, go->target)->title : st->books[go->target.book].title;
        op("%-10s %-*s %sby %s%s  %s%2dd%s  ~%.0f p/day%s\033[K",
           ref_str(st, go->target, rb, sizeof rb),
           ui->w > 90 ? 34 : 20, fit(title, ui->w > 90 ? 34 : 20),
           fg(P_MUT), d, TR,
           fg(left <= 1 ? P_ROSE : left <= 3 ? P_AMBER : P_SAGE), left, TR,
           work / (left < 1 ? 1 : left), TR);
    }
    row++;

    omove(row++, 1);
    op("   %s%srecent outcomes%s\033[K", TB, fg(P_MUT), TR);
    int shown = 0;
    for (int g = st->ngoals - 1; g >= 0 && shown < 6 && row < ui->h - 2; g--) {
        Goal *go = &st->goals[g];
        if (go->status != GOAL_KEPT && go->status != GOAL_MISSED) continue;
        fmt_date(go->by, d, sizeof d);
        omove(row++, 1);
        if (go->status == GOAL_KEPT)
            op("   %s✓ kept%s   %-10s by %s\033[K", fg(P_SAGE), TR,
               ref_str(st, go->target, rb, sizeof rb), d);
        else
            op("   %s✗ missed%s %-10s by %s\033[K", fg(P_ROSE), TR,
               ref_str(st, go->target, rb, sizeof rb), d);
        shown++;
    }
    if (!shown) {
        omove(row++, 1);
        op("   %snothing resolved yet%s\033[K", fg(P_FAINT), TR);
    }
}

static void d_stats(UI *ui) {
    State *st = ui->st;
    int row = 6;
    double total_min = 0;
    long first = 0;
    for (int i = 0; i < st->nsess; i++) {
        total_min += st->sess[i].minutes;
        long k = day_start(st->sess[i].at) / 86400;
        if (!first || k < first) first = k;
    }
    int sealed_books = 0, sealed_chs = 0, total_chs = 0;
    for (int b = 0; b < st->nbooks; b++) {
        Book *bk = &st->books[b];
        int all = bk->nchs > 0;
        for (int c = 0; c < bk->nchs; c++) {
            total_chs++;
            if (bk->chs[c].done_at) sealed_chs++;
            else all = 0;
        }
        if (all) sealed_books++;
    }

    omove(row++, 1);
    op("   %s%srecords%s\033[K", TB, fg(P_MUT), TR);
    omove(row++, 1);
    op("   streak %s%dd%s now · %s%dd%s best     "
       "biggest week %s%.1fh%s     total %s%.1fh%s",
       fg(P_SAGE), streak_current(st), TR,
       fg(P_GOLD), streak_longest(st), TR,
       fg(P_GOLD), biggest_week_min(st) / 60.0, TR,
       fg(P_TEAL), total_min / 60.0, TR);
    op("\033[K");
    omove(row++, 1);
    op("   chapters sealed %s%d/%d%s · books %s%d%s\033[K",
       fg(P_GOLD), sealed_chs, total_chs, TR, fg(P_GOLD), sealed_books, TR);
    row++;

    omove(row++, 1);
    op("   %s%sshelf%s\033[K", TB, fg(P_MUT), TR);
    if (!sealed_books) {
        omove(row++, 1);
        op("   %sempty — a fully sealed book lives here forever%s\033[K",
           fg(P_FAINT), TR);
    } else {
        static const int spinec[6] = { 131, 67, 65, 96, 137, 73 };
        omove(row, 3);
        int colw = 3, si = 0;
        for (int b = 0; b < st->nbooks; b++) {
            Book *bk = &st->books[b];
            int all = bk->nchs > 0;
            for (int c = 0; c < bk->nchs; c++)
                if (!bk->chs[c].done_at) { all = 0; break; }
            if (!all) continue;
            char t[32];
            snprintf(t, sizeof t, "%.28s", bk->title);
            int wlen = dlen(t) + 4;
            if (colw + wlen > ui->w - 4) { row += 1; omove(row, 3); colw = 3; }
            if (g_color)
                op("\033[48;5;%dm\033[38;5;230m %s \033[0m ", spinec[si % 6], t);
            else
                op("▐%s▌ ", t);
            colw += wlen;
            si++;
        }
        op("\033[K");
        row += 2;
    }
    row++;

    int weeks = (ui->w - 12) / 2;
    if (weeks > 26) weeks = 26;
    if (row + 8 <= ui->h - 1 && weeks > 4) {
        omove(row++, 1);
        op("   %s%slast %d weeks%s\033[K", TB, fg(P_MUT), weeks, TR);
        d_heat(ui, row, 4, weeks);
        row += 7;
        omove(row, 4);
        op("%sless · ░ ▒ ▓ █ more%s\033[K", fg(P_FAINT), TR);
    }
}

static void d_bigtime(UI *ui, int row, const char *txt) {
    static const unsigned char F[11][5] = {
        {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
        {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},
        {0,2,0,2,0},
    };
    int n = (int)strlen(txt);
    int width = n * 8 - 2;
    int left = (ui->w - width) / 2;
    if (left < 1) left = 1;
    for (int r = 0; r < 5; r++) {
        omove(row + r, left);
        op("%s", fg(P_GOLDHI));
        for (int i = 0; i < n; i++) {
            int gi = txt[i] == ':' ? 10 : txt[i] - '0';
            if (gi < 0 || gi > 10) gi = 10;
            for (int bit = 2; bit >= 0; bit--)
                op("%s", (F[gi][r] >> bit) & 1 ? "██" : "  ");
            op("  ");
        }
        op("%s\033[K", TR);
    }
}

static void d_study(UI *ui) {
    State *st = ui->st;
    Chapter *ch = CH(st, ui->study);
    char rb[64];
    int elapsed = (int)difftime(time(NULL), ui->study_start);
    int mm = elapsed / 60, ss = elapsed % 60;
    char t[16];
    if (mm > 99) snprintf(t, sizeof t, "%d:%02d", mm / 60, mm % 60);
    else snprintf(t, sizeof t, "%02d:%02d", mm, ss);

    int row = ui->h / 2 - 6;
    if (row < 6) row = 6;
    char line[256];
    snprintf(line, sizeof line, "%s — %s",
             ref_str(st, ui->study, rb, sizeof rb), ch->title);
    omove(row, 1);
    int pad = (ui->w - dlen(line)) / 2;
    if (pad < 1) pad = 1;
    op("%*s%s%s%s\033[K", pad, "", TB, fit(line, ui->w - 2), TR);

    d_bigtime(ui, row + 2, t);

    omove(row + 8, 1);
    double prog = ch_progress(st, ui->study);
    char info[160];
    if (prog > 0 && prog < 1)
        snprintf(info, sizeof info, "chapter at %.0f%% before this session",
                 prog * 100);
    else
        snprintf(info, sizeof info, "first session on this chapter");
    pad = (ui->w - dlen(info)) / 2;
    if (pad < 1) pad = 1;
    op("%*s%s%s%s\033[K", pad, "", fg(P_MUT), info, TR);
}

static void draw_frame(UI *ui) {
    plat_winsize(&ui->w, &ui->h);
    ui->st->now = time(NULL);
    goals_refresh(ui->st);
    olen = 0;
    op("\033[H\033[2J");
    if (ui->w < 56 || ui->h < 14) {
        omove(2, 2);
        op("terminal too small for khatm (%dx%d)", ui->w, ui->h);
        return;
    }
    d_header(ui);
    d_tabs(ui);

    switch (ui->view) {
    case V_HOME:     d_home(ui); break;
    case V_BOOKS:    d_books(ui); break;
    case V_CHAPTERS: d_chapters(ui); break;
    case V_GOALS:    d_goals(ui); break;
    case V_STATS:    d_stats(ui); break;
    case V_STUDY:    d_study(ui); break;
    }

    static const char *hints[6] = {
        "[enter] study   [d] seal   [g] promise   [1-4] views   [?] help   [q] quit",
        "[↑↓] select   [enter] chapters   [g] promise the book   [q] quit",
        "[↑↓] select   [enter] study   [d] seal   [g] promise   [esc] back",
        "[↑↓] select   [d] drop promise   [1-4] views   [q] quit",
        "[1-4] views   [q] quit",
        "[enter] finish session   [esc] discard",
    };
    d_footer(ui, hints[ui->view]);
}

static int ui_modal(UI *ui, char lines[][160], int n, int accent) {
    draw_frame(ui);
    int bw = 0;
    for (int i = 0; i < n; i++) {
        int l = dlen(lines[i]);
        if (l > bw) bw = l;
    }
    bw += 6;
    if (bw > ui->w - 4) bw = ui->w - 4;
    int top = (ui->h - n - 2) / 2;
    if (top < 2) top = 2;
    int left = (ui->w - bw) / 2 + 1;

    omove(top, left);
    op("%s╭", fg(accent));
    for (int i = 0; i < bw - 2; i++) op("─");
    op("╮%s", TR);
    for (int i = 0; i < n; i++) {
        omove(top + 1 + i, left);
        int pad = bw - 2 - dlen(lines[i]);
        if (pad < 0) pad = 0;
        op("%s│%s%*s%s%*s%s│%s", fg(accent), TR,
           pad / 2, "", lines[i], pad - pad / 2, "", fg(accent), TR);
    }
    omove(top + 1 + n, left);
    op("%s╰", fg(accent));
    for (int i = 0; i < bw - 2; i++) op("─");
    op("╯%s", TR);
    oflush();
    int k;
    while ((k = plat_read_key(60000)) < 0) {}
    return k;
}

static int ui_prompt(UI *ui, const char *label, char *out, size_t n) {
    size_t len = strlen(out);
    for (;;) {
        draw_frame(ui);
        omove(ui->h - 1, 1);
        op("  %s%s%s %s%s%s█%s\033[K", fg(P_GOLDHI), label, TR,
           TB, out, fg(P_GOLD), TR);
        oflush();
        int k = plat_read_key(60000);
        if (k == K_ENTER) return 0;
        if (k == K_ESC) return -1;
        if (k == K_BS) {
            while (len && ((unsigned char)out[len - 1] & 0xC0) == 0x80) len--;
            if (len) len--;
            out[len] = 0;
        } else if (k >= 32 && k < 127 && len < n - 1) {
            out[len++] = (char)k;
            out[len] = 0;
        }
    }
}

static void act_seal(UI *ui, Ref r) {
    State *st = ui->st;
    Chapter *ch = CH(st, r);
    char rb[64], lines[12][160];
    if (ch->done_at) {
        toastf(ui, P_MUT, "%s is already sealed",
               ref_str(st, r, rb, sizeof rb));
        return;
    }
    if (!deps_met(st, r)) {
        snprintf(lines[0], 160, "prerequisites of %s are not sealed",
                 ref_str(st, r, rb, sizeof rb));
        snprintf(lines[1], 160, " ");
        snprintf(lines[2], 160, "[y] seal anyway      [esc] not yet");
        int k = ui_modal(ui, lines, 3, P_AMBER);
        if (k != 'y') return;
    }
    if (ev_done(st, r)) {
        toastf(ui, P_ROSE, "could not write to the log");
        return;
    }

    int n = 0;
    snprintf(lines[n++], 160, "%s✦  S E A L E D  ✦%s", TB, TR);
    snprintf(lines[n++], 160, "%s", fit(ch->title, 50));
    snprintf(lines[n++], 160, " ");
    if (ch->est_min > 0 && ch->minutes > 0)
        snprintf(lines[n++], 160, "time %.0fm vs ~%.0fm estimated",
                 ch->minutes, ch->est_min);
    else if (ch->minutes > 0)
        snprintf(lines[n++], 160, "time %.0fm logged", ch->minutes);
    if (ch->pages > 0)
        snprintf(lines[n++], 160, "pages %.0f", ch->pages);
    int unb = count_unblocks(st, r);
    if (unb)
        snprintf(lines[n++], 160, "unblocked %d chapter%s", unb,
                 unb == 1 ? "" : "s");
    for (int g = 0; g < st->ngoals; g++) {
        Goal *go = &st->goals[g];
        if (go->status == GOAL_KEPT && go->resolved_at == st->now &&
            goal_covers(st, go, r)) {
            char d[32];
            fmt_date(go->by, d, sizeof d);
            snprintf(lines[n++], 160, "%s✓ promise kept%s — you said by %s",
                     fg(P_SAGE), TR, d);
            break;
        }
    }
    Book *bk = &st->books[r.book];
    int done = 0;
    for (int c = 0; c < bk->nchs; c++)
        if (bk->chs[c].done_at) done++;
    snprintf(lines[n++], 160, "%s %d/%d",
             sbar(book_progress(st, r.book), 20, P_TEAL), done, bk->nchs);
    ui_modal(ui, lines, n, P_GOLD);

    if (done == bk->nchs) {
        double mins = 0;
        for (int i = 0; i < st->nsess; i++)
            if (st->sess[i].ch.book == r.book) mins += st->sess[i].minutes;
        n = 0;
        snprintf(lines[n++], 160, "════════ ✦ ✦ ✦ ════════");
        snprintf(lines[n++], 160, "%sK H A T M A%s", TB, TR);
        snprintf(lines[n++], 160, "%s", fit(bk->title, 50));
        snprintf(lines[n++], 160, "the book is sealed");
        if (mins > 0)
            snprintf(lines[n++], 160, "%.1f hours of your life, kept", mins / 60);
        snprintf(lines[n++], 160, "it is on your shelf now");
        snprintf(lines[n++], 160, "════════ ✦ ✦ ✦ ════════");
        ui_modal(ui, lines, n, P_GOLD);
        ui->view = V_STATS;
    }
    int k, m;
    double rate = kept_rate(st, &k, &m, NULL);
    if (rate >= 0)
        toastf(ui, P_SAGE, "your word, kept: %.0f%% of %d", rate * 100, k + m);
}

static void act_goal(UI *ui, Ref target) {
    State *st = ui->st;
    char buf[64] = "", rb[64], dl[32], lines[8][160];
    if (target.ch >= 0 && CH(st, target)->done_at) {
        toastf(ui, P_MUT, "already sealed — promise something living");
        return;
    }
    if (ui_prompt(ui, "promise it by (2026-07-01 · friday · +5d):",
                  buf, sizeof buf))
        return;
    time_t by = parse_when(trim(buf), st->now);
    if (!by) { toastf(ui, P_ROSE, "could not read \"%s\" as a date", buf); return; }
    if (by <= st->now) { toastf(ui, P_ROSE, "that day is already gone"); return; }

    double work = target.ch >= 0
        ? ch_weight(st, target) * (1 - ch_progress(st, target))
        : book_remaining_pages(st, target.book);
    int days = days_between(st->now, by);
    if (days < 1) days = 1;
    double need = work / days, vel = velocity_pages(st, -1, 28);

    fmt_date(by, dl, sizeof dl);
    int n = 0;
    snprintf(lines[n++], 160, "promise %s%s%s by %s (%d day%s)",
             TB, ref_str(st, target, rb, sizeof rb), TR, dl,
             days, days == 1 ? "" : "s");
    if (vel > 0)
        snprintf(lines[n++], 160, "needs ~%.0f p/day · your pace %.*f — %s",
                 need, vel < 10 ? 1 : 0, vel,
                 need <= vel ? "comfortably yours" :
                 need <= vel * 1.5 ? "a stretch" : "steep");
    else
        snprintf(lines[n++], 160, "needs ~%.0f p/day", need);
    int k_, m_;
    double rate = kept_rate(st, &k_, &m_, NULL);
    if (rate >= 0)
        snprintf(lines[n++], 160, "your word so far: %.0f%% kept", rate * 100);
    snprintf(lines[n++], 160, " ");
    snprintf(lines[n++], 160, "[enter] give your word    [esc] think again");
    int k = ui_modal(ui, lines, n, P_TEAL);
    if (k != K_ENTER) { toastf(ui, P_MUT, "no promise made"); return; }
    if (ev_goal(st, target, by)) {
        toastf(ui, P_ROSE, "could not write to the log");
        return;
    }
    toastf(ui, P_SAGE, "promised: %s by %s", rb, dl);
}

static void act_drop_goal(UI *ui) {
    State *st = ui->st;
    int idx[64];
    int n = open_goal_idx(st, idx, 64);
    if (!n) { toastf(ui, P_MUT, "nothing to drop"); return; }
    if (ui->sel_goal >= n) ui->sel_goal = n - 1;
    Goal *go = &st->goals[idx[ui->sel_goal]];
    char rb[64], lines[4][160];
    ref_str(st, go->target, rb, sizeof rb);
    snprintf(lines[0], 160, "drop the promise on %s?", rb);
    snprintf(lines[1], 160, "a drop never counts against your word");
    snprintf(lines[2], 160, " ");
    snprintf(lines[3], 160, "[enter] drop it      [esc] keep it");
    if (ui_modal(ui, lines, 4, P_AMBER) != K_ENTER) return;
    if (ev_dropgoal(st, go->target)) {
        toastf(ui, P_ROSE, "could not write to the log");
        return;
    }
    toastf(ui, P_MUT, "dropped %s — no judgment", rb);
}

static void act_study_start(UI *ui, Ref r) {
    Chapter *ch = CH(ui->st, r);
    char rb[64];
    if (ch->done_at) {
        toastf(ui, P_MUT, "%s is already sealed",
               ref_str(ui->st, r, rb, sizeof rb));
        return;
    }
    ui->study = r;
    ui->study_start = time(NULL);
    ui->view = V_STUDY;
}

static void act_study_finish(UI *ui, int discard) {
    State *st = ui->st;
    double min = difftime(time(NULL), ui->study_start) / 60.0;
    char rb[64];
    ref_str(st, ui->study, rb, sizeof rb);

    if (discard) {
        if (min >= 5) {
            char lines[3][160];
            snprintf(lines[0], 160, "discard %.0f minutes on %s?", min, rb);
            snprintf(lines[1], 160, " ");
            snprintf(lines[2], 160, "[enter] discard      [esc] keep studying");
            if (ui_modal(ui, lines, 3, P_AMBER) != K_ENTER) return;
        }
        ui->view = V_HOME;
        toastf(ui, P_MUT, "session discarded");
        return;
    }
    if (min < 1) min = 1;

    char buf[32] = "";
    char label[160];
    snprintf(label, sizeof label, "%.0f min on %s — pages read (enter = none):",
             min, rb);
    ui->view = V_HOME;
    if (ui_prompt(ui, label, buf, sizeof buf)) {
        ui->view = V_STUDY;
        return;
    }
    double pages = 0;
    char *s = trim(buf);
    if (*s && parse_nonneg_double(s, &pages)) {
        toastf(ui, P_ROSE, "pages must be a non-negative number");
        ui->view = V_STUDY;
        return;
    }

    int prev_streak = streak_current(st);
    double prev_week = biggest_week_min(st);
    if (ev_session(st, ui->study, min, pages, time(NULL))) {
        toastf(ui, P_ROSE, "could not write to the log");
        return;
    }
    double prog = ch_progress(st, ui->study);
    int strk = streak_current(st);
    if (strk > prev_streak && strk >= 3 && strk == streak_longest(st))
        toastf(ui, P_GOLDHI, "★ new record — longest streak, %d days", strk);
    else if (biggest_week_min(st) > prev_week && prev_week > 0 &&
             biggest_week_min(st) >= 120)
        toastf(ui, P_GOLDHI, "★ new record — biggest week, %.1fh",
               biggest_week_min(st) / 60.0);
    else if (prog >= 0.8 && prog < 1)
        toastf(ui, P_SAGE, "logged %.0fm — %s at %.0f%%, one push seals it "
               "(d on the chapter)", min, rb, prog * 100);
    else
        toastf(ui, P_SAGE, "logged %.0fm on %s%s", min, rb,
               strk >= 2 ? "" : "");
}

static void show_help(UI *ui) {
    char lines[12][160];
    int n = 0;
    snprintf(lines[n++], 160, "%skhatm%s — keys", TB, TR);
    snprintf(lines[n++], 160, " ");
    snprintf(lines[n++], 160, "1 2 3 4      home · books · goals · stats");
    snprintf(lines[n++], 160, "↑↓ / jk      move      ←→ / hl   switch view");
    snprintf(lines[n++], 160, "enter        open book / start studying");
    snprintf(lines[n++], 160, "d            seal a chapter (or drop a goal)");
    snprintf(lines[n++], 160, "g            promise a chapter or book");
    snprintf(lines[n++], 160, "esc          back      q   quit");
    snprintf(lines[n++], 160, " ");
    snprintf(lines[n++], 160, "the CLI still works: khatm help");
    ui_modal(ui, lines, n, P_TEAL);
}

static int view_tab(int view) {
    switch (view) {
    case V_BOOKS: case V_CHAPTERS: return 1;
    case V_GOALS: return 2;
    case V_STATS: return 3;
    default: return 0;
    }
}
static int tab_view(int t) {
    switch (t) {
    case 1: return V_BOOKS;
    case 2: return V_GOALS;
    case 3: return V_STATS;
    default: return V_HOME;
    }
}

int tui_run(State *st) {
    UI ui = {0};
    ui.st = st;
    ui.view = V_HOME;
    if (plat_raw()) {
        fprintf(stderr, "khatm: cannot enter raw mode\n");
        return 1;
    }

    while (!ui.quit) {
        draw_frame(&ui);
        oflush();
        int k = plat_read_key(ui.view == V_STUDY ? 250 : 1000);
        if (k < 0) continue;

        if (ui.view == V_STUDY) {
            if (k == K_ENTER || k == 's') act_study_finish(&ui, 0);
            else if (k == K_ESC || k == 'q') act_study_finish(&ui, 1);
            continue;
        }

        switch (k) {
        case 'q': ui.quit = 1; break;
        case '?': show_help(&ui); break;
        case '1': case '2': case '3': case '4':
            ui.view = tab_view(k - '1');
            break;
        case '\t': case K_RIGHT: case 'l':
            ui.view = tab_view((view_tab(ui.view) + 1) % 4);
            break;
        case K_LEFT: case 'h':
            if (ui.view == V_CHAPTERS) ui.view = V_BOOKS;
            else ui.view = tab_view((view_tab(ui.view) + 3) % 4);
            break;
        case K_ESC:
            if (ui.view == V_CHAPTERS) ui.view = V_BOOKS;
            else if (ui.view != V_HOME) ui.view = V_HOME;
            break;
        case K_UP: case 'k':
            if (ui.view == V_BOOKS && ui.sel_book > 0) ui.sel_book--;
            else if (ui.view == V_CHAPTERS && ui.sel_ch > 0) ui.sel_ch--;
            else if (ui.view == V_GOALS && ui.sel_goal > 0) ui.sel_goal--;
            break;
        case K_DOWN: case 'j':
            if (ui.view == V_BOOKS && ui.sel_book < st->nbooks - 1)
                ui.sel_book++;
            else if (ui.view == V_CHAPTERS &&
                     ui.sel_ch < st->books[ui.sel_book].nchs - 1)
                ui.sel_ch++;
            else if (ui.view == V_GOALS) ui.sel_goal++;
            break;
        case K_ENTER:
            if (ui.view == V_HOME && ui.has_suggestion)
                act_study_start(&ui, ui.suggestion);
            else if (ui.view == V_BOOKS && st->nbooks) {
                ui.view = V_CHAPTERS;
                ui.sel_ch = 0;
                ui.scroll = 0;
            } else if (ui.view == V_CHAPTERS)
                act_study_start(&ui,
                                (Ref){ ui.sel_book, ui.sel_ch });
            break;
        case 'd':
            if (ui.view == V_CHAPTERS)
                act_seal(&ui, (Ref){ ui.sel_book, ui.sel_ch });
            else if (ui.view == V_HOME && ui.has_suggestion)
                act_seal(&ui, ui.suggestion);
            else if (ui.view == V_GOALS)
                act_drop_goal(&ui);
            break;
        case 'g':
            if (ui.view == V_CHAPTERS)
                act_goal(&ui, (Ref){ ui.sel_book, ui.sel_ch });
            else if (ui.view == V_BOOKS && st->nbooks)
                act_goal(&ui, (Ref){ ui.sel_book, -1 });
            else if (ui.view == V_HOME && ui.has_suggestion)
                act_goal(&ui, ui.suggestion);
            break;
        }
    }
    plat_restore();
    return 0;
}
