#!/usr/bin/env bash
set -e
K="${KHATM_BIN:-./khatm}"

"$K" init >/dev/null

# id, "title", pages, then chapter titles; logs + seals every chapter.
seal_book() {
    local id=$1 title=$2 pages=$3; shift 3
    "$K" book new "$id" "$title" --pages "$pages" >/dev/null
    local i=1
    for c in "$@"; do
        "$K" book add "$id" "$c" --est 20p >/dev/null
        "$K" log "$id/$i" 40m --pages 20 >/dev/null
        "$K" done "$id/$i" -q >/dev/null
        i=$((i + 1))
    done
}

# two finished books, already on the shelf
seal_book kr   "The C Programming Language"   272 Tutorial Types Pointers
seal_book sicp "Structure & Interpretation"   657 Procedures Abstraction

# the star: every chapter but the last sealed; the last one studied and
# waiting, so `khatm done ostep/7` completes the book and fires the khatma.
"$K" book new ostep "Operating Systems: Three Easy Pieces" --pages 600 >/dev/null
for c in Virtualization Scheduling Memory Paging Locks Condition Persistence; do
    "$K" book add ostep "$c" --est 30p >/dev/null
done
for i in 1 2 3 4 5 6; do
    "$K" log  "ostep/$i" 50m --pages 30 >/dev/null
    "$K" done "ostep/$i" -q >/dev/null
done
"$K" log ostep/7 55m --pages 32 >/dev/null
