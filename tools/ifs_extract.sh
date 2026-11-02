#!/bin/sh
# Extract a QNX IFS image into a directory tree.
#
# WHY THIS SCRIPT EXISTS
#   `dumpifs -x <image>` extracts into the *current directory* and does not
#   create parent directories: the first run dies with
#       dumpifs: Unable to open proc/boot/procnto-smp-instr: No such file or directory
#   and extracting the full 2.1.0.1917 image needs five such rounds (72 files,
#   11 directories).  This loops: run dumpifs, create whatever directory it
#   complains about, run it again, until it stops complaining.
#
# USAGE
#   tools/ifs_extract.sh <image.ifs> <outdir>
#
# NEEDS
#   dumpifs on PATH, i.e. `. tools/env.sh` first (the QNX 6.6 SDP in
#   local/toolchains/qnx660-master).  The script sources it itself if present.
#
# NOTE
#   The tree it writes is what the image builder (tools/ifs_build.py, not yet
#   written) consumes.  Modes and symlinks are preserved by dumpifs; uid/gid are
#   not (you are not root), and mtimes come from the image.
set -e
[ $# -eq 2 ] || { echo "usage: $0 <image.ifs> <outdir>" >&2; exit 2; }

img=$1
out=$2
[ -f "$img" ] || { echo "ifs_extract: no such image: $img" >&2; exit 1; }
img=$(CDPATH= cd -- "$(dirname -- "$img")" && pwd)/$(basename -- "$img")

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
[ -f "$ROOT/tools/env.sh" ] && . "$ROOT/tools/env.sh" >/dev/null 2>&1 || true
command -v dumpifs >/dev/null 2>&1 || {
    echo "ifs_extract: dumpifs not on PATH - run '. tools/env.sh' first" >&2
    exit 1
}

mkdir -p "$out"
cd "$out"

i=0
while [ "$i" -lt 200 ]; do
    i=$((i + 1))
    msg=$(dumpifs -x "$img" 2>&1) || true
    miss=$(printf '%s\n' "$msg" |
           sed -n 's/^dumpifs: Unable to open \(.*\): No such file or directory$/\1/p' |
           head -1)
    if [ -n "$miss" ]; then
        mkdir -p "$(dirname "$miss")"
        continue
    fi
    printf '%s\n' "$msg" | grep -q . && printf '%s\n' "$msg" >&2
    break
done

printf 'ifs_extract: %s files, %s directories, %s bytes (in %s)\n' \
    "$(find . -type f | wc -l)" \
    "$(find . -type d | wc -l)" \
    "$(du -sb . | cut -f1)" \
    "$out"
