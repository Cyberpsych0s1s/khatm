#ifndef KHATM_H
#define KHATM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* "ختم" for terminal output. Terminals do neither bidi nor Arabic shaping,
 * so the word is pre-shaped (Unicode Presentation Forms-B: each letter in
 * its joined glyph) and stored in visual, left-to-right order:
 *   U+FEE2 meem-final + U+FE98 teh-medial + U+FEA7 khah-initial.
 * Markdown/docs keep the logical-order "ختم"   proper renderers do bidi. */
#define KHATM_AR "\xEF\xBB\xA2\xEF\xBA\x98\xEF\xBA\xA7"

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

/* Grow-by-doubling push: evaluates to a pointer to the new (uninitialized)
 * slot. Usage: *VPUSH(v, vlen, vcap) = value;  (v is T*, len/cap are int.) */
#define VPUSH(v, len, cap) \
    (((len) == (cap) ? (void)((cap) = (cap) ? (cap) * 2 : 8, \
        (v) = xrealloc((v), (size_t)(cap) * sizeof *(v))) : (void)0), \
     &(v)[(len)++])

char *trim(char *s);
int   ieq(const char *a, const char *b);
int   icontains(const char *hay, const char *needle);

time_t day_start(time_t t);
time_t day_end(time_t t);
int    days_between(time_t a, time_t b);
time_t parse_when(const char *s, time_t now);  /* ISO date | +Nd | weekday |
                                        today | tomorrow; 0 on failure.
                                        Returns end-of-day timestamp.    */
double parse_duration_min(const char *s);      /* "35m" "1h30m" "1.5h" "90";
                                        <=0 on failure                   */
int    parse_nonneg_double(const char *s, double *out);
void   fmt_date(time_t t, char *buf, size_t n);
void   fmt_ts(time_t t, char *buf, size_t n);
time_t parse_ts(const char *s);

typedef struct Ref { int book; int ch; } Ref;   /* ch == -1: whole book */

typedef struct Section {
    char  *title;
    char **needs_raw; int nneeds_raw, cneeds_raw;
} Section;

/* A spaced-review card: `? front :: back` inline under a chapter in the .md.
 * The SRS fields below are not stored in the file   they are folded from the
 * append-only `review` log events at load (SM-2). `id` (FNV-1a of front) is
 * the card's identity in the log, so reordering or re-indenting cards in the
 * .md keeps their history; only editing the front text resets a card. */
typedef struct Card {
    char    *front, *back;
    unsigned id;
    int      reps;          /* successful reps in a row */
    double   ef;            /* ease factor, >= 1.3 */
    int      interval;      /* days from last_review to due */
    time_t   last_review;
    time_t   due;
    int      seen;          /* 0 = new (never reviewed) */
} Card;

typedef struct Chapter {
    char  *title;
    int    section;
    double est_pages;
    double est_min;
    int    done_in_file;
    char **needs_raw; int nneeds_raw, cneeds_raw;
    Ref   *needs;     int nneeds, cneeds;
    Card  *cards;     int ncards, ccards;
    double minutes, pages;
    time_t done_at;
    time_t last_session;
} Chapter;

typedef struct Book {
    char   *id;
    char   *title;
    char   *path;
    time_t  deadline;
    double  total_pages;
    time_t  mtime;
    Chapter *chs;  int nchs,  cchs;
    Section *secs; int nsecs, csecs;
} Book;

enum { GOAL_OPEN, GOAL_KEPT, GOAL_MISSED, GOAL_DROPPED };

typedef struct Goal {
    time_t set_at, by;
    Ref    target;
    int    status;
    time_t resolved_at;
} Goal;

typedef struct Session {
    time_t at;
    Ref    ch;
    double minutes, pages;
} Session;

typedef struct State {
    char    *root;
    Book    *books; int nbooks, cbooks;
    Goal    *goals; int ngoals, cgoals;
    Session *sess;  int nsess,  csess;
    time_t   now;
} State;

int  syl_load(State *st);
/* Syllabus writers (append-only   khatm never rewrites user lines).
 * They also update in-memory state. 0 ok; -1 with a message in err. */
int  syl_book_new(State *st, const char *id, const char *title,
                  double pages, time_t deadline, char *err, size_t errn);
int  syl_book_add(State *st, const char *id, const char *title,
                  const char *est, const char *needs,
                  char *err, size_t errn);
int  syl_book_section(State *st, const char *id, const char *title,
                      const char *needs, char *err, size_t errn);
/* Resolve one raw needs token ("3", "Section", "book/3", "book/Section")
 * relative to book b. Appends resolved chapter refs to ch->needs.
 * Returns 0 ok, -1 unresolved. */
int  syl_resolve_token(State *st, int b, const char *tok, Chapter *ch);
int  syl_need_resolves(State *st, int b, const char *tok, Chapter *ch);
void syl_resolve_all(State *st, FILE *complain);

int  log_load(State *st);
int  log_append(State *st, const char *line);
const char *log_path(State *st, char *buf, size_t n); /* NULL: doesn't fit */

/* Events: append to the log AND fold into in-memory state, so CLI and TUI
 * share one mutation path. 0 ok, -1 log write failed.
 * ev_dropgoal drops the newest open goal on r (1: no open goal   nothing
 * appended); ev_dropgoal_g drops st->goals[g] exactly. */
int  ev_session(State *st, Ref r, double min, double pages, time_t when);
int  ev_done(State *st, Ref r);
int  ev_goal(State *st, Ref r, time_t by);
int  ev_dropgoal(State *st, Ref r);
int  ev_dropgoal_g(State *st, int g);

int  deps_met(State *st, Ref r);
int  count_unblocks(State *st, Ref r);
int  plan_total_chs(State *st);
/* Implied deadline per chapter (one slot per chapter, file order across
 * books; 0 = none): the earliest of goals covering it, its book deadline,
 * and deadlines inherited from dependents through needs edges. */
void plan_implied(State *st, time_t *out);
/* Pages-equivalent left to complete r   including every unsealed chapter
 * it transitively needs. r.ch == -1: the whole book. prereq_out (may be
 * NULL) receives just the prerequisite portion. */
double plan_goal_work(State *st, Ref r, double *prereq_out);
/* Best next candidates, sorted. Fills out[] up to max; returns count.
 * reason receives a short human string for out[0..count-1]. */
typedef struct NextPick { Ref r; double score; char reason[160]; } NextPick;
int  plan_next(State *st, NextPick *out, int max);

double ch_weight(State *st, Ref r);
double ch_progress(State *st, Ref r);
double book_bias_min(State *st, int b, int *n_out);
double book_calib_pages(State *st, int b);
double book_calib_min(State *st, int b);
double book_progress(State *st, int b);
double velocity_pages(State *st, int b, int days);
double velocity_min(State *st, int b, int days);
double book_remaining_pages(State *st, int b);
int    goal_covers(State *st, Goal *g, Ref ch);
void   goals_refresh(State *st);
double kept_rate(State *st, int *kept, int *missed, int *open_);
int    streak_current(State *st);
int    streak_longest(State *st);
double biggest_week_min(State *st);

extern int g_color;
const char *C(const char *code);
#define CRESET  C("\033[0m")
#define CBOLD   C("\033[1m")
#define CDIM    C("\033[2m")
#define CGREEN  C("\033[32m")
#define CYELLOW C("\033[33m")
#define CRED    C("\033[31m")
#define CCYAN   C("\033[36m")
#define CMAGENTA C("\033[35m")

void ui_bar(double frac, int width);
void ui_seal(State *st, Ref r, int quiet);
void ui_khatma(State *st, int b);
void ui_shelf(State *st);
void ui_heatmap(State *st, int weeks);

/* The ceremony: the cinematic seal-stamp reveal on `khatm done`. A transient
 * alt-screen animation, so the textual receipt printed afterwards still lands
 * in the scrollback. should_play() gates on tty + colour + UTF-8 + !json +
 * KHATM_ANIM != 0 + terminal size; when it says no, only the static UI runs. */
int  cere_should_play(void);
/* own_screen: 1 from the CLI (enter/leave the alt screen here); 0 from the
 * TUI, which is already in raw mode + alt screen and redraws afterwards. */
void cere_play(State *st, Ref r, int book_complete, int own_screen);

int cmd_init(State *st, int argc, char **argv);
int cmd_book(State *st, int argc, char **argv);
int cmd_books(State *st, int argc, char **argv);
int cmd_status(State *st, int argc, char **argv);
int cmd_next(State *st, int argc, char **argv);
int cmd_log(State *st, int argc, char **argv);
int cmd_study(State *st, int argc, char **argv);
int cmd_goal(State *st, int argc, char **argv);
int cmd_done(State *st, int argc, char **argv);
int cmd_pace(State *st, int argc, char **argv);
int cmd_shelf(State *st, int argc, char **argv);
int cmd_graph(State *st, int argc, char **argv);
int cmd_doctor(State *st, int argc, char **argv);
int cmd_review(State *st, int argc, char **argv);
int cmd_cards(State *st, int argc, char **argv);
int cmd_edit(State *st, int argc, char **argv);

/* SRS (spaced review). Cards are reviewable once their chapter is sealed.
 * grade: 0 again · 1 hard · 2 good · 3 easy. */
void srs_apply(Card *c, int grade, time_t when);  /* fold one review (SM-2) */
int  card_due(State *st, Card *c);                 /* new cards are due now  */
int  cards_due_count(State *st, int book);         /* book == -1: all books  */
int  ev_review(State *st, Ref ch, Card *c, int grade);

int  resolve_target(State *st, const char *arg, Ref *out, int allow_book);
const char *ref_str(State *st, Ref r, char *buf, size_t n);
void announce_records(State *st, int prev_streak, double prev_week);

int tui_run(State *st);

extern int g_json;
int  cmd_dump(State *st, int argc, char **argv);
void api_err(const char *msg);
int  api_session_json(State *st, Ref r, double min, double pages,
                      int prev_streak, double prev_week);
int  api_done_json(State *st, Ref r, int already);
int  api_goal_json(State *st, Ref r, time_t by);
int  api_drop_json(State *st, Ref r);
int  api_book_json(const char *event, const char *id, int chapter);

#endif
