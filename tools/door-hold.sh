#!/bin/sh
# Keep the BerryShell qconn door alive across PlayBook reboots.
#
# BerryShell-V4's `hold` mode already re-handshakes when the TCP door drops,
# but the process can still exit (signal, unhandled error).  This supervisor
# simply restarts it, so the door is back as soon as the device's qconn
# service is reachable again.
#
# Everything is resolved from this script's own location (a clone works
# anywhere); the PB_* variables override the defaults:
#   PB_KEY, PB_PUB      SSH key pair     (local/secrets/rsa[.pub])
#   PB_BERTSHELL        BerryShell-V4.py (tools/BerryShell-V4.py)
#   PB_DOOR_LOG         supervisor log   (/tmp/berryshell-hold.log)
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 1
KEY=${PB_KEY:-$ROOT/local/secrets/rsa}
PUB=${PB_PUB:-$ROOT/local/secrets/rsa.pub}
TOOL=${PB_BERTSHELL:-$ROOT/tools/BerryShell-V4.py}
LOG=${PB_DOOR_LOG:-/tmp/berryshell-hold.log}

if [ ! -f "$KEY" ] || [ ! -f "$PUB" ]; then
    echo "door-hold: no SSH key pair at $KEY / $PUB" >&2
    echo "door-hold: put it there or set PB_KEY / PB_PUB" >&2
    exit 1
fi

while true; do
    echo "[$(date -u +%FT%TZ)] supervisor: starting BerryShell-V4 hold" >> "$LOG"
    python3 "$TOOL" --log "$LOG" hold --key "$KEY" --pub "$PUB"
    rc=$?
    echo "[$(date -u +%FT%TZ)] supervisor: hold exited rc=$rc; restarting in 2s" >> "$LOG"
    sleep 2
done
