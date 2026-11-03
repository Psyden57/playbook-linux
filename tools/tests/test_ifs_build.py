#!/usr/bin/env python3
"""Acceptance test for tools/ifs_build.py (IFS-REBUILD.md milestone 1).

No device, no USB.  Two layers:

  1. the built-in synthetic selftest always runs;
  2. if the vendor 2.1.0.1917 IFS and QNX's dumpifs are available, the full
     round trip runs: extract the image, capture a `dumpifs -vv` listing,
     rebuild, and require dumpifs to list the same 72 files + 26 symlinks
     (+ root) with the same names, sizes and modes, and a Checksums line.

The vendor image is looked up in $PB_IFS_1917, then $BB10MT, then the known
bench path.  When it is absent the round trip is SKIPped (exit 0); the public
snapshot has neither the vendor image nor the QNX toolchain.

Run:  python3 tools/tests/test_ifs_build.py
Exit: 0 = all passed (or skipped), 1 = at least one failure.
"""

import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import ifs_build                               # noqa: E402

failures = []


def check(name, cond, detail=''):
    print(('PASS ' if cond else 'FAIL ') + name +
          (('  ' + detail) if detail and not cond else ''))
    if not cond:
        failures.append(name)


def vendor_image():
    cand = []
    if os.environ.get('PB_IFS_1917'):
        cand.append(os.environ['PB_IFS_1917'])
    if os.environ.get('BB10MT'):
        cand.append(os.path.join(os.environ['BB10MT'],
                                 'PlayBook_2.1.0.1917.4.ifs'))
    cand.append(os.path.expanduser(
        '~/Pdrive/bb10mt/PlayBook_2.1.0.1917.4.ifs'))
    for p in cand:
        if os.path.isfile(p):
            return p
    return None


def main():
    # ---- layer 1: synthetic selftest ---------------------------------------
    rc = ifs_build.main(['selftest'])
    check('ifs_build selftest', rc == 0)

    dumpifs = ifs_build.find_dumpifs()
    vendor = vendor_image()
    if not vendor or not dumpifs:
        print('SKIP 1917 round trip (vendor image or dumpifs not found)')
        return 0 if not failures else 1

    tmp = tempfile.mkdtemp(prefix='ifs_build_accept_')
    try:
        tree = os.path.join(tmp, 'tree')
        listing = os.path.join(tmp, 'stock.list')
        out = os.path.join(tmp, 'out.ifs')

        ex = subprocess.run([os.path.join(ROOT, 'tools', 'ifs_extract.sh'),
                             vendor, tree], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
        check('extract vendor image', ex.returncode == 0,
              ex.stdout.decode('latin-1')[-300:])

        rc = ifs_build.main(['listing', vendor, '--out', listing])
        check('capture reference listing', rc == 0)

        rc = ifs_build.main(['build', '--tree', tree, '--listing', listing,
                             '--stock', vendor, '--out', out, '--quiet'])
        check('build rebuilt image', rc == 0)

        ref_text = open(listing, encoding='latin-1').read()
        ref_hdr, ref_entries = ifs_build.parse_listing(ref_text)
        files = [e for e in ref_entries if e.kind == 'file']
        links = [e for e in ref_entries if e.kind == 'symlink']
        check('reference has 99 dirents', len(ref_entries) == 99,
              str(len(ref_entries)))
        check('reference has 72 files', len(files) == 72, str(len(files)))
        check('reference has 26 symlinks', len(links) == 26, str(len(links)))

        rc = ifs_build.main(['check', '--image', out, '--listing', listing])
        check('dumpifs acceptance check', rc == 0)

        # prefix is RIM's, unchanged except flags1 and the startup checksum
        # field that flags1 invalidates (startup_off+startup_size-4)
        stock = open(vendor, 'rb').read()
        built = open(out, 'rb').read()
        check('prefix length', len(built) == ref_hdr.image_off +
              ifs_build.u32le(built, ref_hdr.image_off + 8))
        diffs = [i for i, (a, b) in enumerate(zip(stock[:ref_hdr.image_off],
                                                  built[:ref_hdr.image_off]))
                 if a != b]
        cks_at = (ref_hdr.startup_off +
                  ifs_build.u32le(built, ref_hdr.startup_off + 0x20) - 4)
        allowed = {ref_hdr.startup_off + ifs_build.STARTUP_FLAGS1_OFF}
        allowed |= set(range(cks_at, cks_at + 4))
        check('prefix unchanged except flags1 + startup checksum',
              set(diffs) <= allowed, repr(diffs))
        check('startup checksum recomputed',
              ifs_build.imagefs_cksum(
                  built[ref_hdr.startup_off:cks_at]) ==
              ifs_build.u32le(built, cks_at))

        # ---- milestone 3: append the OS trailer and reseal with the length
        crc_tool = os.path.join(ROOT, 'tools', '89-crc_tool.py')
        final = os.path.join(tmp, 'final.ifs')
        r = subprocess.run([sys.executable, crc_tool, 'reseal', out, final,
                            '--image-off', '0', '--length', 'auto',
                            '--append-os-trailer', '--type', '6'],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        check('reseal with length', r.returncode == 0,
              r.stdout.decode('latin-1')[-300:])
        f = open(final, 'rb').read()
        ls, le = ifs_build.u32le(f, 4), ifs_build.u32le(f, 8)
        check('LoadEnd == LoadStart + size', le - ls == len(f),
              '0x%X vs 0x%X' % (le - ls, len(f)))
        check('OS cookie at end-8',
              ifs_build.u32le(f, len(f) - 8) == 0xD7C82D1F)
        check('os_type == 6', f[0x2C + 0xD4] == 6)
        r = subprocess.run([sys.executable, crc_tool, 'verify', final,
                            '--image-off', '0'],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        check('CRC path verifies', r.returncode == 0,
              r.stdout.decode('latin-1')[-300:])

        # ---- milestone 2: the shim builds for ARM when the toolchain is there
        gcc = shutil.which('ntoarmv7-gcc')
        if gcc:
            shim = os.path.join(tmp, 'shim')
            r = subprocess.run([gcc, '-O2', '-Wall', '-o', shim,
                                os.path.join(ROOT, 'tools',
                                             'rfs_validator_shim.c')],
                               stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
            check('shim builds with ntoarmv7-gcc', r.returncode == 0,
                  r.stdout.decode('latin-1')[-300:])
            if r.returncode == 0:
                head = open(shim, 'rb').read(20)
                check('shim is ARM EABI5', head[:4] == b'\x7fELF' and
                      head[18:20] == b'\x28\x00', head.hex())
        else:
            print('SKIP shim build (ntoarmv7-gcc not on PATH)')
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    if failures:
        print('test_ifs_build: FAILED (%d)' % len(failures))
        return 1
    print('test_ifs_build: PASSED')
    return 0


if __name__ == '__main__':
    sys.exit(main())
