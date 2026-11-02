#!/bin/sh
# Capture the full kernel log of a running PlayBook Linux bring-up image.
#
# Usage:  tools/pb_bootlog.sh [outfile]
# Default: local/logs/boot-YYYYmmdd-HHMMSS.log
#
# Run it after the console is up (/dev/ttyACM0 chmod 666).  It asks the
# console's `kmsg` command for every /dev/kmsg record (the same text the
# framebuffer console shows) and appends the console's banner/version.  The
# file is plain text; the console's kmsg prints the message part of each
# record (the /dev/kmsg "prio,seq,usec,flags;" prefix is stripped), in order.
#
# This is a capture helper only; it does not touch the unit's state.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
STAMP=$(date +%Y%m%d-%H%M%S)
OUT=${1:-"$ROOT/local/logs/boot-$STAMP.log"}

mkdir -p "$(dirname -- "$OUT")"
{
	echo "# PlayBook Linux boot log capture"
	echo "# started: $(date -Is)"
	echo "# console: /dev/ttyACM0"
	echo "#"
} > "$OUT"

# The console banner prints /proc/version on the first byte after connect.
PBCON_TMO=${PBCON_TMO:-180} "$ROOT/tools/pbcon.py" 'help' >> "$OUT" 2>&1 || true

# The whole kernel log (the console's `kmsg` with no filter prints every
# record, in order).
PBCON_TMO=${PBCON_TMO:-300} "$ROOT/tools/pbcon.py" 'kmsg' >> "$OUT" 2>&1 || true

{
	echo "#"
	echo "# finished: $(date -Is)"
} >> "$OUT"

echo "wrote $OUT ($(wc -l < "$OUT") lines)"
