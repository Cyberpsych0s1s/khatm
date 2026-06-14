<h1 align="center">khatm (ختم)</h1>

<p align="center"><em>seal your books, chapter by chapter</em></p>

<p align="center">
  <img alt="runs on linux, macOS, windows" src="https://img.shields.io/badge/runs_on-linux_·_macos_·_windows-126e62?style=flat-square">
  <img alt="TUI, CLI and JSON interfaces" src="https://img.shields.io/badge/interface-TUI_·_CLI_·_JSON-c9a227?style=flat-square">
</p>

A little terminal app for tracking your reading/studying. Each book is a
markdown checklist; khatm tracks what's done, what's next, how fast you're
actually going, and whether you'll hit the deadlines you set yourself.
Finish a whole book and that's a khatma, it goes on your shelf.

## Preview

![sealing the last chapter of a book — the khatma](assets/demo.gif)

<sub>Reproducible: `vhs assets/demo/demo.tape` re-records this GIF (see
[`assets/demo/`](assets/demo)).</sub>

<details><summary>Full TUI walkthrough (video)</summary>

https://github.com/user-attachments/assets/8e32cca6-0a04-4036-9cd3-61aec204e670

</details>

## Build

Linux and macOS:

```sh
make            # produces ./khatm
make test       # smoke suite
make install    # /usr/local/bin + man page (PREFIX=~/.local for per-user)
```

Shell completions live in [`completions/`](completions) (bash, zsh, fish) —
source the one for your shell, or drop it in the usual place
(`/etc/bash_completion.d/`, your `$fpath`, `~/.config/fish/completions/`).
`man khatm` works after `make install`.

Windows, native (MSYS2 UCRT64 or any MinGW-w64 gcc): same thing with
`make CC=gcc`.

## Use

```sh
./khatm
```

That's the TUI. First run offers to set things up with a sample syllabus,
and `?` shows the keys. I would personally recommend the TUI since writing
everything manually in the terminal is hell...

...but every command works headless too, `khatm -h` lists them all:

```sh
./khatm book new ostep "OSTEP"  # start a syllabus
./khatm edit ostep              # open the book's .md in $EDITOR
./khatm next                    # what to study, and why
./khatm study ostep/1           # timed session
./khatm done ostep/1            # seal the chapter (plays the ceremony)
```

Sealing plays a short seal-stamp ceremony; the last chapter of a book blooms
into the khatma and slides onto your shelf. `KHATM_ANIM=0` turns it off, and
`KHATM_ROMAN=1` swaps the Arabic mark for a roman one if your font mangles it.

## Syllabus format

One markdown file per book in `<root>/books/` (default `~/.khatm`, or
`$KHATM_DIR`). khatm only creates files or appends lines, it never rewrites
what you wrote, so hand-editing stays safe.

```markdown
# Operating Systems: Three Easy Pieces
meta: deadline=2026-08-30 pages=650

## Virtualization
- [ ] Processes ~18p
  ? what does fork() return :: 0 in the child, the child pid in the parent
- [ ] Process API ~14p

## Concurrency [needs: Virtualization]
- [ ] Concurrency Intro ~12p
- [ ] Thread API ~10p [needs: 1, otherbook/3]
```

- `~18p` / `~2h` / `~90m` are optional estimates; khatm tells you how far
  off your estimates tend to run.
- `[needs: ...]` are prerequisites: a chapter number, a section title, a
  title substring, or `book/ref` across books. Sections pass their needs to
  every chapter inside.
- `? front :: back` lines under a chapter are spaced-review cards (see below).
- Checking `[x]` by hand counts as done; `khatm done` is better (it's dated).

## Spaced review

Drop `? front :: back` lines under any chapter and they become flashcards.
A card unlocks for review only once you **seal its chapter** — you revise
what you've actually finished. Reviews are scheduled with SM-2.

```sh
khatm review           # grade due cards: [a]gain [h]ard [g]ood [e]asy
khatm cards            # every card and when it next falls due
```

The schedule lives entirely in the append-only log (`review` events keyed by
a hash of the card front), so the `.md` is never rewritten — reorder or
re-indent cards freely and their history follows.

Everything khatm writes goes to one append-only log, `<root>/log.txt`.
State is rebuilt from plain text on every load, so grep it, back it up,
sync it with git, whatever.

## Scripting

`khatm dump` prints the full state as one JSON object, and mutations take
`--json` and answer with `{"ok":...}` (nonzero exit and `{"ok":false}` on
failure). Render from dump, mutate with `--json`, dump again; that's enough
to build a GUI on. Details in `khatm -h`.

## Omar Notes
This is a bit of a nitpick, but the "ختم" in the top left of the tui looks a bit broken.
I was unable to solve this programatically for all fonts, so I believe it'd only look correct
if you use an arabic-supported font. I haven't tested one yet so I'm not 100% sure, for now it's good enough.
