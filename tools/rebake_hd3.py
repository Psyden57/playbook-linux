#!/usr/bin/env python3
"""rebake_hd3.py — bake the current GPU desktop delta into a copy of the
flasher-restorable hd3 image.

Why this exists
---------------
The baked image (local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img,
sha256 07555bde...) is the session-59 state.  Flashing it reverts the whole
session-69/70 GPU result (trap-343 class).  This tool applies the measured delta
to a COPY and leaves the original untouched:

  * catacomb  1.5.0-r10000 -> 1.5.0-r10003   (wl_drm PRIME + NPOT clamp)
  * epitaph   1.4.0-r0     -> 1.4.0-r10008   (atlas fix + bound GL locations)
  * /etc/inittab           -> + ::sysinit:/usr/sbin/taskset -p 3 1
  * the PowerVR pitch fix  -> /usr/local/lib/dbmfix.so + catacomb-dbmfix wrapper
                              + catacomb.desktop Exec= (report 271)

It deliberately does NOT install the catacomb APK's /etc/catacomb/initrc:
that file is owned by postmarketos-ui-catacomb and the image carries the
bench-tuned pvr variant (md5 00fdff8481e7c4dde730221ef55d53da, no `unset`).
The r10003 APK's copy is the upstream default (squeekboard/swayidle/bash) and
installing it would break the desktop.  `apk --upgrade` of that APK would try
to, and would also file-conflict with the UI package -- which is why this tool
installs the three package files it needs directly and splices the package
database from the live root's records.

Modes
-----
  root  : loop-mount the copy and write with ordinary file operations.
  user  : write with debugfs (no mount, no root); ownership is set explicitly.
Both apply the same delta; the mode is chosen from the effective uid.

The package-database records are taken from a reference snapshot of the live
root (`/lib/apk/db/installed` and `/etc/apk/world`).  They carry apk's opaque
`C:` identity hashes, which are what the world pins name; the records therefore
match what `apk` installed on the unit.  Pull them with:

    tools/pbssh.sh 'cat /lib/apk/db/installed' > local/work/rebake-ref/installed.live
    tools/pbssh.sh 'cat /etc/apk/world'        > local/work/rebake-ref/world.live

Usage
-----
    DBMFIX_SO=$PWD/local/work/dbmfix.so tools/rebake-hd3.sh --dry-run
    DBMFIX_SO=$PWD/local/work/dbmfix.so tools/rebake-hd3.sh

The operator flashes the result at loader block 0x4915 (see HANDOFF §0f.1).
"""

import argparse
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_BASE = REPO / "local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img"
DEFAULT_PKGS = REPO / "local/work/nura/work/packages/edge/armv7"
DEFAULT_REF_INSTALLED = REPO / "local/work/rebake-ref/installed.live"
DEFAULT_REF_WORLD = REPO / "local/work/rebake-ref/world.live"
DEFAULT_MODULES = REPO / "local/work/rebake-modules/7.3.3"

CATACOMB_APK = "catacomb-1.5.0-r10003.apk"
EPITAPH_APK = "epitaph-1.4.0-r10008.apk"

BASE_SHA256 = "07555bde64260e23e022eb6190e0cd4c45866e9bf19e0f815240209fdf2539d6"
INITRC_MD5 = "00fdff8481e7c4dde730221ef55d53da"

WRAPPER = """#!/bin/sh
# Start catacomb with the PowerVR pitch fix (report 271): libdbm describes a
# linear buffer as width=1,height=<bytes>,bpp=8 and omapdrm aligns pitch to
# 8 bytes, so every buffer is 8x too big.  dbmfix.so re-expresses the bytes as
# width=<bytes>,height=1.  Remove this wrapper to revert.
DBMFIX=1
LD_PRELOAD=/usr/local/lib/dbmfix.so${LD_PRELOAD:+:$LD_PRELOAD}
export DBMFIX LD_PRELOAD
exec catacomb "$@"
"""


class Fatal(Exception):
    pass


def run(cmd, **kw):
    p = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if p.returncode != 0:
        raise Fatal(f"command failed ({p.returncode}): {' '.join(map(str, cmd))}\n{p.stdout}\n{p.stderr}")
    return p


def sha256_file(path):
    import hashlib
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def md5_file(path):
    import hashlib
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


class Ext4Image:
    """Minimal file API over an unmounted ext4 image using debugfs."""

    def __init__(self, path):
        self.path = str(path)

    def _df(self, cmds, write=True):
        args = ["debugfs"]
        if write:
            args.append("-w")
        for c in cmds:
            args += ["-R", c]
        args.append(self.path)
        return subprocess.run(args, capture_output=True, text=True)

    def read(self, imgpath):
        with tempfile.NamedTemporaryFile() as tf:
            p = self._df([f"dump {imgpath} {tf.name}"], write=False)
            if p.returncode != 0 or "File not found" in p.stderr or "File not found" in p.stdout:
                raise Fatal(f"debugfs dump {imgpath} failed: {p.stdout} {p.stderr}")
            return Path(tf.name).read_bytes()

    def write(self, imgpath, data, mode, gid=0):
        with tempfile.NamedTemporaryFile() as tf:
            tf.write(data)
            tf.flush()
            # `rm` may print "File not found" for a new path; ignore only that.
            self._df([f"rm {imgpath}"])
            p = self._df([f"write {tf.name} {imgpath}"])
            if "Allocated inode" not in (p.stdout + p.stderr):
                raise Fatal(f"debugfs write {imgpath} failed: {p.stdout} {p.stderr}")
            for cmd in (f"sif {imgpath} mode 0{0o100000 | mode:o}",
                        f"sif {imgpath} uid 0",
                        f"sif {imgpath} gid {gid}"):
                p = self._df([cmd])
                if p.returncode != 0:
                    raise Fatal(f"debugfs {cmd} failed: {p.stdout} {p.stderr}")

    def exists(self, imgpath):
        p = self._df([f"stat {imgpath}"], write=False)
        return "Inode:" in p.stdout

    def mkdir(self, imgpath):
        p = self._df([f"mkdir {imgpath}"])
        err = p.stdout + p.stderr
        if p.returncode != 0 and "File exists" not in err:
            raise Fatal(f"debugfs mkdir {imgpath} failed: {err}")

    def rm(self, imgpath):
        self._df([f"rm {imgpath}"])

    def rmdir(self, imgpath):
        self._df([f"rmdir {imgpath}"])

    def run_script(self, commands):
        """Run a batch of debugfs commands from one file.  Returns the output."""
        with tempfile.NamedTemporaryFile("w", suffix=".debugfs", delete=False) as f:
            f.write("\n".join(commands) + "\n")
            name = f.name
        try:
            p = subprocess.run(["debugfs", "-w", "-f", name, self.path],
                               capture_output=True, text=True)
            return p.returncode, p.stdout + p.stderr
        finally:
            os.unlink(name)


class MountedImage:
    """File API over a loop-mounted image (root only)."""

    def __init__(self, path):
        self.path = str(path)
        self.mnt = tempfile.mkdtemp(prefix="rebake-mnt-")
        run(["mount", "-t", "ext4", "-o", "loop", self.path, self.mnt])

    def _host(self, imgpath):
        return Path(self.mnt + imgpath)

    def read(self, imgpath):
        return self._host(imgpath).read_bytes()

    def write(self, imgpath, data, mode, gid=0):
        p = self._host(imgpath)
        p.parent.mkdir(parents=True, exist_ok=True)
        tmp = p.with_name(p.name + ".rebake-tmp")
        tmp.write_bytes(data)
        os.chmod(tmp, mode)
        os.chown(tmp, 0, gid)
        os.replace(tmp, p)

    def exists(self, imgpath):
        return self._host(imgpath).exists()

    def mkdir(self, imgpath):
        self._host(imgpath).mkdir(parents=True, exist_ok=True)

    def close(self):
        run(["umount", self.mnt])
        os.rmdir(self.mnt)


def extract_apk(apk, dest):
    with tarfile.open(apk, "r:gz") as t:
        for m in t.getmembers():
            if m.name.startswith(".SIGN"):
                continue
            t.extract(m, dest, filter="data")


def read_blocks(text):
    """Split an apk v2 installed database into (name -> text) blocks, keeping
    the optional C: line and the trailing blank line."""
    lines = text.splitlines(keepends=True)
    blocks = []
    i = 0
    while i < len(lines):
        start = i
        if lines[i].startswith("C:") and i + 1 < len(lines) and lines[i + 1].startswith("P:"):
            i += 1
        if lines[i].startswith("P:"):
            name = lines[i][2:].strip()
            j = i + 1
            while j < len(lines) and lines[j].strip() != "":
                j += 1
            end = j + 1 if j < len(lines) else j
            blocks.append((name, start, end, "".join(lines[start:end])))
            i = end
        else:
            i += 1
    return lines, blocks


def splice_installed(img_text, ref_text, names):
    lines, blocks = read_blocks(img_text)
    _, ref_blocks = read_blocks(ref_text)
    ref = {n: b for (n, _, _, b) in ref_blocks}
    for n in names:
        if n not in ref:
            raise Fatal(f"reference installed DB has no record for {n}")
    # Replace from the bottom up so earlier indices stay valid.
    for name, start, end, _ in sorted(blocks, key=lambda b: b[1], reverse=True):
        if name in names:
            lines[start:end] = [ref[name]]
    return "".join(lines)


def install_modules(img, modules_dir):
    """Replace /lib/modules/7.3.3 in the image with the live ARMv7 set.

    The baked image carries the pre-ARMv7 (vermagic ARMv6) module set; the
    running kernel is ARMv7 SMP, so the image's modules would not load.  This
    was missing from the recorded delta and is why the rebake copies the whole
    tree (metadata included) from the live root.
    """
    dst = "/lib/modules/7.3.3"
    modules_dir = Path(modules_dir)
    if isinstance(img, MountedImage):
        host = img._host(dst)
        if host.exists():
            shutil.rmtree(host)
        shutil.copytree(modules_dir, host, symlinks=True)
        for root, dirs, files in os.walk(host):
            os.chown(root, 0, 0)
            for name in dirs:
                os.chown(os.path.join(root, name), 0, 0)
            for name in files:
                os.chown(os.path.join(root, name), 0, 0)
        return

    cmds = []
    # Remove the old tree, bottom-up.
    with tempfile.TemporaryDirectory() as td:
        img._df([f"rdump {dst} {td}"], write=False)
        old = Path(td) / "7.3.3"
        if old.exists():
            for root, dirs, files in os.walk(old, topdown=False):
                relroot = Path(root).relative_to(old)
                for name in files:
                    cmds.append(f"rm {dst}/{relroot / name}")
                for name in dirs:
                    cmds.append(f"rmdir {dst}/{relroot / name}")
    # Recreate directories, then files.
    dirs = []
    for root, _, _ in os.walk(modules_dir):
        dirs.append(Path(root).relative_to(modules_dir))
    for rel in sorted(dirs, key=lambda p: len(p.parts)):
        cmds.append(f"mkdir {dst}" if str(rel) == "." else f"mkdir {dst}/{rel}")
    nfiles = 0
    for root, _, files in os.walk(modules_dir):
        relroot = Path(root).relative_to(modules_dir)
        for name in sorted(files):
            host = Path(root) / name
            rel = relroot / name
            mode = os.stat(host).st_mode & 0o7777
            cmds.append(f"write {host} {dst}/{rel}")
            cmds.append(f"sif {dst}/{rel} mode 0{0o100000 | mode:o}")
            cmds.append(f"sif {dst}/{rel} uid 0")
            cmds.append(f"sif {dst}/{rel} gid 0")
            nfiles += 1
    rc, out = img.run_script(cmds)
    allocated = out.count("Allocated inode")
    if rc != 0 or allocated < nfiles:
        raise Fatal(f"module tree install failed (rc={rc}, files={nfiles}, allocated={allocated})\n{out[-4000:]}")


def group_members(text, name):
    for line in text.splitlines():
        parts = line.split(":")
        if parts and parts[0] == name:
            return parts[3].split(",") if len(parts) > 3 and parts[3] else []
    return None


def install_permissions(img):
    """Install the session-70 desktop permissions set (rootfs/README-permissions.md).

    Brightness, the Wi-Fi toggle and gorm's scan/connect all run as `playbook`
    and need (a) the `video` group on the backlight node, (b) `playbook` in that
    group, and (c) the NetworkManager polkit rule.  The zram override is included
    because the unit otherwise has no swap (zstd is not built into this kernel).
    These were measured working on the live root in session 71; this bakes them.
    """
    polkit = REPO / "rootfs/polkit/50-playbook-desktop.rules"
    udev = REPO / "rootfs/udev/90-playbook-backlight.rules"
    devinfo = REPO / "rootfs/deviceinfo.d/bench-overrides"
    for p in (polkit, udev, devinfo):
        if not p.is_file():
            raise Fatal(f"missing permissions input: {p}")

    group = img.read("/etc/group").decode()
    pgid = None
    lines = []
    video_found = False
    for line in group.splitlines(keepends=True):
        parts = line.rstrip("\n").split(":")
        if parts and parts[0] == "polkitd":
            pgid = int(parts[2])
        if parts and parts[0] == "video":
            video_found = True
            members = [m for m in (parts[3].split(",") if len(parts) > 3 and parts[3] else []) if m]
            if "playbook" not in members:
                members.append("playbook")
            line = ":".join(parts[:3] + [",".join(members)]) + "\n"
        lines.append(line)
    if pgid is None:
        raise Fatal("no polkitd group in /etc/group")
    if not video_found:
        raise Fatal("no video group in /etc/group")
    img.write("/etc/group", "".join(lines).encode(), 0o644)
    img.write("/etc/polkit-1/rules.d/50-playbook-desktop.rules",
              polkit.read_bytes(), 0o640, gid=pgid)
    img.write("/etc/udev/rules.d/90-playbook-backlight.rules",
              udev.read_bytes(), 0o644)
    img.write("/etc/deviceinfo", devinfo.read_bytes(), 0o644)


def splice_world(img_text, ref_text):
    ref_lines = {}
    for line in ref_text.splitlines(keepends=True):
        if line.strip():
            ref_lines[line.split("><")[0].strip()] = line if line.endswith("\n") else line + "\n"
    out = []
    seen = set()
    for line in img_text.splitlines(keepends=True):
        name = line.split("><")[0].strip() if line.strip() else ""
        if name in ("catacomb", "epitaph"):
            if name in ref_lines and name not in seen:
                out.append(ref_lines[name])
                seen.add(name)
            continue
        out.append(line)
    for name in ("catacomb", "epitaph"):
        if name not in seen:
            out.append(ref_lines[name])
    return "".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", default=str(DEFAULT_BASE))
    ap.add_argument("--out", default=str(DEFAULT_BASE.parent / f"blackberry-playbook-root-gpu-{__import__('datetime').date.today():%Y%m%d}.img"))
    ap.add_argument("--packages", default=str(DEFAULT_PKGS))
    ap.add_argument("--ref-installed", default=str(DEFAULT_REF_INSTALLED))
    ap.add_argument("--ref-world", default=str(DEFAULT_REF_WORLD))
    ap.add_argument("--modules-dir", default=str(DEFAULT_MODULES),
                    help="the live root's /lib/modules/7.3.3 (ARMv7 tree)")
    ap.add_argument("--no-modules", action="store_true",
                    help="do not replace /lib/modules/7.3.3 (the image then keeps its ARMv6 set)")
    ap.add_argument("--dbmfix", default=os.environ.get("DBMFIX_SO", ""))
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    base = Path(args.base)
    out = Path(args.out)
    pkgs = Path(args.packages)
    catacomb_apk = pkgs / CATACOMB_APK
    epitaph_apk = pkgs / EPITAPH_APK

    for p in (base, catacomb_apk, epitaph_apk):
        if not p.is_file():
            raise Fatal(f"missing input: {p}")
    if not Path(args.ref_installed).is_file():
        raise Fatal(f"missing reference installed DB: {args.ref_installed}\n"
                    "  tools/pbssh.sh 'cat /lib/apk/db/installed' > " + args.ref_installed)
    if not Path(args.ref_world).is_file():
        raise Fatal(f"missing reference world file: {args.ref_world}\n"
                    "  tools/pbssh.sh 'cat /etc/apk/world' > " + args.ref_world)

    if args.dbmfix and not Path(args.dbmfix).is_file():
        raise Fatal(f"DBMFIX_SO is set but not a file: {args.dbmfix}")
    modules_dir = Path(args.modules_dir)
    if not args.no_modules:
        if not modules_dir.is_dir():
            raise Fatal(f"missing ARMv7 module tree: {modules_dir}\n"
                        "  tools/pbssh.sh 'tar -C /lib/modules -cf - 7.3.3' > /tmp/m.tar && "
                        f"tar -C {modules_dir.parent} -xf /tmp/m.tar")
        pvrs = next(modules_dir.rglob("pvrsrvkm_omap4_sgx540_120.ko"), None)
        if pvrs is None:
            raise Fatal(f"no pvrsrvkm_omap4_sgx540_120.ko under {modules_dir}")
        verm = subprocess.run(["modinfo", "-F", "vermagic", str(pvrs)],
                              capture_output=True, text=True).stdout.strip()
        if "ARMv7" not in verm or "SMP" not in verm:
            raise Fatal(f"module tree is not the ARMv7 SMP set: vermagic={verm!r}")

    base_hash = sha256_file(base)
    print(f"rebake: base = {base}")
    print(f"rebake: sha256(base) = {base_hash}")
    print(f"rebake: out  = {out}")

    stage = Path(tempfile.mkdtemp(prefix="rebake-stage-"))
    try:
        extract_apk(catacomb_apk, stage)
        extract_apk(epitaph_apk, stage)

        files = {
            "/usr/bin/catacomb": (stage / "usr/bin/catacomb", 0o755),
            "/usr/bin/epitaph": (stage / "usr/bin/epitaph", 0o755),
            "/usr/share/wayland-sessions/catacomb.desktop.orig":
                (stage / "usr/share/wayland-sessions/catacomb.desktop", 0o644),
        }
        desktop = (stage / "usr/share/wayland-sessions/catacomb.desktop").read_text()
        if "Exec=catacomb" not in desktop:
            raise Fatal("package catacomb.desktop does not have Exec=catacomb")
        files["/usr/share/wayland-sessions/catacomb.desktop"] = (
            None, 0o644, desktop.replace("Exec=catacomb", "Exec=/usr/local/bin/catacomb-dbmfix").encode())

        if args.dbmfix:
            files["/usr/local/lib/dbmfix.so"] = (Path(args.dbmfix), 0o755)
            files["/usr/local/bin/catacomb-dbmfix"] = (None, 0o755, WRAPPER.encode())

        print("rebake: delta:")
        print(f"  catacomb 1.5.0-r10000 -> 1.5.0-r10003   ({files['/usr/bin/catacomb'][0].stat().st_size} B)")
        print(f"  epitaph  1.4.0-r0     -> 1.4.0-r10008   ({files['/usr/bin/epitaph'][0].stat().st_size} B)")
        print("  /etc/inittab          -> + ::sysinit:/usr/sbin/taskset -p 3 1")
        if not args.no_modules:
            print(f"  /lib/modules/7.3.3     -> the live ARMv7 SMP set ({modules_dir})")
        print("  permissions           -> polkit rule + backlight udev rule + /etc/deviceinfo")
        print("                           + playbook in the video group")
        if args.dbmfix:
            print(f"  pitch fix             -> {args.dbmfix} ({sha256_file(args.dbmfix)[:16]}…)")
        else:
            print("  pitch fix             -> NOT included (set DBMFIX_SO)")

        if args.dry_run:
            print("rebake: DRY RUN — no image written.")
            return 0

        if out.exists():
            raise Fatal(f"refusing to overwrite existing output: {out} (remove it first)")
        if base_hash != BASE_SHA256:
            print(f"rebake: WARNING base sha256 differs from the recorded {BASE_SHA256}", file=sys.stderr)

        print("rebake: copying base -> out")
        shutil.copyfile(base, out)

        if os.geteuid() == 0:
            img = MountedImage(out)
            close = img.close
        else:
            img = Ext4Image(out)
            close = lambda: None
        try:
            print("rebake: installing files")
            for imgpath, entry in files.items():
                if len(entry) == 2:
                    host, mode = entry
                    data = Path(host).read_bytes()
                else:
                    _, mode, data = entry
                img.write(imgpath, data, mode)

            if not args.no_modules:
                print("rebake: replacing /lib/modules/7.3.3 with the live ARMv7 set")
                install_modules(img, modules_dir)

            print("rebake: installing the desktop permissions set")
            install_permissions(img)

            print("rebake: adding the inittab affinity line")
            inittab = img.read("/etc/inittab").decode()
            if "taskset -p 3 1" not in inittab:
                marker = "::sysinit:/sbin/openrc sysinit\n"
                if marker not in inittab:
                    raise Fatal("unexpected /etc/inittab: no openrc sysinit line")
                inittab = inittab.replace(marker, "::sysinit:/usr/sbin/taskset -p 3 1\n" + marker, 1)
                img.write("/etc/inittab", inittab.encode(), 0o644)

            print("rebake: updating /lib/apk/db/installed")
            installed = img.read("/lib/apk/db/installed").decode()
            ref_installed = Path(args.ref_installed).read_text()
            installed = splice_installed(installed, ref_installed, {"catacomb", "epitaph"})
            img.write("/lib/apk/db/installed", installed.encode(), 0o644)

            print("rebake: updating /etc/apk/world")
            world = img.read("/etc/apk/world").decode()
            ref_world = Path(args.ref_world).read_text()
            world = splice_world(world, ref_world)
            img.write("/etc/apk/world", world.encode(), 0o644)

            print("rebake: verifying")
            checks = []
            cata_db = installed.split("P:catacomb\n", 1)[1].split("\n\n", 1)[0]
            epi_db = installed.split("P:epitaph\n", 1)[1].split("\n\n", 1)[0]
            checks.append(("catacomb version", "V:1.5.0-r10003" in cata_db))
            checks.append(("epitaph version", "V:1.4.0-r10008" in epi_db))
            checks.append(("inittab taskset", img.read("/etc/inittab").decode().count("taskset -p 3 1") == 1))
            checks.append(("initrc untouched", md5_file_from_bytes(img.read("/etc/catacomb/initrc")) == INITRC_MD5))
            checks.append(("desktop Exec", b"Exec=/usr/local/bin/catacomb-dbmfix" in
                           img.read("/usr/share/wayland-sessions/catacomb.desktop")))
            checks.append(("catacomb binary", sha256_bytes(img.read("/usr/bin/catacomb")) ==
                           sha256_file(stage / "usr/bin/catacomb")))
            checks.append(("epitaph binary", sha256_bytes(img.read("/usr/bin/epitaph")) ==
                           sha256_file(stage / "usr/bin/epitaph")))
            if args.dbmfix:
                checks.append(("dbmfix shim", sha256_bytes(img.read("/usr/local/lib/dbmfix.so")) ==
                               sha256_file(args.dbmfix)))
            checks.append(("polkit rule", img.read("/etc/polkit-1/rules.d/50-playbook-desktop.rules") ==
                           (REPO / "rootfs/polkit/50-playbook-desktop.rules").read_bytes()))
            checks.append(("udev rule", img.read("/etc/udev/rules.d/90-playbook-backlight.rules") ==
                           (REPO / "rootfs/udev/90-playbook-backlight.rules").read_bytes()))
            checks.append(("deviceinfo", img.read("/etc/deviceinfo") ==
                           (REPO / "rootfs/deviceinfo.d/bench-overrides").read_bytes()))
            checks.append(("playbook in video",
                           "playbook" in (group_members(img.read("/etc/group").decode(), "video") or [])))
            if not args.no_modules:
                rel = pvrs.relative_to(modules_dir)
                checks.append(("ARMv7 module tree",
                               sha256_bytes(img.read("/lib/modules/7.3.3/" + str(rel))) ==
                               sha256_file(pvrs)))
                dep = img.read("/lib/modules/7.3.3/modules.dep").decode()
                checks.append(("modules.dep path", str(rel) in dep))
            for name, ok in checks:
                print(f"  {'OK ' if ok else 'FAIL'} {name}")
            if not all(ok for _, ok in checks):
                raise Fatal("verification failed")
        finally:
            close()

        print("rebake: e2fsck -fn")
        p = subprocess.run(["e2fsck", "-fn", str(out)], capture_output=True, text=True)
        if p.returncode != 0:
            print(p.stdout)
            print(p.stderr, file=sys.stderr)
            raise Fatal(f"e2fsck returned {p.returncode}")
        print(f"rebake: sha256(out) = {sha256_file(out)}")
        print(f"rebake: size        = {out.stat().st_size} bytes")
        print("rebake: DONE — flash at loader block 0x4915 (start the flasher, then")
        print("        reset/power on), then verify on the unit with:")
        print("        tools/pbssh.sh 'apk info -v catacomb epitaph; grep taskset /etc/inittab'")
        return 0
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def sha256_bytes(b):
    import hashlib
    return hashlib.sha256(b).hexdigest()


def md5_file_from_bytes(b):
    import hashlib
    return hashlib.md5(b).hexdigest()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Fatal as e:
        print(f"rebake: ERROR: {e}", file=sys.stderr)
        sys.exit(1)
