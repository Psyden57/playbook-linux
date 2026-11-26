#!/bin/sh
# rebake-hd3.sh — bake the current GPU desktop state into the flasher-restorable
# hd3 image.  Thin wrapper around tools/rebake_hd3.py (see that file for the
# full delta and the two write modes).
#
# WHY: the baked image (local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img)
# carries catacomb 1.5.0-r10000 and epitaph 1.4.0-r0, so flashing it reverts the
# session-69 GPU client result and the session-70 pitch fix (trap-343 class).
#
# USAGE (host):
#
#   DBMFIX_SO=$PWD/local/work/dbmfix.so tools/rebake-hd3.sh --dry-run
#   DBMFIX_SO=$PWD/local/work/dbmfix.so tools/rebake-hd3.sh
#
# Reference package-database records (once per live root state):
#
#   mkdir -p local/work/rebake-ref
#   tools/pbssh.sh 'cat /lib/apk/db/installed' > local/work/rebake-ref/installed.live
#   tools/pbssh.sh 'cat /etc/apk/world'        > local/work/rebake-ref/world.live
#
# ROOT IS NOT REQUIRED: as root the copy is loop-mounted; otherwise debugfs is
# used on the image file directly.  Both apply the same delta.
#
# The catacomb APK's /etc/catacomb/initrc is deliberately NOT installed: it is
# owned by postmarketos-ui-catacomb and the image carries the bench-tuned pvr
# variant (md5 00fdff8481e7c4dde730221ef55d53da).  The APK's copy is the
# upstream default (squeekboard/swayidle) and would break the desktop.
#
# SAFETY: hd3 is MCT type 0x1C, loader blocks 0x4905-0xFFFFF; flashing is the
# documented pre-OS 0xF7 loop (SAFETY.md §4).  No NVRAM/MCT/RPMB access.

set -eu

REPO=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

exec python3 "$REPO/tools/rebake_hd3.py" "$@"
