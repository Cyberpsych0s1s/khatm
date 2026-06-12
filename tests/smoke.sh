#!/usr/bin/env bash
# smoke.sh — end-to-end exercise of every khatm command against a temp dir.
set -u
BIN="${1:-./khatm}"
# KHATM_TEST_DIR overrides the temp dir — pass a relative path to smoke-test
# the Windows binary from WSL or MSYS2 (a native .exe can't see /tmp).
if [ -n "${KHATM_TEST_DIR:-}" ]; then
    DIR="$KHATM_TEST_DIR"; rm -rf "$DIR"; mkdir -p "$DIR"
else
    DIR="$(mktemp -d)"
fi
export KHATM_DIR="$DIR"
export NO_COLOR=1
export WSLENV="KHATM_DIR:NO_COLOR${WSLENV:+:$WSLENV}"
fails=0

run() { # run <desc> <expected-substring> -- <args...>
    local desc="$1" want="$2"; shift 2
    [ "$1" = "--" ] && shift
    local out
    # stdin from /dev/null: interactive confirmations must never block here
    out="$("$BIN" "$@" </dev/null 2>&1)"
    if printf '%s' "$out" | grep -qF -- "$want"; then
        echo "ok   $desc"
    else
        echo "FAIL $desc"
        echo "     wanted: $want"
        printf '%s\n' "$out" | sed 's/^/     got: /' | head -6
        fails=$((fails+1))
    fi
}

run "init creates root"        "khatm root ready"      -- init
run "sample book listed"       "knr"                   -- books
run "next picks chapter 1"     "knr/1"                 -- next
run "goal is priced"           "required pace"         -- goal knr/1 --by +3d
run "bad goal date rejected"   "bad date"              -- goal knr/1 --by 2026-06-31
run "trailing date rejected"   "bad date"              -- goal knr/1 --by 2026-07-01junk
run "log records session"      "logged 45 min"         -- log knr/1 45m --pages 18
run "negative pages rejected"  '"ok":false'            -- log knr/1 10m --pages -5 --json
run "bad pages rejected"       "bad pages"             -- log knr/1 10m --pages nope
run "unknown log flag rejected" "unknown option"        -- log knr/1 10m --wat
run "fuzzy title resolves"     "logged 30 min"         -- log tutorial 30m --pages 12
run "book+number resolves"     "logged 20 min"         -- log knr2 20m
run "book prefix resolves"     "remaining"             -- pace kn
run "done seals + keeps goal"  "goal kept"             -- done knr/1
run "done is idempotent"       "already sealed"        -- done knr/1
run "kept rate shows"          "your word, kept: 100%" -- goal
run "goal prices prerequisites" "unsealed prerequisites" -- goal knr/8 --by +10d
run "status totals promises"   "all promises"          -- status
run "status renders"           "study now"             -- status
run "pace forecasts"           "remaining"             -- pace knr
run "graph renders"            "minutes/day"           -- graph 4
run "shelf starts empty-ish"   "your shelf"            -- shelf
run "doctor notices no deadline" "no deadline"         -- doctor
run "doctor flags at-risk promise" "at-risk promise"   -- doctor
cat > "$DIR/books/partial.md" <<'MD'
# Partial Needs

- [ ] A
- [ ] B [needs: A, Missing]
MD
run "doctor catches partial bad need" "unresolved needs \"Missing\"" -- doctor
rm -f "$DIR/books/partial.md"
"$BIN" goal knr/2 --by +5d </dev/null >/dev/null
run "drop is judgment-free"    "never counts"          -- goal --drop knr/2

# deadlines propagate backwards through needs edges
cat > "$DIR/books/dep.md" <<'MD'
# Dep Chain

- [ ] First
- [ ] Second [needs: 1]
MD
"$BIN" goal dep/2 --by +2d </dev/null >/dev/null 2>&1
run "deadline propagates to prereq" "blocks work due"  -- next
"$BIN" goal --drop dep/2 </dev/null >/dev/null 2>&1
rm -f "$DIR/books/dep.md"

# book creation without an editor
run "book new creates a syllabus" "now add chapters"    -- book new algo "Algorithms Illuminated" --pages 200
run "book add appends a chapter"  "algo/1"              -- book add algo "Sorting" --est 12p
run "book section opens"          "section \"Graphs\""  -- book section algo "Graphs"
run "book add with needs"         "algo/2"              -- book add algo "BFS" --needs 1
run "new book is studyable"       "logged 5 min"        -- log algo1 5m
run "dup book id rejected"        "already exists"      -- book new knr whatever
run "weird book id rejected"      "book id"             -- book new "we!rd" x
run "bad estimate rejected"       "bad estimate"        -- book add algo "Z" --est nope
run "book add survives reload"    "Sorting"             -- books --json

# pomodoro (stdin is /dev/null: ends immediately, logs the 1-min floor)
run "pomodoro logs focus time"    "logged 1 min"        -- study algo/1 --pomo 1/1
run "bad pomodoro spec rejected"  "pomodoro spec"       -- study algo/1 --pomo 0/0

# blocked chapters stay blocked: 4 needs section Basics (1-3)
out="$("$BIN" next 2>&1)"
if printf '%s' "$out" | grep -q "knr/4"; then
    echo "FAIL deps gate next (knr/4 offered while Basics unsealed)"
    fails=$((fails+1))
else
    echo "ok   deps gate next"
fi

# seal everything -> khatma + shelf spine
for c in 2 3 4 5 6 7; do "$BIN" done "knr/$c" -q >/dev/null 2>&1; done
run "khatma fires on last seal" "K H A T M A"          -- done knr/8
run "shelf shows the spine"    "1 book sealed"         -- shelf

# state survives reload (fold of the log)
run "state persists"           "8/8"                   -- books

# machine interface
run "dump emits api v1"        '"api":1'               -- dump
run "dump carries books"       '"id":"knr"'            -- dump
run "json session result"      '"event":"session"'     -- log knr/1 5m --json
run "json done idempotent"     '"already_sealed":true' -- done knr/1 --json
run "json read = dump"         '"api":1'               -- books --json
out="$("$BIN" done nosuch/9 --json 2>/dev/null)"; rc=$?
if [ "$rc" -ne 0 ] && printf '%s' "$out" | grep -q '"ok":false'; then
    echo "ok   json error shape + exit code"
else
    echo "FAIL json error shape + exit code (rc=$rc)"
    fails=$((fails+1))
fi
if command -v python3 >/dev/null 2>&1; then
    if "$BIN" dump | python3 -m json.tool >/dev/null 2>&1; then
        echo "ok   dump parses as strict JSON"
    else
        echo "FAIL dump parses as strict JSON"
        fails=$((fails+1))
    fi
fi

# bare invocation without a tty must print usage, never hang
out="$("$BIN" < /dev/null 2>&1)"
if printf '%s' "$out" | grep -q "build system for your studying"; then
    echo "ok   non-tty bare run falls back to usage"
else
    echo "FAIL non-tty bare run falls back to usage"
    fails=$((fails+1))
fi

echo
if [ "$fails" -eq 0 ]; then echo "all smoke tests passed"; else echo "$fails FAILED"; fi
rm -rf "$DIR"
exit "$fails"
