#!/bin/sh
# Extract a pristine Linux tree from the repository payload.
#
# The project must not depend on a pristine copy that lives outside this
# workspace (the old recipe pointed at .../wsl-migration/home/psyden/kernel/
# pristine).  The authoritative copies are the tarballs in local/pristine/,
# whose sha256 is checked below.
#
# Usage:  tools/get_pristine.sh [VERSION] [dest-dir]
#         VERSION: 7.3.3 (default, the current base) or 6.15.11 (reference)
#         default dest: ${TMPDIR:-/tmp}/linux-$VERSION
#
# Examples:
#   tools/get_pristine.sh                    # 7.3.3 -> /tmp/linux-7.3.3
#   tools/get_pristine.sh 6.15.11            # 6.15.11 -> /tmp/linux-6.15.11
#   tools/get_pristine.sh 7.3.3 local/pristine/linux-7.3.3
#
# The tree is ~1.3-1.8 GB extracted.  /tmp is a tmpfs on the bench host, so if
# RAM is tight pass a disk path instead.  Re-running is cheap: an existing
# extraction is reused.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

case "${1:-7.3.3}" in
7.3.3)
	VERSION=7.3.3
	SHA256=79c66dde66cd96877fcde8f7d36aec946b1f423d2b97b5a7b1751b75c764f3d1
	;;
6.15.11)
	VERSION=6.15.11
	SHA256=89ab469fc35bd9cbc6c5bf4e5cb802581806d5cbc14fb47e5c9edcc477e65d93
	;;
*)
	echo "unknown VERSION '$1' (want 7.3.3 or 6.15.11)" >&2
	exit 1
	;;
esac
[ $# -ge 1 ] && shift

NAME=linux-$VERSION
TARBALL="$ROOT/local/pristine/$NAME.tar.xz"

if [ $# -ge 1 ]; then
	DEST=$1
else
	DEST=${TMPDIR:-/tmp}/$NAME
fi

want_major=${VERSION%%.*}
rest=${VERSION#*.}
want_minor=${rest%%.*}
want_sub=${rest#*.}

if [ -f "$DEST/Makefile" ] &&
   grep -q "^VERSION = $want_major\$" "$DEST/Makefile" 2>/dev/null &&
   grep -q "^PATCHLEVEL = $want_minor\$" "$DEST/Makefile" 2>/dev/null &&
   grep -q "^SUBLEVEL = $want_sub\$" "$DEST/Makefile" 2>/dev/null; then
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
