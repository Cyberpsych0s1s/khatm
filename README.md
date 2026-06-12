# khatm (ختم)

A build system for your studying. Portable C99 (Linux, macOS, Windows),
single binary, zero runtime dependencies.

Your syllabus is source code, goals are build targets, and `khatm` is `make`:
it always knows what you should study next, whether you're on pace, and it
pays out the dopamine at the moments that matter — setting a goal, keeping a
goal, and sealing a chapter. See [IDEA.md](IDEA.md) for the full design and
prior-art audit.

## Build

Linux and macOS:

```sh
make            # produces ./khatm
make test       # end-to-end smoke suite
```

Windows, native (MSYS2 UCRT64 or any MinGW-w64 gcc):

```sh
make CC=gcc     # produces ./khatm.exe
make CC=gcc test
```

Everything OS-specific lives in `src/plat.c` (POSIX termios/poll on one
side, the Win32 console API on the other); the rest of the code is shared.
The TUI needs a console that renders ANSI escapes — Windows Terminal or
any Windows 10+ conhost qualifies. On an older console the TUI declines
to start, but every CLI command still works (without color). Data lives
in `%USERPROFILE%\.khatm` on Windows, `~/.khatm` elsewhere; `KHATM_DIR`
overrides it everywhere.

## Quick start

```sh
./khatm                         # the TUI (default in a terminal)
```

First run offers to set everything up with a sample syllabus. Inside:
`1-4` switch views (home · books · goals · stats), `↑↓/jk` move, `enter`
opens a book or starts a timed session (live block-digit clock), `g`
promises a chapter or book by a date — priced before you commit — `d`
seals a chapter (the ceremony) or drops a goal, `?` help, `q` quit.
The palette is a muted manuscript theme (gold seals, teal ink, sage);
`NO_COLOR` is honored.

Every command also works headless, for scripts and quick one-liners:

```sh
./khatm init                    # data dir (default ~/.khatm, or $KHATM_DIR)
$EDITOR ~/.khatm/books/ostep.md # write a syllabus (sample provided)
./khatm next                    # what to study, and why
./khatm study ostep/1           # timed session (or: khatm log ostep/1 45m --pages 12)
./khatm goal ostep/3 --by friday# a promise, priced before you make it
./khatm done ostep/1            # seal the chapter (the ceremony)
./khatm status                  # today, streak, promises, kept-rate
./khatm pace ostep              # burndown, ETA, required pace
./khatm shelf                   # every book you have ever sealed
./khatm graph                   # session heatmap
./khatm doctor                  # cycles, stale promises, estimate bias
```

## Syllabus format

One markdown file per book in `<root>/books/`. khatm never rewrites it.

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

- `~18p` / `~2h` / `~90m` — optional estimates (khatm reports your bias).
- `[needs: ...]` — prerequisites: a chapter number, a section title, a title
  substring, or `book/ref` across books. Sections pass their needs to every
  chapter inside.
- Checking `[x]` by hand counts as done; `khatm done` is better (it's dated).

All state khatm writes is one append-only, greppable log: `<root>/log.txt`.
State is a pure fold of (syllabi, log, clock) — back it up or sync it with git.

## Integrating khatm into other programs

khatm doubles as a backend (api version 1). The pattern for a GUI:
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

## The reward rules

Every reward is triggered by a real study event; there is nothing to grind.

- **Seals** — finishing a chapter stamps it, with real numbers (actual vs.
  estimate, what it unblocked). Finishing a whole book is a **khatma** and
  puts its spine on your shelf, forever.
- **Your word, kept** — the % of self-set goals met on time, shown whenever
  you set a new one. Goals are priced (required pages/day vs. your actual
  4-week pace) *before* you promise. Dropping a goal never counts against you.
- **Streaks, heatmap, records** — announced only when the real numbers are new.
