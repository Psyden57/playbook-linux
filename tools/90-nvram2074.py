#!/usr/bin/env python3
"""90-nvram2074.py -- host-side, read-only constructor for the Phase 1
`0x2074` (NV_OSSTORE_OS_REGION / OsRegionV3) patch.

No device access. It only:
  1. decodes the live 72-byte record captured in
     analysis/playbook-live-2026-10-21/after-0x2074.txt,
  2. builds the exact Phase 1 record (requested=1, actual=0, counts 0,
     max 0, flags preserved, epoch=given, version IDs preserved),
  3. prints the equivalent `nvram patch` command sequence,
  4. prints the exact expected read-back,
  5. prints the post-boot (bootloader-written) record and the restore set.

struct ('=IBBHHHIQ24s24s') is native byte order (LE on ARM) with no padding,
so struct.Struct('=...') on x86-64 gives identical bytes.

Usage:
    python3 90-nvram2074.py [--epoch N] [--patch-only]
"""

import argparse
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
LIVE = os.path.join(HERE, 'playbook-live-2026-10-21', 'after-0x2074.txt')

FMT = struct.Struct('=IBBHHHIQ24s24s')          # 72 bytes
assert FMT.size == 72, FMT.size

NV_OSSTORE_OS_REGION = 8308                     # 0x2074, osregion.py:206/49
MAX_BOOT_COUNT = 0                              # committed image = unlimited
DEF_BOOT_COUNT = 4                              # budget a switch() grants

FIELDS = ['version', 'requested_region', 'actual_region', 'boot_count_0',
          'boot_count_1', 'max_boot_count', 'flags', 'epoch',
          'version_id_0', 'version_id_1']


def decode(raw):
    vals = FMT.unpack(raw)
    return dict(zip(FIELDS, vals))


def encode(rec):
    return FMT.pack(*[rec[f] for f in FIELDS])


def hexdump(raw, base=0):
    out = []
    for off in range(0, len(raw), 16):
        chunk = raw[off:off + 16]
        out.append('%04x  %-47s  %s' % (
            base + off,
            ' '.join('%02x' % b for b in chunk),
            ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)))
    return '\n'.join(out)


def read_live():
    with open(LIVE) as f:
        lines = [ln.strip() for ln in f if ln.strip()]
    hexline = ' '.join(lines[2:])
    raw = bytes(int(t, 16) for t in hexline.split())
    if len(raw) != 72:
        raise SystemExit('live record is %d bytes, expected 72' % len(raw))
    return raw


def show(tag, rec):
    print('%s' % tag)
    for k in FIELDS:
        v = rec[k]
        if isinstance(v, bytes):
            v = v.rstrip(b'\x00')
            print('    +%-2s %-18s %r' % ('', k, v))
        else:
            print('    %-18s %d (0x%x)' % (k, v, v) if k != 'epoch'
                  else '    %-18s %d (0x%x) %s' % (
                      k, v, v,
                      time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(v))
                      if v else '(grace: is_grace() always true)'))


def patch_cmds(want, live):
    """Minimal set of `nvram patch` commands.

    Deliberately never touches +0x0C..+0x0F (flags) or +0x18..+0x47 (version
    IDs), so those stay bit-identical by construction rather than by value.
    """
    cmds = []
    if want['requested_region'] != live['requested_region']:
        cmds.append(('requested_region (+0x04)',
                     './nvram patch 0x2074 0x04 0x%02x' % want['requested_region']))
    counts = [want['boot_count_0'], want['boot_count_1'], want['max_boot_count']]
    if counts != [live['boot_count_0'], live['boot_count_1'], live['max_boot_count']]:
        cmds.append(('counts + max (+0x06..+0x0B)',
                     './nvram patch 0x2074 0x06 ' +
                     ' '.join('0x%02x' % b for b in struct.pack('<HHH', *counts))))
    if want['epoch'] != live['epoch']:
        lo = want['epoch'] & 0xFFFFFFFF
        hi = (want['epoch'] >> 32) & 0xFFFFFFFF
        cmds.append(('epoch (+0x10..+0x17)',
                     './nvram patch 0x2074 0x10 0x%08x 0x%08x' % (lo, hi)))
    return cmds


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--epoch', type=lambda s: int(s, 0), default=None)
    ap.add_argument('--patch-only', action='store_true')
    args = ap.parse_args()

    epoch = args.epoch if args.epoch is not None else int(time.time())
    live_raw = read_live()
    live = decode(live_raw)

    print('== live record (analysis/playbook-live-2026-10-21/after-0x2074.txt)')
    print(hexdump(live_raw))
    show('fields:', live)

    phase1 = dict(live)
    phase1.update(version=3, requested_region=1, actual_region=0,
                  boot_count_0=0, boot_count_1=0, max_boot_count=0,
                  epoch=epoch)
    raw1 = encode(phase1)
    assert len(raw1) == 72
    assert phase1['version_id_0'] == live['version_id_0']
    assert phase1['version_id_1'] == live['version_id_1']
    assert phase1['flags'] == live['flags']

    print('\n== Phase 1 target record (72 bytes)')
    print(hexdump(raw1))
    show('fields:', phase1)

    diff = [i for i in range(72) if raw1[i] != live_raw[i]]
    print('\nbytes that change vs live: %s' %
          ', '.join('0x%02x:%02x->%02x' % (i, live_raw[i], raw1[i]) for i in diff))

    print('\nepoch = %d (0x%x); grace window ends %d (0x%x) = %s'
          % (epoch, epoch, epoch + 172800, epoch + 172800,
             time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(epoch + 172800))))
    print('device-side epoch (preferred; avoids host/unit clock skew):')
    print('  E=$(date +%s); ./nvram patch 0x2074 0x10 '
          '0x$(printf %08x $((E & 0xffffffff))) 0x$(printf %08x $((E >> 32)))')

    print('\n== (a) nvram patch sequence  [MAIN SESSION ONLY]')
    for why, cmd in patch_cmds(phase1, live):
        print('  # %s' % why)
        print('  ' + cmd)
    if not args.patch_only:
        print('\n== read-back (must match the 72 bytes above exactly)')
        print('  ./nvram partialread 0x2074 0 72 hex')
        print('  cat /pps/system/nvram/osregion      # RequestedBootRegion=1 '
              'ActualBootRegion=0 MaxBootCount=0')

        print('\n== predicted record AFTER the Phase 1 boot (bootloader write-back)')
        after = dict(phase1)
        after['boot_count_0'] = (phase1['boot_count_0'] + 1) & 0xFFFF
        native = dict(phase1)
        # binary reading (0x800144d8 bne): clear only when requested == winner
        if native['requested_region'] == 0:
            native['boot_count_1'] = 0
        print(hexdump(encode(after)))
        show('fields:', after)

        print('\n== (b) Platform.switch() equivalent')
        sw = dict(live)
        sw.update(requested_region=live['actual_region'] ^ 1,
                  boot_count_0=0, boot_count_1=0,
                  max_boot_count=DEF_BOOT_COUNT, epoch=epoch)
        print(hexdump(encode(sw)))
        follow = dict(sw)
        follow['max_boot_count'] = MAX_BOOT_COUNT
        print('  follow-up required (switch() grants a 4-boot budget):')
        print('    ./nvram patch 0x2074 0x0a 0x00 0x00')
        print('  result must equal the Phase 1 record: %s'
              % ('YES' if encode(follow) == raw1 else 'NO'))

        print('\n== restore-to-sane set  [MAIN SESSION ONLY]')
        sane = dict(phase1)
        sane.update(requested_region=0, actual_region=0, boot_count_0=0,
                    boot_count_1=0, max_boot_count=0, epoch=0)
        print('-- variant A: spec "sane" restore (counts 0, max 0, epoch 0)')
        for why, cmd in patch_cmds(sane, after):
            print('  # %s' % why)
            print('  ' + cmd)
        print(hexdump(encode(sane)))
        print('  byte-identical to the pre-test baseline record: %s '
              '(differs only at +0x06/+0x07 = boot_count_0)'
              % ('YES' if encode(sane) == live_raw else 'NO'))

        print('\n-- variant B: exact pre-test baseline (restores boot_count_0)')
        exact = dict(live)
        for why, cmd in patch_cmds(exact, after):
            print('  # %s' % why)
            print('  ' + cmd)
        print(hexdump(encode(exact)))
        print('  byte-identical to the pre-test baseline record: %s'
              % ('YES' if encode(exact) == live_raw else 'NO'))


if __name__ == '__main__':
    main()
