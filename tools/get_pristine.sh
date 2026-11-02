#!/bin/sh
# Extract the pristine Linux 6.15.11 tree from the repository payload.
#
# The project must not depend on a pristine copy that lives outside this
# workspace (the old recipe pointed at .../wsl-migration/home/psyden/kernel/
# pristine).  The authoritative copy is local/pristine/linux-6.15.11.tar.xz,
# whose sha256 is checked below.
#
# Usage:  tools/get_pristine.sh [dest-dir]
#         default dest: ${TMPDIR:-/tmp}/linux-6.15.11
#
# The tree is ~1.3 GB extracted.  /tmp is a tmpfs on the bench host, so if RAM
# is tight pass a disk path instead, e.g. local/pristine/linux-6.15.11.
# Re-running is cheap: an existing extraction is reused.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARBALL="$ROOT/local/pristine/linux-6.15.11.tar.xz"
SHA256=89ab469fc35bd9cbc6c5bf4e5cb802581806d5cbc14fb47e5c9edcc477e65d93
NAME=linux-6.15.11

if [ $# -ge 1 ]; then
	DEST=$1
else
	DEST=${TMPDIR:-/tmp}/$NAME
fi

if [ -f "$DEST/Makefile" ] && grep -q "^VERSION = 6$" "$DEST/Makefile" 2>/dev/null; then
	echo "pristine tree already present: $DEST"
	exit 0
fi

[ -f "$TARBALL" ] || { echo "missing payload: $TARBALL" >&2; exit 1; }

got=$(sha256sum "$TARBALL" | cut -d' ' -f1)
if [ "$got" != "$SHA256" ]; then
	echo "sha256 mismatch on $TARBALL" >&2
	echo "  want $SHA256" >&2
	echo "  got  $got" >&2
	exit 1
fi

parent=$(dirname -- "$DEST")
base=$(basename -- "$DEST")
if [ "$base" != "$NAME" ]; then
	echo "destination must end in /$NAME (the tarball's top directory)" >&2
	exit 1
fi

mkdir -p -- "$parent"
echo "extracting $TARBALL -> $parent/$NAME"
tar -xf "$TARBALL" -C "$parent"
echo "done: $DEST"
