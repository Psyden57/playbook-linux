#!/bin/sh
# rebake-hd3.sh — bake the current GPU desktop state into the flasher-restorable
# hd3 image.
#
# WHY: the baked image (local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img)
# carries catacomb 1.5.0-r10000 and epitaph 1.4.0-r0, so flashing it reverts the
# session-69 GPU client result (trap-343 class).  This script applies the delta
# from session 69/70 to a COPY of that image and leaves the original untouched.
#
# VERIFIED DELTA (session 70, read from the image's own /lib/apk/db/installed
# and its files, not assumed):
#
#   catacomb  1.5.0-r10000  ->  1.5.0-r10003   (adds the PRIME wl_drm and the
#                                               GL_TEXTURE_WRAP clamp, traps 345/347)
#   epitaph   1.4.0-r0      ->  1.4.0-r10008   (r10005 atlas/r0 fix; r10008 binds
#                                               the GL attribute/uniform locations, trap 348)
#   /etc/inittab            ->  + `::sysinit:/usr/sbin/taskset -p 3 1` before the
#                               OpenRC entries (the PID-1 affinity workaround,
#                               trap 340) — the baked image does NOT have it
#   the PowerVR pitch fix   ->  /usr/local/lib/dbmfix.so + /usr/local/bin/
#                               catacomb-dbmfix + catacomb.desktop Exec= (report
#                               271).  Without it every pvr buffer is 8x too big
#                               (19,660,800 B instead of 2,457,600) and a large
#                               client exhausts the 128 MiB TILER window (trap 349).
#                               Set DBMFIX_SO=/path/to/dbmfix.so to include it.
#
# ALREADY CORRECT IN THE BAKED IMAGE (do not "fix" these):
#   * /etc/catacomb/initrc   — md5 00fdff8481e7c4dde730221ef55d53da, the pvr
#                              variant with no `unset`; identical to the live root
#                              and to /etc/catacomb/initrc.session68-pvr
#   * /etc/profile.d/mesa.sh — exports MESA_LOADER_DRIVER_OVERRIDE=pvr
#   * /etc/modules           — carries pvrsrvkm_omap4_sgx540_120
#   * libglvnd 1.6.0-r2, mesa-pvr-dri-classic 21.3.9-r1, sgx-ddk-um 1.17.4948957-r1
#
# NOT IN THE BAKED IMAGE AND NOT NEEDED (the handoff listed them; measured absent
# on the live root too, so they are not part of the working desktop):
#   * libLLVM-13.so / an llvmpipe swrast_dri.so — the live root's
#     /usr/lib/xorg/modules/dri/swrast_dri.so pulls no LLVM and the desktop runs
#     on pvr_dri, so the llvmpipe swap is not in the shipped state
#   * custom udev/polkit "catacomb" rules — /etc/udev/rules.d and
#     /etc/polkit-1/rules.d are both EMPTY on the live root
#
# USAGE (host, needs root for the loop mount and the apk --root install):
#
#   ./tools/rebake-hd3.sh [--dry-run]
#
# The operator runs this; it writes a NEW image and prints its sha256.  Then
# flash it at loader block 0x4915:
#
#   python3 tools/bbusb.py flash \
#       --component local/work/nura/images/blackberry-playbook-root-gpu-<DATE>.img \
#       --dest-block 0x4915 --from-bootrom --password 0000 --apply
#
# SAFETY: hd3 is MCT type 0x1C, loader blocks 0x4905-0xFFFFF; flashing is the
# documented pre-OS 0xF7 loop (SAFETY.md §4).  No NVRAM/MCT/RPMB access.

set -eu

REPO=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BASE="$REPO/local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img"
PKGS="$REPO/local/work/nura/work/packages/edge/armv7"
DATE=$(date -u +%Y%m%d)
OUT="$REPO/local/work/nura/images/blackberry-playbook-root-gpu-$DATE.img"
MNT="$REPO/local/work/rebake-mnt"

CATACOMB_APK="$PKGS/catacomb-1.5.0-r10003.apk"
EPITAPH_APK="$PKGS/epitaph-1.4.0-r10008.apk"

DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1

die() { echo "rebake: $*" >&2; exit 1; }

[ -f "$BASE" ]          || die "missing base image: $BASE"
[ -f "$CATACOMB_APK" ]  || die "missing package: $CATACOMB_APK"
[ -f "$EPITAPH_APK" ]   || die "missing package: $EPITAPH_APK"

echo "rebake: base = $BASE"
echo "rebake: sha256(base) = $(sha256sum "$BASE" | cut -d' ' -f1)"
echo "rebake: out  = $OUT"

if [ "$DRY" = 1 ]; then
    echo "rebake: DRY RUN — would copy the base, loop-mount it, and install:"
    echo "  $CATACOMB_APK"
    echo "  $EPITAPH_APK"
    echo "  + the inittab affinity line"
    exit 0
fi

[ "$(id -u)" = 0 ] || die "must run as root (loop mount + apk --root install)"

cp -f "$BASE" "$OUT"
mkdir -p "$MNT"

# Mount the image copy.  `apk --root` needs a real directory tree, so the
# filesystem must be mounted first; nothing is written to the base image.
LOOP=$(losetup -f --show "$OUT") || die "losetup failed"
cleanup() {
    umount "$MNT" 2>/dev/null || true
    losetup -d "$LOOP" 2>/dev/null || true
}
trap cleanup EXIT INT TERM
mount -t ext4 "$LOOP" "$MNT" || die "mount $LOOP failed"

# No resize: the baked fs already fills the image (152 832 blocks × 4 KiB =
# 625 999 872 B = the file size) and has ~225 MiB free, far more than the two
# packages need.  The DEVICE grows the fs to 62.5 G on first boot (report 249),
# so the baked image must stay at its natural size.

# 1. Install the two packages into the image, offline and untrusted-signed.
#    --root makes apk operate on the image's filesystem, not the host's.
echo "rebake: installing packages"
apk --root "$MNT" --no-network --allow-untrusted --upgrade \
    "$CATACOMB_APK" "$EPITAPH_APK"

# 2. The PID-1 affinity workaround.  Insert before the OpenRC entries.
echo "rebake: adding the inittab affinity line"
if ! grep -q 'taskset -p 3 1' "$MNT/etc/inittab"; then
    tmp="$MNT/etc/inittab.rebake"
    {
        sed -n '1,/^::sysinit:\/sbin\/openrc sysinit/p' "$MNT/etc/inittab" | sed '$d'
        echo '::sysinit:/usr/sbin/taskset -p 3 1'
        sed -n '/^::sysinit:\/sbin\/openrc sysinit/,$p' "$MNT/etc/inittab"
    } > "$tmp"
    chmod --reference="$MNT/etc/inittab" "$tmp"
    mv -f "$tmp" "$MNT/etc/inittab"
fi

# 3. The PowerVR pitch fix (report 271).  Optional: set DBMFIX_SO to the built
#    dbmfix.so (armv7 musl).  Installs the shim, the exec wrapper that enables it,
#    and repoints the session at the wrapper.
if [ -n "${DBMFIX_SO:-}" ] && [ -f "$DBMFIX_SO" ]; then
    echo "rebake: installing the PowerVR pitch fix from $DBMFIX_SO"
    install -d "$MNT/usr/local/lib" "$MNT/usr/local/bin"
    install -m 755 "$DBMFIX_SO" "$MNT/usr/local/lib/dbmfix.so"
    cat > "$MNT/usr/local/bin/catacomb-dbmfix" <<'WRAP'
#!/bin/sh
# Start catacomb with the PowerVR pitch fix (report 271): libdbm describes a
# linear buffer as width=1,height=<bytes>,bpp=8 and omapdrm aligns pitch to
# 8 bytes, so every buffer is 8x too big.  dbmfix.so re-expresses the bytes as
# width=<bytes>,height=1.  Remove this wrapper to revert.
DBMFIX=1
LD_PRELOAD=/usr/local/lib/dbmfix.so${LD_PRELOAD:+:$LD_PRELOAD}
export DBMFIX LD_PRELOAD
exec catacomb "$@"
WRAP
    chmod 755 "$MNT/usr/local/bin/catacomb-dbmfix"
    if [ -f "$MNT/usr/share/wayland-sessions/catacomb.desktop" ]; then
        [ -f "$MNT/usr/share/wayland-sessions/catacomb.desktop.orig" ] || \
            cp -f "$MNT/usr/share/wayland-sessions/catacomb.desktop" \
                  "$MNT/usr/share/wayland-sessions/catacomb.desktop.orig"
        sed -i 's|^Exec=catacomb$|Exec=/usr/local/bin/catacomb-dbmfix|' \
            "$MNT/usr/share/wayland-sessions/catacomb.desktop"
    fi
    echo "rebake: catacomb.desktop Exec = $(grep '^Exec=' "$MNT/usr/share/wayland-sessions/catacomb.desktop")"
else
    echo "rebake: NOTE — DBMFIX_SO not set, so the pitch fix is NOT baked in."
    echo "rebake:        the image will allocate 8x buffers and a large client will"
    echo "rebake:        exhaust the TILER (trap 349).  Build it with:"
    echo "rebake:          \$M -shared -fPIC -O2 -o dbmfix.so tools/bench/gpu/dbmfix.c -ldl"
fi

# 4. Report the resulting state, from the image's own database.
echo "rebake: installed versions in the image:"
for p in catacomb epitaph; do
    v=$(awk -v P="P:$p" '$0==P{getline; sub(/^V:/,""); print; exit}' \
        "$MNT/lib/apk/db/installed")
    echo "  $p $v"
done
echo "rebake: inittab line: $(grep -c 'taskset -p 3 1' "$MNT/etc/inittab")"
echo "rebake: initrc md5:   $(md5sum "$MNT/etc/catacomb/initrc" | cut -d' ' -f1) (want 00fdff8481e7c4dde730221ef55d53da)"

echo "rebake: sha256(out) = $(sha256sum "$OUT" | cut -d' ' -f1)"
echo "rebake: size        = $(stat -c%s "$OUT") bytes"
echo "rebake: DONE — flash at block 0x4915, then verify with:"
echo "        tools/pbssh.sh 'apk info -v catacomb epitaph; grep taskset /etc/inittab'"
