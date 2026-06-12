#define _POSIX_C_SOURCE 200809L
#include "khatm.h"
#include "plat.h"
#include <unistd.h>

static void usage(void) {
    printf(
"khatm (" KHATM_AR ") — a build system for your studying\n"
"\n"
"  khatm                         the TUI (default in a terminal)\n"
"  khatm init                    prepare the data dir (default ~/.khatm)\n"
"  khatm books                   every book with its progress\n"
"  khatm status                  today, streak, promises, what to study\n"
"  khatm next                    dependency- and deadline-aware: study this\n"
"  khatm study <chapter>         timed session (Enter to stop)\n"
"  khatm study <ch> --pomo [25/5]    pomodoro: focus/break intervals;\n"
"                                only focused minutes are logged\n"
"  khatm log <chapter> <dur>     log a session  [--pages N] [--date D]\n"
"  khatm book new <id> [title]   start a syllabus  [--pages N] [--deadline D]\n"
"  khatm book add <id> <title>   append a chapter  [--est 12p] [--needs ...]\n"
"  khatm book section <id> <t>   append a section heading\n"
"  khatm goal <target> --by <when>   promise; priced before you make it\n"
"  khatm goal                    list open promises and your kept-rate\n"
"  khatm goal --drop <target>    drop a promise (never counts against you)\n"
"  khatm done <chapter> [-q]     seal a chapter (the ceremony)\n"
"  khatm pace [book]             burndown, ETA, required pace\n"
"  khatm shelf                   every book you have ever sealed\n"
"  khatm graph [weeks]           session heatmap\n"
"  khatm doctor                  cycles, stale promises, estimate bias\n"
"\n"
"  machine interface (for GUIs and scripts):\n"
"  khatm dump                    full state as JSON (api version 1)\n"
"  khatm <log|done|goal> --json  mutations emit JSON results; errors are\n"
"                                {\"ok\":false,...} with a nonzero exit code\n"
"\n"
"  chapters: ostep/3, ostep/pointers, or a bare title substring\n"
"  dates: 2026-07-01, friday, tomorrow, +5d, +2w\n"
"  data: $KHATM_DIR or ~/.khatm — plain markdown + an append-only log\n");
}

int main(int argc, char **argv) {
    g_color = plat_console_init() && !getenv("NO_COLOR");

    State st = {0};
    st.now = time(NULL);

    const char *env = getenv("KHATM_DIR");
    char rootbuf[512];
    if (env && *env) {
        snprintf(rootbuf, sizeof rootbuf, "%s", env);
    } else {
        snprintf(rootbuf, sizeof rootbuf, "%s/.khatm", plat_home());
    }
    st.root = rootbuf;

    int ac2 = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) g_json = 1;
        else argv[ac2++] = argv[i];
    }
    argc = ac2;

    const char *cmd = argc < 2 ? "tui" : argv[1];
    int ac = argc < 2 ? 0 : argc - 2;
    char **av = argc < 2 ? argv : argv + 2;

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 ||
        strcmp(cmd, "-h") == 0) {
        usage();
        return 0;
    }
    if (strcmp(cmd, "init") == 0)
        return cmd_init(&st, ac, av);

    int want_tui = strcmp(cmd, "tui") == 0;
    if (want_tui && !(isatty(0) && isatty(1))) {
        usage();
        return 0;
    }

    if (syl_load(&st)) {
        if (!want_tui) {
            fprintf(stderr,
                    "khatm: no data at %s — run `khatm init` first\n",
                    st.root);
            return 1;
        }
        printf("khatm: no data at %s yet.\n"
               "set it up now with a sample syllabus? [Y/n] ", st.root);
        fflush(stdout);
        char ans[16] = "";
        if (!fgets(ans, sizeof ans, stdin) ||
            ans[0] == 'n' || ans[0] == 'N')
            return 0;
        if (cmd_init(&st, 0, NULL)) return 1;
        if (syl_load(&st)) return 1;
    }
    syl_resolve_all(&st, NULL);
    log_load(&st);
    goals_refresh(&st);

    if (want_tui) return tui_run(&st);

    if (strcmp(cmd, "dump") == 0)   return cmd_dump(&st, ac, av);
    if (g_json && (strcmp(cmd, "books") == 0 || strcmp(cmd, "status") == 0 ||
                   strcmp(cmd, "next") == 0 || strcmp(cmd, "pace") == 0 ||
                   strcmp(cmd, "shelf") == 0 || strcmp(cmd, "graph") == 0))
        return cmd_dump(&st, 0, NULL);

    if (strcmp(cmd, "book") == 0)   return cmd_book(&st, ac, av);
    if (strcmp(cmd, "books") == 0)  return cmd_books(&st, ac, av);
    if (strcmp(cmd, "status") == 0) return cmd_status(&st, ac, av);
    if (strcmp(cmd, "next") == 0)   return cmd_next(&st, ac, av);
    if (strcmp(cmd, "log") == 0)    return cmd_log(&st, ac, av);
    if (strcmp(cmd, "study") == 0)  return cmd_study(&st, ac, av);
    if (strcmp(cmd, "goal") == 0)   return cmd_goal(&st, ac, av);
    if (strcmp(cmd, "done") == 0)   return cmd_done(&st, ac, av);
    if (strcmp(cmd, "pace") == 0)   return cmd_pace(&st, ac, av);
    if (strcmp(cmd, "shelf") == 0)  return cmd_shelf(&st, ac, av);
    if (strcmp(cmd, "graph") == 0)  return cmd_graph(&st, ac, av);
    if (strcmp(cmd, "doctor") == 0) return cmd_doctor(&st, ac, av);

    fprintf(stderr, "khatm: unknown command \"%s\" (khatm help)\n", cmd);
    return 1;
}
