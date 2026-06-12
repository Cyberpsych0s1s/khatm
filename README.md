# khatm (ختم)

A little terminal app for tracking your reading/studying. You describe each
book as a markdown checklist, and khatm keeps track of what's done, what's
next, how fast you're actually going, and whether you're on track for any
deadlines you've set yourself.

## Build

Linux and macOS:

```sh
make            # produces ./khatm
make test       # end-to-end smoke suite
make install    # to /usr/local/bin (PREFIX=~/.local for per-user)
```

Windows, native (MSYS2 UCRT64 or any MinGW-w64 gcc):

```sh
make CC=gcc     # produces ./khatm.exe
make CC=gcc test
make CC=gcc install
```

`make install` adapts to where it runs: a POSIX system gets
`$(PREFIX)/bin` (honoring `DESTDIR` for packaging), an MSYS2/Git-Bash
shell gets its own `/usr/local/bin`, and native `mingw32-make` under
cmd.exe copies to `%LOCALAPPDATA%\Programs\khatm` (per-user, no admin —
add that folder to PATH once). `make uninstall` undoes it.

Everything OS-specific lives in `src/plat.c` (POSIX termios/poll on one
side, the Win32 console API on the other); the rest of the code is shared.
The TUI needs a console that renders ANSI escapes — Windows Terminal or
any Windows 10+ conhost is fine. On an older console the TUI won't start,
but every CLI command still works (without color). Data lives in
`%USERPROFILE%\.khatm` on Windows, `~/.khatm` elsewhere; `KHATM_DIR`
overrides it everywhere.

## Quick start

```sh
./khatm                         # the TUI (default in a terminal)
```

First run offers to set things up with a sample syllabus. Inside:
`1-4` switch views (home · books · goals · stats), `↑↓/jk` move, `enter`
opens a book or starts a timed session, `p` starts a pomodoro session
(25 min focus + 5 min breaks; only focus time is logged), `g` sets a goal
for a chapter or book by a date, `d` marks a chapter done or drops a goal,
`n`/`a` create a book / add a chapter without leaving the TUI, `?` help,
`q` quit. `NO_COLOR` is honored.

Every command also works headless, for scripts and quick one-liners:

```sh
./khatm init                    # data dir (default ~/.khatm, or $KHATM_DIR)
$EDITOR ~/.khatm/books/ostep.md # write a syllabus (sample provided)…
./khatm book new ostep "OSTEP"  # …or let khatm write it for you
./khatm book add ostep "Processes" --est 18p   # append a chapter
./khatm next                    # suggests what to study, and why
./khatm study ostep/1           # timed session (or: khatm log ostep/1 45m --pages 12)
./khatm study ostep/1 --pomo 25/5   # pomodoro: breaks aren't logged
./khatm goal ostep/3 --by friday# set a deadline for yourself
./khatm done ostep/1            # mark the chapter done
./khatm status                  # today, streak, goals, kept-rate
./khatm pace ostep              # burndown, ETA, required pace
./khatm shelf                   # finished books
./khatm graph                   # session heatmap
./khatm doctor                  # cycles, stale goals, estimate bias
```

## Syllabus format

One markdown file per book in `<root>/books/`. khatm never rewrites what
you wrote — its own `book new`/`book add`/`book section` commands only
create files or append lines, so hand-editing stays safe.

```markdown
# Operating Systems: Three Easy Pieces
meta: deadline=2026-08-30 pages=650

## Virtualization
- [ ] Processes ~18p
- [ ] Process API ~14p

## Concurrency [needs: Virtualization]
- [ ] Concurrency Intro ~12p
- [ ] Thread API ~10p [needs: 1, otherbook/3]
```

- `~18p` / `~2h` / `~90m` — optional estimates (khatm reports how far off
  your estimates tend to be).
- `[needs: ...]` — prerequisites: a chapter number, a section title, a title
  substring, or `book/ref` across books. Sections pass their needs to every
  chapter inside.
- Checking `[x]` by hand counts as done; `khatm done` is better (it's dated).

All state khatm writes is one append-only, greppable log: `<root>/log.txt`.
State is rebuilt from (syllabi, log, clock) on every load — back it up or
sync it with git.

## Using khatm from other programs

khatm can act as a backend (api version 1). The pattern for a GUI:
render from `dump`, call mutations with `--json`, re-`dump`.

```sh
khatm dump                          # the read API: full state as one JSON
                                    #   object — stats (streak, kept-rate,
                                    #   velocity), suggestions with reasons,
                                    #   books → chapters (status: sealed/
                                    #   ready/in_progress/blocked, progress,
                                    #   needs), and every goal
khatm log ostep/3 25m --pages 9 --json    # {"ok":true,"event":"session",...}
khatm done ostep/3 --json                 # {"ok":true,"unblocked":2,
                                          #  "goal_kept":true,"khatma":false}
khatm goal ostep/4 --by +5d --json        # {"ok":true,"need_per_day":3.6,...}
khatm goal --drop ostep/4 --json          # {"ok":true,"event":"drop-goal",...}
```

Contract: JSON on stdout; exit 0 on success; on failure exit nonzero with
`{"ok":false,"error":"..."}` (human detail may accompany it on stderr).
Any read command with `--json` (`books`, `status`, `next`, `pace`, …)
answers with the full dump, so there is exactly one shape to parse.
Chapter refs are stable strings (`bookid/N`, file order, 1-based).
Dates must be exact (`YYYY-MM-DD`, `today`, `tomorrow`, weekday names, `+Nd`,
or `+Nw`); invalid dates and trailing junk are rejected. Page counts are
non-negative finite numbers.

Two lower-level integration points, since all state is plain text: a
program may append well-formed lines to `<root>/log.txt` directly (khatm
folds them on next load), and the syllabi in `<root>/books/*.md` can be
generated by other tools. `done`/`goal`/`drop` are idempotent or safely
re-runnable, and `dump` is cheap enough to call after every mutation.

## Progress and goals

A few small things khatm does to make progress visible:

- Finishing a chapter records the actual time vs. your estimate and what it
  unblocked. Finishing a whole book is a 'khatma' and adds it to your shelf.
- Goals show your kept-rate (% of self-set deadlines met on time), and before
  you set one, khatm shows the required pages/day next to your actual 4-week
  pace — so you know what you're signing up for. Dropping a goal doesn't
  count against you.
- Streaks, a session heatmap, and personal records, mentioned only when
  they actually change.
