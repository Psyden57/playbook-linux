#!/usr/bin/env python3
"""Build the region-1 Linux root filesystem (ext4, no journal).

The image is written to media region 1 (/dev/hd2, raw 0x28000000) at region-1
offset 16 MiB -- raw 0x29000000, loader block 0x2915 -- which is inside the
signed loader's 0x1B allow-list, so `pre_os_ifs_flash.py --payload` can place it
without an MCT edit (reports 98 §4, 134 §6, 138).

`has_journal` is deliberately off: the initramfs mounts this image read-only
with `noload`, and a journal-free filesystem cannot replay anything on a
read-only mount (the ext4 caveat in
docs/contradictions/safety-no-emmc-mount-vs-readonly-root.md).

Usage:
    python3 tools/build_rootfs.py [--size-mb 32] [--out PATH]
                                  [--cross arm-linux-] [--uuid UUID]

The image is not byte-reproducible across builds (mke2fs stamps mkfs time into
the superblock), so the script prints its sha256 for the report.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import time

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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--size-mb', type=int, default=32)
    ap.add_argument('--out', default=DEFAULT_OUT)
    ap.add_argument('--cross', default=os.environ.get('CROSS_COMPILE', 'arm-linux-'))
    ap.add_argument('--uuid', default=DEFAULT_UUID)
    ap.add_argument('--keep-stage', action='store_true')
    args = ap.parse_args()

    if args.size_mb < 8:
        fail('--size-mb must be at least 8')
    if DEST_RAW % BS:
        fail('DEST_RAW is not block aligned')

    cc = args.cross + 'gcc'
    if not shutil.which(cc):
        fail('%s not on PATH (run ". tools/env.sh" first)' % cc)
    for tool in ('mke2fs', 'sha256sum'):
        if not shutil.which(tool):
            fail('%s not found' % tool)

    stage = tempfile.mkdtemp(prefix='pbroot-')
    try:
        os.makedirs(stage + '/etc', exist_ok=True)
        shutil.copy2(SRC + '/etc/pbroot-version', stage + '/etc/pbroot-version')

        init = stage + '/init'
        subprocess.run([cc, '-static', '-O2', '-Wall', '-o', init,
                        SRC + '/init.c'], check=True, cwd=ROOT)

        # Stable timestamps so 10 builds differ only in the superblock.
        for dirpath, dirnames, filenames in os.walk(stage):
            for name in dirnames + filenames + ['.']:
                p = os.path.join(dirpath, name)
                os.utime(p, (EPOCH, EPOCH))

        out = os.path.abspath(args.out)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        if os.path.exists(out):
            os.unlink(out)

        blocks = args.size_mb * 1024        # 1 KiB blocks
        cmd = ['mke2fs', '-q', '-F', '-t', 'ext4', '-b', '1024',
               '-L', 'pbroot', '-m', '0', '-U', args.uuid,
               '-E', 'root_owner=0:0,hash_seed=%s,lazy_itable_init=0,'
                     'lazy_journal_init=0' % args.uuid,
               '-O', '^has_journal',
               '-d', stage, out, str(blocks)]
        subprocess.run(cmd, check=True)

        # The build runs unprivileged, so -d records the host uid.  A rootfs
        # should be root-owned; set it in the image (the bytes, not the host
        # file) for the handful of paths this tree has.  debugfs needs one
        # command per line via -f; a ';'-joined -R request is not parsed.
        owned = ['/', '/init', '/etc', '/etc/pbroot-version', '/lost+found']
        dbg = os.path.join(os.path.dirname(out), '.pbroot-debugfs')
        with open(dbg, 'w') as f:
            for p in owned:
                f.write('sif "%s" uid 0\nsif "%s" gid 0\n' % (p, p))
        try:
            subprocess.run(['debugfs', '-w', '-f', dbg, out], check=True,
                           capture_output=True)
        finally:
            os.unlink(dbg)

        # Report what was built.
        data = open(out, 'rb').read()
        print('rootfs   : %s' % out)
        print('size     : %d bytes (%d MiB), ext4, no journal, label pbroot'
              % (len(data), args.size_mb))
        print('sha256   : %s' % hashlib.sha256(data).hexdigest())
        print('dest     : raw 0x%X -> loader block 0x%X (region 1 + %d MiB)'
              % (DEST_RAW, DEST_RAW // BS + RAW_SHIFT,
                 (DEST_RAW - 0x28000000) // (1 << 20)))
        blocks_used = (len(data) + BS - 1) // BS
        print('flash    : python3 tools/pre_os_ifs_flash.py --payload %s '
              '--dest-raw 0x%X' % (os.path.relpath(out, ROOT), DEST_RAW))
        print('           (%d loader blocks)' % blocks_used)
        return 0
    finally:
        if args.keep_stage:
            print('stage    : %s (kept)' % stage)
        else:
            shutil.rmtree(stage, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
