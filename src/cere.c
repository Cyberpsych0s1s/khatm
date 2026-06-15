#define _POSIX_C_SOURCE 200809L
#include "khatm.h"
#include "plat.h"
#include <stdarg.h>

/* The ceremony: the cinematic seal-stamp reveal that plays on `khatm done`.
 * A transient alt-screen animation   it leaves no trace in the scrollback,
 * so the textual receipt the caller prints afterwards is the durable record.
 *
 * Three beats run as one reveal: the gold wax seal slams down (every
 * chapter), then on a finished book it blooms into the khatma page and the
 * spine glides onto the shelf. UTF-8 + 256-colour is assumed   the gate
 * cere_should_play() refuses anything less and the static boxes carry on. */

/* palette (mirrors tui.c) */
enum { CL_GOLD = 178, CL_GOLDHI = 214, CL_BRIGHT = 230, CL_FLASH = 231,
       CL_INK = 252, CL_MUT = 245, CL_FAINT = 240, CL_SAGE = 108 };

/* "ختم", pre-shaped; or a 3-cell roman stand-in when a font mangles it */
#define MARK (getenv("KHATM_ROMAN") ? "khm" : KHATM_AR)

/* ---- own output buffer (tui.c's op/ob live in another translation unit) -- */
static char   cb[1 << 16];
static size_t cl;
static int    g_skip;

static void cflush(void) { if (cl) { plat_out(cb, cl); cl = 0; } }

static void cp(const char *fmt, ...) {
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof b) n = sizeof b - 1;
    if (cl + (size_t)n >= sizeof cb) cflush();
    memcpy(cb + cl, b, (size_t)n);
    cl += (size_t)n;
}

static void cls(void)            { cp("\033[2J\033[H"); }
static void at(int r, int c)     { cp("\033[%d;%dH", r, c); }
static void fgi(int n)           { cp("\033[38;5;%dm", n); }
static void rst(void)            { cp("\033[0m"); }

/* flush the frame, then sleep   unless a key was hit, which aborts the whole
 * reveal (skip = "just show me the result"). Returns 1 once skipping. */
static int frame(int ms) {
    cflush();
    if (!g_skip && plat_read_key(0) != -1) g_skip = 1;
    if (!g_skip) plat_sleep_ms(ms);
    return g_skip;
}
#define STEP(ms) do { if (frame(ms)) goto done; } while (0)

static int vwidth(const char *s) {
    int n = 0;
    for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++;
    return n;
}

static void center_at(int row, int w, int color, const char *s) {
    int x = (w - vwidth(s)) / 2 + 1;
    if (x < 1) x = 1;
    at(row, x);
    fgi(color);
    cp("%s", s);
    rst();
}

static void box_center(int row, int bx, int bw, int color, const char *s) {
    int x = bx + (bw - vwidth(s)) / 2;
    if (x < bx) x = bx;
    at(row, x);
    fgi(color);
    cp("%s", s);
    rst();
}

/* the wax-blob seal, top-left at (sr,sc); rows are pre-padded to a 15-cell
 * field. mark_col tints the ✦ sparkles and the ختم so they read on the gold. */
static void draw_seal(int sr, int sc, int body, int edge, int mark_col) {
    fgi(edge);  at(sr + 0, sc); cp("  ▄▆██████▆▄");
    fgi(body);  at(sr + 1, sc); cp("▒████████████▒");
    at(sr + 2, sc); cp("█████  ");
        fgi(mark_col); cp("✦"); fgi(body); cp("  █████");
    at(sr + 3, sc); cp("████  ");
        fgi(mark_col); cp("%s", MARK); fgi(body); cp("  ████");
    at(sr + 4, sc); cp("█████  ");
        fgi(mark_col); cp("✦"); fgi(body); cp("  █████");
    fgi(body);  at(sr + 5, sc); cp("▒████████████▒");
    fgi(edge);  at(sr + 6, sc); cp("  ▀▆██████▆▀");
    rst();
}

/* faint dots expanding away from the seal centre   the impact ripple */
static void draw_ring(int cy, int cx, int rad) {
    static const int d[8][2] =
        { {-1,0},{1,0},{0,-2},{0,2},{-1,-2},{-1,2},{1,-2},{1,2} };
    fgi(CL_FAINT);
    for (int i = 0; i < 8; i++) {
        int ry = cy + d[i][0] * rad, cxx = cx + d[i][1] * rad;
        if (ry >= 1 && cxx >= 1) { at(ry, cxx); cp("·"); }
    }
    rst();
}

static void draw_box(int by, int bx, int bw, int bh) {
    fgi(CL_GOLD);
    at(by, bx); cp("◈");
    for (int i = 0; i < bw - 2; i++) cp("─");
    cp("◈");
    for (int r = 1; r < bh - 1; r++) {
        at(by + r, bx); cp("│");
        for (int i = 0; i < bw - 2; i++) cp(" ");
        cp("│");
    }
    at(by + bh - 1, bx); cp("◈");
    for (int i = 0; i < bw - 2; i++) cp("─");
    cp("◈");
    rst();
}

static void book_stats(State *st, int b, char *out, size_t n) {
    double mins = 0, pages = 0;
    time_t first = 0, last = 0;
    for (int i = 0; i < st->nsess; i++) {
        if (st->sess[i].ch.book != b) continue;
        mins  += st->sess[i].minutes;
        pages += st->sess[i].pages;
        if (!first || st->sess[i].at < first) first = st->sess[i].at;
        if (st->sess[i].at > last) last = st->sess[i].at;
    }
    if (mins <= 0) { snprintf(out, n, "the book is sealed"); return; }
    size_t o = 0;
    o += (size_t)snprintf(out + o, n - o, "%.1f hours", mins / 60.0);
    if (pages > 0 && o < n)
        o += (size_t)snprintf(out + o, n - o, " · %.0f pages", pages);
    if (first && last > first && o < n)
        snprintf(out + o, n - o, " · %d days", days_between(first, last) + 1);
}

/* one shelf row of sealed-book spines, the just-sealed one (hib) lit gold */
static const char *shortt(const char *s) {
    static char b[24];
    snprintf(b, sizeof b, "%.18s", s);
    return b;
}

static void draw_shelf(State *st, int row, int w,
                       const int *sealed, int ns, int hib, int base) {
    int tot = 0;
    for (int i = 0; i < ns; i++)
        tot += (int)strlen(shortt(st->books[sealed[i]].title)) + 5;
    int col = (w - tot) / 2 + 1;
    if (col < 1) col = 1;
    at(row, col);
    for (int i = 0; i < ns; i++) {
        Book *bk = &st->books[sealed[i]];
        fgi(sealed[i] == hib ? CL_GOLDHI : base);
        cp("▐ %s ▌ ", shortt(bk->title));
    }
    rst();
    at(row + 1, col);
    fgi(CL_FAINT);
    for (int i = 0; i < tot; i++) cp("═");
    rst();
}

int cere_should_play(void) {
    if (g_json || !g_color) return 0;
    if (!plat_utf8()) return 0;
    if (!plat_stdin_tty()) return 0;          /* need a tty to drive + skip */
    const char *a = getenv("KHATM_ANIM");
    if (a && a[0] == '0') return 0;
    int w, h;
    plat_winsize(&w, &h);
    return w >= 24 && h >= 12;
}

void cere_play(State *st, Ref r, int book_complete, int own_screen) {
    if (own_screen && plat_raw()) return;   /* no alt screen: skip silently */
    g_skip = 0;
    int w, h;
    plat_winsize(&w, &h);

    int sc = (w - 14) / 2 + 1;       if (sc < 1) sc = 1;
    int cy = (h - 7) / 2 - 1;        if (cy < 1) cy = 1;
    int ccx = sc + 7, ccy = cy + 3;  /* seal centre */

    /* ---- BEAT 1: the stamp descends, hits, ripples ---- */
    int top = cy - 5; if (top < 1) top = 1;
    for (int y = top; y <= cy; y++) {
        cls();
        draw_seal(y, sc, CL_GOLD, CL_GOLDHI, CL_BRIGHT);
        STEP(26);
    }
    cls(); draw_seal(cy, sc, CL_FLASH, CL_FLASH, CL_FLASH); STEP(70);   /* flash */
    cls(); draw_seal(cy, sc + 1, CL_GOLD, CL_GOLDHI, CL_BRIGHT); STEP(34); /* shake */
    for (int rad = 1; rad <= 4; rad++) {
        cls();
        draw_seal(cy, sc, CL_GOLD, CL_GOLDHI, CL_BRIGHT);
        draw_ring(ccy, ccx, rad);
        STEP(46);
    }
    cls(); draw_seal(cy, sc, CL_GOLD, CL_GOLDHI, CL_BRIGHT);

    {
        Chapter *ch = &st->books[r.book].chs[r.ch];
        char cap[160];
        snprintf(cap, sizeof cap, "✦ sealed   %.90s", ch->title);
        center_at(cy + 9, w, CL_SAGE, cap);
    }
    if (!book_complete) { STEP(950); goto done; }

    STEP(650);

    /* ---- BEAT 2/3: bloom into the khatma page ---- */
    int bw = w - 6; if (bw > 48) bw = 48;
    int bh = 9;
    int bx = (w - bw) / 2 + 1; if (bx < 1) bx = 1;
    int by = (h - bh) / 2 + 1; if (by < 1) by = 1;

    cls();
    draw_box(by, bx, bw, bh);
    box_center(by + 1, bx, bw, CL_GOLDHI, MARK);
    STEP(160);

    const char *kh = "K H A T M A";
    for (size_t i = 1; i <= strlen(kh); i++) {
        char part[32];
        memcpy(part, kh, i); part[i] = 0;
        box_center(by + 3, bx, bw, CL_GOLDHI, part);
        STEP(48);
    }

    {
        Book *bk = &st->books[r.book];
        char t[64], s[120];
        snprintf(t, sizeof t, "%.50s", bk->title);
        box_center(by + 5, bx, bw, CL_INK, t);
        STEP(140);
        box_center(by + 6, bx, bw, CL_MUT, "the book is sealed");
        book_stats(st, r.book, s, sizeof s);
        box_center(by + 7, bx, bw, CL_SAGE, s);
    }
    STEP(1500);

    /* ---- BEAT 4: the spine glides to the shelf ---- */
    int sealed[256], ns = 0;
    for (int b = 0; b < st->nbooks && ns < 256; b++) {
        Book *bk = &st->books[b];
        int all = bk->nchs > 0;
        for (int c = 0; c < bk->nchs; c++)
            if (!bk->chs[c].done_at) { all = 0; break; }
        if (all) sealed[ns++] = b;
    }
    int srow = h / 2;
    for (int k = 0; k < 6; k++) {
        cls();
        center_at(srow - 2, w, CL_MUT, "your shelf");
        draw_shelf(st, srow, w, sealed, ns, r.book,
                   (k & 1) ? CL_GOLDHI : CL_GOLD);   /* shimmer pulse */
        STEP(150);
    }
    STEP(1300);

done:
    cflush();
    if (own_screen) plat_restore();    /* the TUI keeps its own raw screen */
}
