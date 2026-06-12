#define _POSIX_C_SOURCE 200809L
#include "plat.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
/* ------------------------------------------------------------------ */
/* Win32 console. ANSI escapes work on Windows 10+ once               */
/* ENABLE_VIRTUAL_TERMINAL_PROCESSING is set; on older consoles that  */
/* SetConsoleMode fails and we degrade gracefully (CLI works, no TUI).*/
/* ------------------------------------------------------------------ */
#include <windows.h>
#include <direct.h>
#include <io.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

static DWORD saved_in_mode, saved_out_mode;
static int raw_on;

int plat_console_init(void) {
    SetConsoleOutputCP(CP_UTF8);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD m;
    if (!_isatty(1) || !GetConsoleMode(out, &m)) return 0;
    return SetConsoleMode(out, m | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}

void plat_restore(void) {
    if (!raw_on) return;
    raw_on = 0;
    const char *s = "\033[?1049l\033[?25h\033[0m";
    DWORD n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &n, NULL);
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), saved_in_mode);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), saved_out_mode);
}

int plat_raw(void) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(in, &saved_in_mode)) return -1;
    if (!GetConsoleMode(out, &saved_out_mode)) return -1;
    if (!SetConsoleMode(out, saved_out_mode |
                        ENABLE_PROCESSED_OUTPUT |
                        ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        return -1;
    /* no line input, no echo, no processed input (Ctrl-C becomes a
     * key), no quick-edit (it freezes output on a stray click) */
    if (!SetConsoleMode(in, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT)) {
        SetConsoleMode(out, saved_out_mode);
        return -1;
    }
    raw_on = 1;
    atexit(plat_restore);
    const char *s = "\033[?1049h\033[?25l";
    DWORD n;
    WriteFile(out, s, (DWORD)strlen(s), &n, NULL);
    return 0;
}

void plat_winsize(int *w, int *h) {
    CONSOLE_SCREEN_BUFFER_INFO bi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &bi)) {
        *w = bi.srWindow.Right - bi.srWindow.Left + 1;
        *h = bi.srWindow.Bottom - bi.srWindow.Top + 1;
        if (*w > 0 && *h > 0) return;
    }
    *w = 80; *h = 24;
}

int plat_read_key(int timeout_ms) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    for (;;) {
        if (WaitForSingleObject(in, (DWORD)timeout_ms) != WAIT_OBJECT_0)
            return -1;
        INPUT_RECORD rec;
        DWORD n = 0;
        if (!ReadConsoleInputW(in, &rec, 1, &n) || n != 1) return -1;
        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown)
            continue;        /* resize/focus/key-up: poll again */
        switch (rec.Event.KeyEvent.wVirtualKeyCode) {
        case VK_UP:     return K_UP;
        case VK_DOWN:   return K_DOWN;
        case VK_LEFT:   return K_LEFT;
        case VK_RIGHT:  return K_RIGHT;
        case VK_RETURN: return K_ENTER;
        case VK_ESCAPE: return K_ESC;
        case VK_BACK:   return K_BS;
        }
        WCHAR c = rec.Event.KeyEvent.uChar.UnicodeChar;
        if (c == 3) return 'q';                  /* Ctrl-C */
        if (c == 8) return K_BS;
        if (c == '\r' || c == '\n') return K_ENTER;
        if (c == '\t') return '\t';
        if (c >= 32 && c < 127) return (int)c;
    }
}

void plat_out(const char *buf, size_t n) {
    DWORD wr;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, (DWORD)n, &wr, NULL);
}

int plat_mkdir(const char *path) {
    if (_mkdir(path) == 0) return 0;
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES &&
            (a & FILE_ATTRIBUTE_DIRECTORY)) ? 0 : -1;
}

int plat_stdin_tty(void) {
    return _isatty(0);
}

int plat_wait_enter(int timeout_ms) {
    /* no console: nothing interactive can arrive   end the session
     * rather than risk blocking on a pipe forever */
    if (!_isatty(0)) return -1;
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD start = GetTickCount();
    for (;;) {
        DWORD spent = GetTickCount() - start;
        if (spent >= (DWORD)timeout_ms) return 0;
        if (WaitForSingleObject(in, (DWORD)timeout_ms - spent)
            != WAIT_OBJECT_0)
            return 0;
        INPUT_RECORD rec;
        DWORD n = 0;
        if (!ReadConsoleInputW(in, &rec, 1, &n) || n != 1) return -1;
        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown &&
            rec.Event.KeyEvent.wVirtualKeyCode == VK_RETURN)
            return 1;
    }
}

const char *plat_home(void) {
    const char *h = getenv("HOME");
    if (!h || !*h) h = getenv("USERPROFILE");
    return (h && *h) ? h : ".";
}

#else
/* ------------------------------------------------------------------ */
/* POSIX (Linux, macOS, the BSDs).                                     */
/* ------------------------------------------------------------------ */
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

static struct termios saved_tio;
static int raw_on;

int plat_console_init(void) {
    return isatty(1);
}

void plat_restore(void) {
    if (!raw_on) return;
    raw_on = 0;
    const char *s = "\033[?1049l\033[?25h\033[0m";
    ssize_t w = write(1, s, strlen(s)); (void)w;
    tcsetattr(0, TCSAFLUSH, &saved_tio);
}

static void on_fatal_signal(int sig) {
    plat_restore();
    _exit(128 + sig);
}

int plat_raw(void) {
    if (tcgetattr(0, &saved_tio)) return -1;
    struct termios t = saved_tio;
    t.c_lflag &= ~(unsigned)(ECHO | ICANON | ISIG | IEXTEN);
    t.c_iflag &= ~(unsigned)(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    t.c_oflag &= ~(unsigned)OPOST;
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSAFLUSH, &t)) return -1;
    raw_on = 1;
    atexit(plat_restore);
    signal(SIGINT, SIG_IGN);            /* ISIG is off; ^C is a key */
    signal(SIGTERM, on_fatal_signal);   /* don't leave the tty raw  */
    signal(SIGHUP, on_fatal_signal);
    const char *s = "\033[?1049h\033[?25l";
    ssize_t w = write(1, s, strlen(s)); (void)w;
    return 0;
}

void plat_winsize(int *w, int *h) {
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        *w = ws.ws_col;
        *h = ws.ws_row;
    } else {
        *w = 80; *h = 24;
    }
}

int plat_read_key(int timeout_ms) {
    struct pollfd p = { 0, POLLIN, 0 };
    if (poll(&p, 1, timeout_ms) <= 0) return -1;
    unsigned char c;
    if (read(0, &c, 1) != 1) return -1;
    if (c == 0x1b) {
        struct pollfd q = { 0, POLLIN, 0 };
        if (poll(&q, 1, 25) <= 0) return K_ESC;
        unsigned char c2;
        if (read(0, &c2, 1) != 1) return K_ESC;
        if (c2 != '[' && c2 != 'O') return K_ESC;
        unsigned char c3;
        if (read(0, &c3, 1) != 1) return K_ESC;
        switch (c3) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        default:
            while (c3 >= '0' && c3 <= ';')
                if (read(0, &c3, 1) != 1) break;
            return -1;
        }
    }
    if (c == '\r' || c == '\n') return K_ENTER;
    if (c == 127 || c == 8) return K_BS;
    if (c == 3) return 'q';
    return c;
}

void plat_out(const char *buf, size_t n) {
    ssize_t w = write(1, buf, n); (void)w;
}

int plat_mkdir(const char *path) {
    if (mkdir(path, 0755) == 0 || errno == EEXIST) return 0;
    return -1;
}

int plat_stdin_tty(void) {
    return isatty(0);
}

int plat_wait_enter(int timeout_ms) {
    struct pollfd p = { 0, POLLIN, 0 };
    if (poll(&p, 1, timeout_ms) <= 0) return 0;
    /* cooked mode: data arrives only once Enter is pressed; drain it */
    char buf[256];
    ssize_t got = read(0, buf, sizeof buf);
    return got > 0 ? 1 : -1;
}

const char *plat_home(void) {
    const char *h = getenv("HOME");
    return (h && *h) ? h : ".";
}

#endif
