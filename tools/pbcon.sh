#!/bin/sh
# pbcon.sh — drive the PlayBook bench console (report 115) from the PC.
#
#   stty -F /dev/ttyACM0 raw -echo   # once per enumerate (see below)
#   ./pbcon.sh 'help' 'peek 4a314028 2'
#
# One host open() for the whole session: the console's prompt/banner is only
# printed while a host fd is held, and the read-back at the end would otherwise
# race with `printf ... > /dev/ttyACM0` (each command in its own open loses the
# first ~1.5 KB of output to the tty hangup).
#
# The ACM node comes up root:uucp 0660; a normal user needs either
#   pkexec chmod 666 /dev/ttyACM0      (what the 2026-10-27 session did)
# or a udev rule / group membership before this script can open it.
exec 3<>/dev/ttyACM0 || exit 1
sleep 0.4
for c in "$@"; do
	printf '%s\n' "$c" >&3
	sleep 0.6
done
sleep 0.5
timeout 2 cat <&3
