#ifndef KHATM_PLAT_H
#define KHATM_PLAT_H

#include <stddef.h>

/* Platform layer: everything OS-specific lives behind these calls.
 * plat.c has two implementations   POSIX (termios/poll/ioctl) and
 * Win32 console (SetConsoleMode/ReadConsoleInput). The TUI itself
 * speaks only ANSI escapes, which both understand. */

/* keys returned by plat_read_key beyond plain ASCII characters */
enum { K_UP = 1000, K_DOWN, K_LEFT, K_RIGHT, K_ENTER, K_ESC, K_BS };

/* One-time console setup (UTF-8 output, ANSI escape processing on
 * Windows). Returns 1 if stdout is a terminal that will render
 * escapes   the "may we use color?" answer. */
int  plat_console_init(void);

int  plat_raw(void);            /* enter raw mode + alt screen; 0 ok */
void plat_restore(void);        /* leave raw mode (idempotent)       */
void plat_winsize(int *w, int *h);
int  plat_read_key(int timeout_ms);   /* key, or -1 on timeout       */
void plat_out(const char *buf, size_t n);

int  plat_mkdir(const char *path);    /* 0 if created or exists      */
const char *plat_home(void);          /* never NULL                  */
int  plat_stdin_tty(void);            /* may we ask a question?      */

void plat_sleep_ms(int ms);           /* animation frame timing      */
int  plat_utf8(void);                 /* terminal likely renders UTF-8 */

/* Cooked-mode wait used by the CLI pomodoro: block until Enter is
 * pressed or the timeout elapses. 1 = Enter, 0 = timeout, -1 = EOF. */
int  plat_wait_enter(int timeout_ms);

#endif
