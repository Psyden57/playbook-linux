#!/usr/bin/env python3
"""Build a debloated 2.1.0.1917 QNX image for use as a live hardware oracle.

WHAT THIS IS
  A reproducible way to turn the vendor 2.1.0.1917 components into a *minimal*
  bootable OS whose only job is: boot, bring up the USB door/qconn, and let us
  read (and poke) the hardware from QCL.py.  Two things change:

    * /base/scripts/startup.sh -- only the services needed for the USB
      network + qconn are started (no GUI, no AIR, no radio, no camera, no
      bluetooth, ...), and qconn is started directly on port 9090:

          /base/usr/sbin/qconn port=9090 &

      qconn is unauthenticated, so this skips qconnDoor/sshd/authman entirely.
      Connect from the host with tools/QCL.py to 169.254.0.1:9090.
    * /base is pruned -- the GUI/app/font/browser/DRM subtrees are deleted.

  The IFS is *not* rebuilt here.  It must be the shim-sealed 1917 IFS
  (local/work/ifs-shim-sealed.ifs), because the stock `rfs_validator` checks
  the RCFS signature and any edit to /base invalidates it.  The shim just
  mounts /base, which is exactly what a modified RCFS needs.

HOW THE EDIT WORKS (operator's rule)
  Copy the vendor .1.rcfs, mount the COPY read-write with `bb10mt qnx6 mount`,
  make every change through the mount, unmount.  Never rebuild the QNX6
  filesystem from scratch and never edit the vendor component.

SAFETY
  This only writes files under local/work/.  Flashing is a separate, explicit
  step (bbusb.py) that the operator authorises; this tool never touches USB or
  the device.

USAGE
  . tools/env.sh                       # for the pristine path, optional
  python3 tools/build_minimal_qnx.py --no-prune      # services only (stage A)
  python3 tools/build_minimal_qnx.py                 # services + prune (stage B)
  python3 tools/build_minimal_qnx.py --dry-run       # show the plan, change nothing
"""

import argparse
import os
import shutil
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BB10MT = os.path.join(REPO, "local/vendor/bb10mt/bb10mt")

DEFAULT_STOCK = "/home/psyden/Pdrive/bb10mt/2.1.0.1917/PlayBook_2.1.0.1917.1.rcfs"
DEFAULT_WORK = os.path.join(REPO, "local/work/minimal")

# The smallest service set that brings the USB door up.
#   filesystem : /bin,/lib,/usr,/etc symlinks + mount the UFS (/accounts) so
#                the boot-counter top-up script and the SSH key home exist
#   pps        : the PPS filesystem; qconnDoor/usbmgr/nvram publish into it
#   random     : /dev/random (crypto for the door challenge)
#   pty        : /dev/ttyp* for sshd
#   net        : io-pkt, which owns the USB ECM/RNDIS interface
#   powerman   : power management, which phyctrl asks to power the USB PHY
#   phyctrl    : turns the USB PHY + OTG controller clock on (needed to enumerate)
#   usbmgr     : RIM USB manager (creates ecm0 at 169.254.0.1)
#   rimusb     : the USB device-mode driver
# qconn is NOT a service: it is started directly at the end of main() as
#   /base/usr/sbin/qconn port=9090 &
# which is reachable at 169.254.0.1:9090 with tools/QCL.py.  No qconnDoor,
# sshd or authman is needed for that.
# Everything else (bootinfo, logging, simconfig, lpddrmon, thermal, battery,
# bootreason, splashscreen, rtc, dumper, ves, hid, capsensor, screen, drmclock,
# authman, coreservices, perimeter, audio, dtminit, certmgr, vpn, wpa, nfc,
# radio, lc, activator, keyboard, input, crypto, bbio*, bluetooth, geolocation,
# syslink, audioman, mmrenderer, soundplayerd, adl, sensorservices, sapphire,
# powerbrain, clearclipboards, fsevmgr, camera, pim*, carriermgr, launcher,
# avcontrols, mmsynclite, updater, paymentsystem, pmic, appupdatenotifier,
# vesclients, quip, rlogging, ebm, securedatamon, hwinfopps, ema, printd,
# pager, final, tools) is dropped.
MIN_SERVICES = {
    0: "filesystem pps random pty net powerman phyctrl usbmgr rimusb",
    1: "",
    2: "",
    "FINAL": "",
}

# BIG, obviously unrelated subtrees.  Deleting these is what makes the image
# "minimal"; the door stack's libraries (lib/, lib/dll/, usr/lib/libcrypto*,
# usr/lib/libpps*) are deliberately kept.
PRUNE_DIRS = [
    "air",
    "appinstall",
    "opt",
    "usr/fonts",
    "usr/hmi",
    "usr/photon",
    "usr/carrier",
    "usr/include",
    "usr/lib/browser",
    "usr/lib/ema",
    "usr/lib/graphics",
    "usr/lib/pim",
    "usr/lib/python2.7",
    "usr/lib/python3.2",
    "usr/share/wallpaper",
    "usr/share/webkit-owb",
    "usr/share/sounds",
    "seed/bars",
    "lib/firmware",
]
PRUNE_GLOBS = [
    "usr/lib/libwebkit*",
    "usr/lib/libicudata*",
    "usr/lib/libplayready*",
    "usr/lib/libcascades*",
    "usr/lib/libflashplayer*",
]

HOOK_ANCHOR = "        start_runlevel FINAL;\n"
HOOK = (
    "        start_runlevel FINAL;\n"
    "        # Minimal-image hook: keep BOOTCTR_FCTRY topped up every boot.\n"
    "        if [ -x /accounts/devuser/startup.sh ]; then\n"
    "            /accounts/devuser/startup.sh &\n"
    "        fi;\n"
    "        # Minimal-image hook: qconn directly on 9090 (no qconnDoor).\n"
    "        /base/usr/sbin/qconn port=9090 &\n"
)


def die(msg):
    print("build_minimal_qnx: " + msg, file=sys.stderr)
    sys.exit(1)


def is_mounted(mnt):
    try:
        with open("/proc/mounts") as f:
            for line in f:
                if line.split()[1] == mnt:
                    return True
    except OSError:
        pass
    return False


class RcfsMount:
    def __init__(self, image, mnt):
        self.image = image
        self.mnt = mnt
        self.proc = None

    def __enter__(self):
        os.makedirs(self.mnt, exist_ok=True)
        if is_mounted(self.mnt):
            die("%s is already mounted (unmount it first)" % self.mnt)
        self.proc = subprocess.Popen(
            [BB10MT, "qnx6", "mount", "-i", self.image, "-m", self.mnt, "-f"],
            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        for _ in range(100):
            if is_mounted(self.mnt):
                return self
            if self.proc.poll() is not None:
                die("bb10mt mount exited before the mount appeared")
            time.sleep(0.1)
        die("timed out waiting for %s to mount" % self.mnt)

    def __exit__(self, *exc):
        subprocess.run(["fusermount", "-u", self.mnt], check=False)
        for _ in range(100):
            if not is_mounted(self.mnt):
                break
            time.sleep(0.05)
        if self.proc is not None:
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        return False


def patch_startup(path):
    with open(path, "r") as f:
        text = f.read()
    if HOOK not in text:
        if text.count(HOOK_ANCHOR) != 1:
            die("expected exactly one 'start_runlevel FINAL;' in startup.sh")
        text = text.replace(HOOK_ANCHOR, HOOK)
    for lvl, services in MIN_SERVICES.items():
        old = 'RUN_LEVEL_%s_SERVICES="' % lvl
        idx = text.find(old)
        if idx < 0:
            die("missing %s in startup.sh" % old)
        end = text.find('"\n', idx)
        if end < 0:
            die("unterminated %s in startup.sh" % old)
        line = 'RUN_LEVEL_%s_SERVICES="%s"' % (lvl, (" " + services) if services else "")
        text = text[:idx] + line + text[end + 1:]
    with open(path, "w") as f:
        f.write(text)


def _size(p):
    """Size of a file/symlink/dir, tolerant of dangling symlinks on fuse."""
    try:
        st = os.lstat(p)
    except OSError:
        return 0
    if not os.path.isdir(p) or os.path.islink(p):
        return st.st_size
    total = 0
    for dp, _, fns in os.walk(p):
        for fn in fns:
            try:
                total += os.lstat(os.path.join(dp, fn)).st_size
            except OSError:
                pass
    return total


def prune(root, dry):
    removed = 0
    for rel in PRUNE_DIRS:
        p = os.path.join(root, rel)
        if not os.path.lexists(p):
            continue
        size = _size(p)
        if not dry:
            try:
                if os.path.islink(p) or os.path.isfile(p):
                    os.remove(p)
                else:
                    shutil.rmtree(p)
            except OSError as e:
                print("  SKIP  %-32s %s" % (rel, e))
                continue
        print("  prune %-32s %8.1f MiB" % (rel, size / 1048576.0))
        removed += size
    import glob
    for pattern in PRUNE_GLOBS:
        for p in sorted(glob.glob(os.path.join(root, pattern))):
            size = _size(p)
            print("  prune %-32s %8.1f MiB" % (os.path.relpath(p, root),
                                               size / 1048576.0))
            if not dry:
                try:
                    if os.path.islink(p) or os.path.isfile(p):
                        os.remove(p)
                    else:
                        shutil.rmtree(p)
                except OSError as e:
                    print("  SKIP  %-32s %s" % (os.path.relpath(p, root), e))
                    continue
            removed += size
    return removed


def du(path):
    total = 0
    for dp, _, fns in os.walk(path):
        for fn in fns:
            try:
                total += os.lstat(os.path.join(dp, fn)).st_size
            except OSError:
                pass
    return total


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stock", default=DEFAULT_STOCK,
                    help="vendor .1.rcfs (default: %(default)s)")
    ap.add_argument("--work", default=DEFAULT_WORK,
                    help="working directory (default: %(default)s)")
    ap.add_argument("--no-prune", action="store_true",
                    help="stage A: services only, keep all files")
    ap.add_argument("--dry-run", action="store_true",
                    help="report actions, change nothing")
    ap.add_argument("--force", action="store_true",
                    help="recopy the stock image even if the work image exists")
    args = ap.parse_args()

    if not os.path.isfile(BB10MT):
        die("missing %s" % BB10MT)
    if not os.path.isfile(args.stock):
        die("missing stock RCFS %s" % args.stock)

    os.makedirs(args.work, exist_ok=True)
    image = os.path.join(args.work, "rcfs-minimal.rcfs")
    mnt = os.path.join(args.work, "rcfs-mnt")
    if not os.path.isfile(image) or args.force:
        if args.dry_run:
            print("would copy %s -> %s" % (args.stock, image))
        else:
            print("copying stock RCFS (this is ~500 MB)...")
            shutil.copyfile(args.stock, image)

    if args.dry_run:
        print("services: %r" % MIN_SERVICES)
        print("prune: %d dirs + %d globs" % (len(PRUNE_DIRS), len(PRUNE_GLOBS)))
        return 0

    with RcfsMount(image, mnt):
        startup = os.path.join(mnt, "scripts/startup.sh")
        if not os.path.isfile(startup):
            die("mounted image has no scripts/startup.sh")
        patch_startup(startup)
        print("patched /base/scripts/startup.sh")
        if args.no_prune:
            print("stage A: no files removed")
        else:
            print("pruning /base:")
            freed = prune(mnt, dry=False)
            print("  freed %.1f MiB (image stays the same size on disk)"
                  % (freed / 1048576.0))

    # verify it still mounts and the startup is ours
    with RcfsMount(image, mnt):
        with open(os.path.join(mnt, "scripts/startup.sh")) as f:
            text = f.read()
        for lvl, services in MIN_SERVICES.items():
            want = 'RUN_LEVEL_%s_SERVICES="%s"' % (
                lvl, (" " + services) if services else "")
            if want not in text:
                die("verification failed: %s not present" % want)
        if HOOK not in text:
            die("verification failed: top-up hook missing")
        print("verified: %s (%.1f MiB used)"
              % (image, du(mnt) / 1048576.0))

    ifs = os.path.join(REPO, "local/work/ifs-shim-sealed.ifs")
    if os.path.isfile(ifs):
        print("IFS to flash: %s" % ifs)
    else:
        print("WARNING: %s missing - build the shim IFS first" % ifs)
    print("done: %s" % image)
    return 0


if __name__ == "__main__":
    sys.exit(main())
