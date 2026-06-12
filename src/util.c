#define _POSIX_C_SOURCE 200809L
#include "khatm.h"
#include <ctype.h>
#include <math.h>

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "khatm: out of memory\n"); exit(1); }
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "khatm: out of memory\n"); exit(1); }
    return q;
}

char *xstrdup(const char *s) {
    size_t n = strlen(s);
    char *p = xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

char *xstrndup(const char *s, size_t n) {
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

int ieq(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

int icontains(const char *hay, const char *needle) {
    size_t nl = strlen(needle);
    if (!nl) return 1;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < nl && hay[i] &&
               tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nl) return 1;
    }
    return 0;
}

time_t day_start(time_t t) {
    struct tm tm = *localtime(&t);
    tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0; tm.tm_isdst = -1;
    return mktime(&tm);
}

time_t day_end(time_t t) {
    struct tm tm = *localtime(&t);
    tm.tm_hour = 23; tm.tm_min = 59; tm.tm_sec = 59; tm.tm_isdst = -1;
    return mktime(&tm);
}

int days_between(time_t a, time_t b) {
    double d = difftime(day_start(b), day_start(a));
    return (int)((d + (d < 0 ? -43200 : 43200)) / 86400);
}

static int weekday_index(const char *s) {
    static const char *names[7] =
        { "sunday", "monday", "tuesday", "wednesday",
          "thursday", "friday", "saturday" };
    for (int i = 0; i < 7; i++) {
        size_t n = strlen(s);
        if (n >= 3 && strncmp(names[i], s, n) == 0) return i;
        if (n >= 3) {
            size_t j = 0;
            while (j < n && tolower((unsigned char)s[j]) == names[i][j]) j++;
            if (j == n) return i;
        }
    }
    return -1;
}

time_t parse_when(const char *s, time_t now) {
    int y, m, d, used = 0;
    if (sscanf(s, "%d-%d-%d%n", &y, &m, &d, &used) == 3 && s[used] == 0) {
        struct tm tm = {0};
        tm.tm_year = y - 1900; tm.tm_mon = m - 1; tm.tm_mday = d;
        tm.tm_hour = 23; tm.tm_min = 59; tm.tm_sec = 59; tm.tm_isdst = -1;
        time_t t = mktime(&tm);
        if (t == (time_t)-1) return 0;
        struct tm check = *localtime(&t);
        if (check.tm_year != y - 1900 || check.tm_mon != m - 1 ||
            check.tm_mday != d)
            return 0;
        return t;
    }
    if (s[0] == '+') {
        char *end;
        long n = strtol(s + 1, &end, 10);
        char unit = *end;
        if (end != s + 1 && n > 0 && (unit == 0 || end[1] == 0)) {
            if (unit == 0 || unit == 'd') return day_end(now + (time_t)n * 86400);
            if (unit == 'w') return day_end(now + (time_t)n * 7 * 86400);
        }
        return 0;
    }
    if (ieq(s, "today")) return day_end(now);
    if (ieq(s, "tomorrow")) return day_end(now + 86400);
    int wd = weekday_index(s);
    if (wd >= 0) {
        struct tm tm = *localtime(&now);
        int delta = (wd - tm.tm_wday + 7) % 7;
        if (delta == 0) delta = 7;
        return day_end(now + (time_t)delta * 86400);
    }
    return 0;
}

double parse_duration_min(const char *s) {
    double total = 0, num = 0;
    int any = 0, innum = 0, frac = 0;
    double fracmul = 0.1;
    for (; *s; s++) {
        if (isdigit((unsigned char)*s)) {
            if (frac) { num += (*s - '0') * fracmul; fracmul /= 10; }
            else num = num * 10 + (*s - '0');
            innum = 1;
        } else if (*s == '.') {
            frac = 1;
        } else if (*s == 'h' || *s == 'H') {
            if (!innum) return -1;
            total += num * 60; num = 0; innum = frac = 0; fracmul = 0.1; any = 1;
        } else if (*s == 'm' || *s == 'M') {
            if (!innum) return -1;
            total += num; num = 0; innum = frac = 0; fracmul = 0.1; any = 1;
        } else if (isspace((unsigned char)*s)) {
            continue;
        } else {
            return -1;
        }
    }
    if (innum) { total += num; any = 1; }
    return any ? total : -1;
}

int parse_nonneg_double(const char *s, double *out) {
    char *end;
    double v = strtod(s, &end);
    if (end == s) return -1;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end || v < 0 || !isfinite(v)) return -1;
    *out = v;
    return 0;
}

void fmt_date(time_t t, char *buf, size_t n) {
    struct tm tm = *localtime(&t);
    strftime(buf, n, "%Y-%m-%d", &tm);
}

void fmt_ts(time_t t, char *buf, size_t n) {
    struct tm tm = *localtime(&t);
    strftime(buf, n, "%Y-%m-%dT%H:%M:%S", &tm);
}

time_t parse_ts(const char *s) {
    int y, mo, d, h = 12, mi = 0, sec = 0;
    int used = 0;
    int n = sscanf(s, "%d-%d-%dT%d:%d:%d%n", &y, &mo, &d, &h, &mi, &sec,
                   &used);
    if (n < 3) return 0;
    if (s[used] != 0 && s[used] != '\n') return 0;
    struct tm tm = {0};
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
    tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = sec; tm.tm_isdst = -1;
    time_t t = mktime(&tm);
    if (t == (time_t)-1) return 0;
    struct tm check = *localtime(&t);
    if (check.tm_year != y - 1900 || check.tm_mon != mo - 1 ||
        check.tm_mday != d || check.tm_hour != h || check.tm_min != mi ||
        check.tm_sec != sec)
        return 0;
    return t;
}
