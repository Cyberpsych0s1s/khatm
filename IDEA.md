# khatm (ختم) — a build system for your studying

> *khatm* is Arabic for "seal / completion." Classical manuscripts ended with a
> seal; finishing a full reading is a *khatma*. In this tool, every chapter you
> finish earns its seal. No game, no pets, no dungeons — a pure CLI study tool
> whose reward structure is built from goals kept and chapters sealed.

A **C99, single-binary, plain-text study tool for chapter-based learning**
(textbooks, course units, lecture series). The organizing metaphor is the one
programmers already trust: **your syllabus is source code, goals are build
targets, and the tool is `make`** — it always knows what you should study next,
whether you're on pace, and it pays out the dopamine at the moments that matter:
setting a goal, keeping a goal, and sealing a chapter.

---

## 1. The one-line pitch

`make` for studying: declare your books as chapter trees with dependencies and
deadlines, log study sessions against them, and get deterministic answers to
"what should I study right now?" and "will I finish in time?" — with a reward
architecture (seals, kept-goal score, shelf, streaks) wired to real completions
instead of points.

## 2. Why this is novel (prior-art audit, 2026-06-11)

Each design iteration was checked against existing projects:

| Project | What it is | Why khatm is different |
|---|---|---|
| [libro](https://github.com/mkaz/libro), [br_tracker](https://github.com/valbrux/br_tracker), [hardcover-tui](https://github.com/NotMugil/hardcover-tui) | CLI/TUI book trackers | Log *what you read*; no goals, no pace forecasting, no reward design, no chapter dependencies |
| [Beeminder](https://www.beeminder.com/) | Commitment device with pace tracking ("yellow brick road") | Closest conceptual relative — but SaaS, punishment-based (charges your card), generic data points, not chapter-structured, not local, not CLI-native |
| [PlannerCLI](https://github.com/parsany/PlannerCLI) / [study_planner](https://github.com/payrim/study_planner) | Python study planners (kanban, schedule, progress bars) | Kitchen-sink organizers; no dependency engine, no forecasting, no completion architecture; Python scripts, not a tool |
| [Taskwarrior](https://github.com/GothenburgBitFactory/taskwarrior) + [Taskquant](https://dev.to/onlyphantom/todo-gamification-with-taskwarrior-taskquant-3e38) | CLI tasks + score overlay | Tasks are flat and generic; scores are decorative; no notion of structured material, pace, or estimates-vs-actuals |
| [curriculum-parser](https://github.com/Laboratoria/curriculum-parser), [syllabus templates](https://github.com/Make-School-Courses/Syllabus-Template), [LiaScript](https://liascript.github.io/) | Markdown syllabus tooling | For *publishing* course content, not tracking a person's progress through it |
| [bookshelf](https://github.com/trsav/bookshelf) | CLI book organizer w/ ML embeddings | Library management; its "shelf" is a catalog, not a trophy render of completed study |
| `make` graph tools ([makefile-graph](https://github.com/dnaeon/makefile-graph), [makefile2graph](https://github.com/lindenb/makefile2graph)) | Build-graph visualizers | Confirm the metaphor is untaken: nobody applies target/dependency resolution to study material |

**The gap:** progress *trackers* exist and commitment *devices* exist, but no local
CLI tool unifies (a) structured chapter-based material with dependencies,
(b) make-style "what's next" resolution, (c) pace forecasting against self-set
deadlines, and (d) a reward layer triggered only by real completions and kept
promises. And none of it exists in C as a dependency-free single binary.

## 3. Syllabus-as-code

One markdown file per book/course, hand-written, git-able (khatm never rewrites it):

```markdown
# Operating Systems: Three Easy Pieces
meta: pages=650 deadline=2026-08-30

## 1. Virtualization
- [ ] 4. Processes                    (~18p)
- [ ] 5. Process API                  (~14p)
- [ ] 6. Limited Direct Execution     (~16p)

## 2. Concurrency                     needs: 1
- [ ] 26. Concurrency Intro           (~12p)
- [ ] 27. Thread API                  (~10p)
```

- Chapters/sections form a tree; `needs:` declares prerequisites — within a book
  or across books (`needs: linear-algebra/ch2`), like make targets.
- Estimates are optional (`~18p` or `~2h`); khatm learns your actual pace per
  book from session logs and stops trusting bad estimates.
- All state khatm itself writes (sessions, goals, completions) lives in an
  append-only log next to the syllabi. Plain text all the way down.

## 4. The four commands that matter

```
khatm next                      # the make moment: deadline- and dependency-aware
                                #   "study this now", with why ("ch5 blocks 3 chapters,
                                #   goal due Friday, ~40 min at your pace")
khatm study os/ch5              # start a session (timer; Ctrl-D to stop), or
khatm log os/ch5 35m --pages 9  # log one after the fact
khatm goal os/ch6 --by friday   # set a goal — khatm immediately tells you the
                                #   required pace and whether it conflicts with
                                #   your other goals
khatm done os/ch5               # seal a chapter → the ceremony
```

Plus the dashboards: `khatm status` (today, one screen), `khatm pace <book>`
(burndown + ETA), `khatm shelf`, `khatm graph` (heatmap), `khatm doctor`
(stale goals, circular deps, orphaned sessions).

## 5. The reward architecture (dopamine without a game)

Every reward is triggered by a real study event — there is nothing to grind.

1. **Seals.** `khatm done` runs a completion ceremony: an ANSI seal is stamped
   on the chapter (brief, satisfying, skippable with `-q`) followed by the
   numbers that feel good *because they're real*: actual vs. estimated time,
   pace vs. your average, what it unblocked, days ahead of deadline. Finishing
   the last chapter of a book is a **khatma** — the full-book ceremony — and the
   book's spine appears on `khatm shelf`, the persistent ASCII bookshelf of
   everything you've ever sealed. The shelf is the long-term trophy case: it
   only ever grows.

2. **The kept-word score.** Goals in khatm are promises to yourself. The tool
   tracks your **kept rate** — the % of self-set goals met on time — and shows
   it whenever you set a new goal ("your word, kept: 81% over 43 goals").
   This is the behavioral core: it makes goal-setting itself rewarding,
   protects you from overcommitting (khatm warns when a new goal's required
   pace collides with existing goals — *before* you promise), and builds the
   self-trust loop that habit research says actually sustains studying.
   Missed goals are never punished — they just don't count as kept. Rewards
   only, explicitly anti-Beeminder.

3. **Goal-gradient mechanics.** Progress bars are per-chapter and per-book, and
   khatm surfaces near-completions first ("ch7 is 85% done — 20 minutes seals
   it"). The goal-gradient effect (effort accelerates near a finish line) is
   the cheapest honest dopamine there is; `khatm next` deliberately offers
   closeable loops before new ones.

4. **Streaks + records.** GitHub-style heatmap (`khatm graph`), current/best
   streak, and personal-record detection on real metrics (biggest week, fastest
   chapter vs. estimate, most consecutive kept goals). Records are announced at
   session end — a variable-interval surprise that costs nothing and can't be
   gamed, because it only fires when the underlying numbers are genuinely new.

## 6. The pace engine (the analytics that make it a *tool*)

- **Burndown per book**: pages/sections remaining vs. the line you must hold to
  hit the deadline (Beeminder's road, local and reward-framed).
- **ETA forecasting**: "at your 4-week velocity, you finish 2026-09-12 — 13 days
  late. Holding 11 pages/day fixes it." Updated after every session.
- **Estimate calibration**: khatm compares your `~18p`/`~2h` estimates with
  actuals per book and reports your bias ("you under-estimate OSTEP chapters by
  ~30%") — so planning itself improves over time.
- **Dependency-aware schedule**: deadlines propagate backwards through `needs:`
  edges (if ch10 is due Friday and needs ch9, ch9 inherits an earlier implied
  deadline — exactly how build systems schedule).

## 7. Architecture sketch (C99, single binary, zero deps)

```
src/
  base/    arena, vec, map, str           (allocator-first; the digon toolkit pattern)
  syl/     syllabus markdown parser, chapter ids (content-hash, survive renames)
  log/     append-only event log; state = fold(log)   (crash-safe, git-syncable)
  plan/    dep graph, topo sort, deadline propagation, `next` resolver
  pace/    velocity model, burndown, ETA, estimate calibration
  ui/      ANSI render: status, bars, heatmap, shelf, seal ceremony (termios; no ncurses)
  cli/     next/study/log/goal/done/status/pace/shelf/graph/doctor
```

- **State = fold over an append-only log**: every session/goal/completion is one
  line; the entire tool state is a pure function of (syllabi, log, clock).
  Trivial to back up, sync via git, or inspect with grep.
- The planner is a genuinely interesting C project: a small dependency solver
  (topological sort + deadline propagation + a "next" heuristic), a calibration
  model, and a terminal renderer — meaty, but every piece is testable and finite.
- Portable: Linux/macOS/WSL; Windows Terminal handles VT sequences.

## 8. MVP slices (each independently useful)

1. **Track**: syllabus parser + session log + `status`/`log`/`done` with plain
   progress bars. (Already beats br_tracker for chapter-based study.)
2. **Plan**: dep graph + `next` + `goal` with required-pace math.
3. **Pace**: burndown, ETA, kept-rate. (The Beeminder-class utility, local.)
4. **Reward**: seal ceremony, shelf, heatmap, records.
5. **Calibrate**: estimate-bias reporting, `doctor`, cross-book deps.

## 9. Risks / open questions

- **Naming**: "khatma" apps exist in the Quran-reading mobile space (different
  niche); no CLI/dev-tool collision found for `khatm` as of 2026-06-11. The seal
  metaphor is the brand — worth keeping even if the binary name shifts.
- **Logging friction is the death of trackers**: mitigate with `khatm study`
  (timer, zero math), fuzzy chapter matching (`khatm log os5` → `os/ch5`), and
  one-line after-the-fact logging. Target: logging a session ≤ 5 seconds.
- **Ceremony fatigue**: celebrations must stay short (<2s), skippable, and rare
  enough to stay meaningful — the seal is for *chapters*, not every session.
- **Estimate-free decks**: everything must degrade gracefully when the user
  declares no page counts/estimates (fall back to section-count progress).
