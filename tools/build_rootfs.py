#!/usr/bin/env python3
"""Build the region-1 Linux root filesystem (ext4, no journal).

The image is written to media region 1 (/dev/hd2, raw 0x28000000) at region-1
offset 16 MiB -- raw 0x29000000, loader block 0x2915 -- which is inside the
signed loader's 0x1B allow-list, so `pre_os_ifs_flash.py --payload` can place it
without an MCT edit (reports 98 §4, 134 §6, 138).

The tree this installs is a real userland: BusyBox (static), BusyBox init with
`/etc/inittab`, a boot script, the bring-up console and `pbrpmb` (so hardware
diagnostics survive the switch into the eMMC root), and any modules supplied in
--modules under /lib/modules/<krel>.

`has_journal` is deliberately off: the initramfs may mount this image read-only
with `noload`, and a journal-free filesystem cannot replay anything on a
read-only mount.  The switch_root path mounts it read-write (our own data).

Usage:
    python3 tools/build_rootfs.py [--size-mb 64] [--out PATH]
                                  [--busybox PATH] [--busybox-links PATH]
                                  [--modules DIR] [--krel 6.15.11]
                                  [--cross arm-linux-]

The image is not byte-reproducible across builds (mke2fs stamps mkfs time into
the superblock), so the script prints its sha256 for the report.
"""
import argparse
import glob
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = ROOT + '/rootfs'
DEFAULT_OUT = ROOT + '/local/images/rootfs-region1.ext4'
DEFAULT_UUID = '5b10c0de-0000-4b00-8000-000000000018'
EPOCH = 1767225600            # 2026-01-01T00:00:00Z, for a stable mtime
BS = 0x10000                  # loader block
RAW_SHIFT = 0x15
DEST_RAW = 0x29000000         # region 1 + 16 MiB


def fail(msg):
    raise SystemExit('build_rootfs: ' + msg)


def find_busybox(explicit):
    if explicit:
        if not os.path.isfile(explicit):
            fail('--busybox %s not found' % explicit)
        return explicit
    cands = [ROOT + '/local/work/busybox/busybox'] + \
            sorted(glob.glob(ROOT + '/local/work/src/busybox-*/busybox'))
    for c in cands:
        if os.path.isfile(c):
            return c
    fail('no BusyBox binary found; build one into local/work/src/busybox-* '
         'or pass --busybox PATH')


def find_links(busybox, explicit):
    if explicit:
        if not os.path.isfile(explicit):
            fail('--busybox-links %s not found' % explicit)
        return explicit
    cand = os.path.join(os.path.dirname(busybox), 'busybox.links')
    return cand if os.path.isfile(cand) else None


def kernel_release(explicit):
    if explicit:
        return explicit
    p = ROOT + '/local/kernel-tree/include/config/kernel.release'
    try:
        return open(p).read().strip()
    except OSError:
        return '6.15.11'


def cc_for(cross):
    cc = cross + 'gcc'
    if not shutil.which(cc):
        fail('%s not on PATH (run ". tools/env.sh" first)' % cc)
    return cc


def build_static(cc, source, out):
    subprocess.run([cc, '-static', '-O2', '-Wall', '-o', out, source],
                   check=True, cwd=ROOT)


def install_busybox(stage, busybox, links):
    dst = stage + '/bin/busybox'
    shutil.copy2(busybox, dst)
    os.chmod(dst, 0o755)
    if not links:
        # No generated list: the essentials are enough to boot and repair.
        links = ['/bin/sh', '/bin/ls', '/bin/mount', '/bin/cat',
                 '/sbin/init', '/sbin/reboot', '/sbin/insmod']
        lines = links
    else:
        lines = [l.strip() for l in open(links) if l.strip()]

    made = 0
    for path in lines:
        path = path.lstrip('/')
        target = stage + '/' + path
        if os.path.lexists(target):
            continue
        d = os.path.dirname(target)
        os.makedirs(d, exist_ok=True)
        rel = os.path.relpath(dst, d)
        os.symlink(rel, target)
        made += 1
    return made, len(lines)


def install_modules(stage, modules_dir, krel):
    base = '%s/lib/modules/%s' % (stage, krel)
    os.makedirs(base + '/kernel', exist_ok=True)
    n = 0
    if modules_dir and os.path.isdir(modules_dir):
        for dp, _, fns in os.walk(modules_dir):
            for fn in fns:
                if not fn.endswith('.ko'):
                    continue
                src = os.path.join(dp, fn)
                rel = os.path.relpath(src, modules_dir)
                if rel.startswith('..'):
                    rel = fn
                dst = base + '/kernel/' + rel
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(src, dst)
                n += 1
    return base, n


def install_firmware(stage, firmware_dir):
    """Copy a firmware tree into /lib/firmware (layout preserved).

    The linux-firmware blobs are not in the repository (they are not ours to
    redistribute here); the caller points at a directory such as
    local/work/firmware that holds e.g. ti-connectivity/wl128x-fw-5-sr.bin.
    """
    n = 0
    if firmware_dir and os.path.isdir(firmware_dir):
        for dp, _, fns in os.walk(firmware_dir):
            for fn in fns:
                src = os.path.join(dp, fn)
                rel = os.path.relpath(src, firmware_dir)
                dst = stage + '/lib/firmware/' + rel
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(src, dst)
                n += 1
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--size-mb', type=int, default=64)
    ap.add_argument('--out', default=DEFAULT_OUT)
    ap.add_argument('--cross', default=os.environ.get('CROSS_COMPILE', 'arm-linux-'))
    ap.add_argument('--uuid', default=DEFAULT_UUID)
    ap.add_argument('--busybox', default=None)
    ap.add_argument('--busybox-links', default=None)
    ap.add_argument('--modules', default=ROOT + '/local/work/modules')
    ap.add_argument('--firmware', default=None,
                    help='directory copied to /lib/firmware (layout preserved)')
    ap.add_argument('--krel', default=None)
    ap.add_argument('--keep-stage', action='store_true')
    args = ap.parse_args()

    if args.size_mb < 16:
        fail('--size-mb must be at least 16')
    if DEST_RAW % BS:
        fail('DEST_RAW is not block aligned')

    cc = cc_for(args.cross)
    for tool in ('mke2fs', 'depmod'):
        if not shutil.which(tool):
            fail('%s not found' % tool)

    busybox = find_busybox(args.busybox)
    links = find_links(busybox, args.busybox_links)
    krel = kernel_release(args.krel)

    stage = tempfile.mkdtemp(prefix='pbroot-')
    try:
        for d, mode in (('bin', 0o755), ('sbin', 0o755), ('usr/bin', 0o755),
                        ('usr/sbin', 0o755), ('etc', 0o755),
                        ('etc/init.d', 0o755), ('lib', 0o755),
                        ('proc', 0o755), ('sys', 0o755), ('dev', 0o755),
                        ('tmp', 0o1777), ('run', 0o755), ('root', 0o700),
                        ('var/log', 0o755), ('home', 0o755)):
            p = stage + '/' + d
            os.makedirs(p, exist_ok=True)
            os.chmod(p, mode)

        # The userland.
        nlinks, ntotal = install_busybox(stage, busybox, links)

        # /sbin/init must exist before the first boot; BusyBox owns it.
        init = stage + '/sbin/init'
        if not os.path.lexists(init):
            os.symlink('../bin/busybox', init)

        # Init scripts and profile.
        for rel in ('etc/inittab', 'etc/init.d/rcS', 'etc/profile', 'etc/motd',
                    'etc/pbroot-version'):
            src = SRC + '/' + rel
            if os.path.exists(src):
                os.makedirs(os.path.dirname(stage + '/' + rel), exist_ok=True)
                shutil.copy2(src, stage + '/' + rel)
        os.chmod(stage + '/etc/init.d/rcS', 0o755)

        # The pre-switch diagnostic /init, and the tools that must survive it.
        build_static(cc, SRC + '/init.c', stage + '/init')
        build_static(cc, ROOT + '/initramfs/console.c', stage + '/bin/pbconsole')
        build_static(cc, ROOT + '/initramfs/rpmb.c', stage + '/bin/pbrpmb')
        if os.path.exists(SRC + '/pbplay.c'):
            build_static(cc, SRC + '/pbplay.c', stage + '/bin/pbplay')
        if os.path.exists(SRC + '/pbmix.c'):
            build_static(cc, SRC + '/pbmix.c', stage + '/bin/pbmix')
        if os.path.exists(SRC + '/hci.c'):
            build_static(cc, SRC + '/hci.c', stage + '/bin/pbhci')

        # Modules and their dependency map.
        modbase, nmods = install_modules(stage, args.modules, krel)
        if nmods:
            subprocess.run(['depmod', '-b', stage, krel], check=False,
                           capture_output=True)

        # Stable timestamps so builds differ only in the superblock.
        for dirpath, dirnames, filenames in os.walk(stage):
            for name in dirnames + filenames + ['.']:
                p = os.path.join(dirpath, name)
                if not os.path.islink(p):
                    os.utime(p, (EPOCH, EPOCH))

        out = os.path.abspath(args.out)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        if os.path.exists(out):
            os.unlink(out)

        blocks = args.size_mb * 1024        # 1 KiB blocks
        cmd = ['fakeroot', 'mke2fs', '-q', '-F', '-t', 'ext4', '-b', '1024',
               '-L', 'pbroot', '-m', '0', '-U', args.uuid,
               '-E', 'root_owner=0:0,hash_seed=%s,lazy_itable_init=0,'
                     'lazy_journal_init=0' % args.uuid,
               '-O', '^has_journal',
               '-d', stage, out, str(blocks)]
        subprocess.run(cmd, check=True)

        data = open(out, 'rb').read()
        print('rootfs   : %s' % out)
        print('size     : %d bytes (%d MiB), ext4, no journal, label pbroot'
              % (len(data), args.size_mb))
        print('sha256   : %s' % hashlib.sha256(data).hexdigest())
        print('busybox  : %s (%d/%d applet links)'
              % (os.path.relpath(busybox, ROOT), nlinks, ntotal))
        print('modules  : %d in /lib/modules/%s' % (nmods, krel))
        print('dest     : raw 0x%X -> loader block 0x%X (region 1 + %d MiB)'
              % (DEST_RAW, DEST_RAW // BS + RAW_SHIFT,
                 (DEST_RAW - 0x28000000) // (1 << 20)))
        blocks_used = (len(data) + BS - 1) // BS
        print('flash    : python3 tools/bbusb.py flash --component %s '
              '--dest-block 0x%X' % (os.path.relpath(out, ROOT),
                                     DEST_RAW // BS + RAW_SHIFT))
        print('           (%d loader blocks; bbusb.py is the current flasher)'
              % blocks_used)
        return 0
    finally:
        if args.keep_stage:
            print('stage    : %s (kept)' % stage)
        else:
            shutil.rmtree(stage, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
