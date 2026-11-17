#!/usr/bin/env python3
"""Dump / decode the PlayBook DEBUG_LL DRAM ring (32 KB).

Layout (per ring page, ring2 = PA 0x90000000 is the canonical one — it is the
chainloader's BC_PRIMARY, and the only page whose hand-off record is trusted):
    +0x00 .. +0x7C   chainloader / stage breadcrumb words
    +0x80            u32 monotonic character count
    +0x84            u32 wrapping write index (0 .. 0x7EFF)
    +0x88 .. +0xB3   chainloader epoch block ("EPCH")
    +0xB8            u32 "RHND": ring hand-off record present
    +0xBC / +0xC0    prev  entry count / index  (entry header of THIS boot)
    +0xC4 / +0xC8    prevprev entry count / index
    +0x100 .. +0x7FFF 32512 characters, one append-only stream

Since session 62 the chainloader does NOT clear the ring: a boot keeps writing
where the previous one stopped, and the two entry headers delimit the two most
recent boots.  So after a failed boot is read back through a quiet image:
    previous boot's log = chars from prevprev_index, prev_count-prevprev_count
    this boot's log     = chars from prev_index,     count-prev_count
That is the log you want after a failure (`--boot prev`, the default).

A ring written by an older chainloader has no "RHND" record; it is decoded the
old way (the boot started at character 0 and the count is its length).

There is no separate "+0x1000 printk window": +0x1000 is this character array
at character #3840, which is simply the first byte past the old clear.

Usage:
    pb_ring.py dump [--ring 90000000] [--boot prev|cur|all] [--out FILE]
    pb_ring.py decode FILE [--boot prev|cur|all] [--raw]
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

RING_BASE = 0x90000000
SIZE = 0x7F00
CHARS = 0x100
RHND_OFF = 0xB8
RHND_MAGIC = 0x444E4852      # "RHND" in memory order

ROOT = Path(__file__).resolve().parent.parent
KEY = os.environ.get('PB_KEY', str(ROOT / 'local' / 'secrets' / 'rsa'))

SSH = [
    'ssh', '-i', KEY,
    '-o', 'StrictHostKeyChecking=no', '-o', 'UserKnownHostsFile=/dev/null',
    '-o', 'IdentitiesOnly=yes', '-o', 'HostKeyAlgorithms=+ssh-rsa',
    '-o', 'PubkeyAcceptedKeyTypes=+ssh-rsa', '-o', 'PubkeyAcceptedAlgorithms=+ssh-rsa',
    '-o', 'KexAlgorithms=+diffie-hellman-group1-sha1',
    '-o', 'MACs=+hmac-sha1,hmac-sha1-96',
    '-o', 'Ciphers=+aes128-cbc,3des-cbc,aes256-cbc',
    '-o', 'LogLevel=ERROR', '-o', 'ConnectTimeout=8',
    'root@169.254.0.1',
]


def ssh(cmd):
    return subprocess.run(SSH + [cmd], capture_output=True, check=True).stdout


def dump(ring, chunk=0x1FE0):
    words = {}
    start = ring + 0x80
    end = ring + CHARS + SIZE
    addr = start
    while addr < end:
        n = min(chunk, end - addr)
        out = ssh(f'on -C 0 /accounts/devuser/memdump3 {addr:08x} 0x{n:x}')
        for line in out.decode('ascii', 'replace').splitlines():
            m = re.match(r'\s*([0-9a-fA-F]{8}):\s*([0-9a-fA-F]{8})', line)
            if m:
                words[int(m.group(1), 16)] = int(m.group(2), 16)
        addr += n
    return words


def decode(words, ring=RING_BASE, size=SIZE):
    """Return (count, index, handoff) where handoff is None or a dict."""
    count = words.get(ring + 0x80, 0)
    index = words.get(ring + 0x84, 0)

    if count <= 0 or count > 1 << 28:
        raise SystemExit(f'implausible count 0x{count:08x} (index 0x{index:08x})')
    if index >= size:
        raise SystemExit(f'implausible index 0x{index:08x}')

    handoff = None
    if words.get(ring + RHND_OFF) == RHND_MAGIC:
        pc = words.get(ring + 0xBC, 0)
        pi = words.get(ring + 0xC0, 0)
        ppc = words.get(ring + 0xC4, 0)
        ppi = words.get(ring + 0xC8, 0)
        handoff = {'prev_count': pc, 'prev_index': pi,
                   'prevprev_count': ppc, 'prevprev_index': ppi,
                   'prev_tjump': words.get(ring + 0xCC, 0),
                   'prev_tentry': words.get(ring + 0xD0, 0),
                   'prev_magic': words.get(ring + 0xD4, 0)}
    return count, index, handoff


def chars(raw, start, length, size=SIZE):
    """The ring's own addressing: char n lives at raw[n % size]."""
    if length <= 0:
        return b''
    return bytes(raw[(start + i) % size] for i in range(length))


def logs(handoff, count, index, raw, which='prev', size=SIZE):
    """Yield (label, data) for the boots the header can delimit."""
    if handoff:
        pc, pi = handoff['prev_count'], handoff['prev_index']
        ppc, ppi = handoff['prevprev_count'], handoff['prevprev_index']
        if which in ('all', 'prev'):
            yield 'prev', chars(raw, ppi, pc - ppc, size)
        if which in ('all', 'cur'):
            yield 'cur', chars(raw, pi, count - pc, size)
        return
    if count < size:
        yield 'boot', raw[:count]
    else:
        yield 'boot', raw[index:] + raw[:index]


def to_text(data):
    return ''.join(
        chr(c) if 32 <= c < 127 else ('\n' if c in (0, 10, 13) else '.')
        for c in data
    )


def render(count, index, handoff, words, ring, which):
    """Assemble the raw character store and the requested boot log(s)."""
    raw = bytearray()
    for i in range(SIZE):
        w = words.get(ring + CHARS + (i & ~3), 0)
        raw.append((w >> (8 * (i & 3))) & 0xFF)

    head = f'count=0x{count:x} ({count}) index=0x{index:x} ring=0x{ring:08x}'
    if handoff:
        head += (' handoff prev=({prev_count},{prev_index})'
                 ' prevprev=({prevprev_count},{prevprev_index})'
                 .format(**handoff))
    out = [f'# {head}']

    if handoff and handoff.get('prev_magic') == 0x51000001:
        ldr = words.get(ring + 0xA4, 0xFFFC1800)
        tj, te = handoff['prev_tjump'], handoff['prev_tentry']
        out.append(
            '# prev boot timing: chainloader-entry->jump %.3f s, '
            'jump->kernel-entry (copy/decompress) %.3f s, '
            'elapsed since arming at kernel entry %.3f s of 7.812 s'
            % ((tj - ldr) / 32768.0, (te - tj) / 32768.0,
               (te - ldr) / 32768.0))

    for label, data in logs(handoff, count, index, raw, which):
        out.append(f'# ---- {label}: {len(data)} chars ----')
        out.append(to_text(data).rstrip('\n'))
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    d = sub.add_parser('dump')
    d.add_argument('--ring', default='%08x' % RING_BASE)
    d.add_argument('--boot', default='prev', choices=('prev', 'cur', 'all'))
    d.add_argument('--out')
    dec = sub.add_parser('decode')
    dec.add_argument('file')
    dec.add_argument('--boot', default='prev', choices=('prev', 'cur', 'all'))

    args = ap.parse_args()

    if args.cmd == 'dump':
        ring = int(args.ring, 16)
        words = dump(ring)
    else:
        words = {}
        for line in open(args.file):
            m = re.match(r'\s*([0-9a-fA-F]{8}):\s*([0-9a-fA-F]{8})', line)
            if m:
                words[int(m.group(1), 16)] = int(m.group(2), 16)
        if not words:
            raise SystemExit('no words parsed')
        ring = min(words) & ~0xFFFF

    count, index, handoff = decode(words, ring)
    text = render(count, index, handoff, words, ring, args.boot)

    if getattr(args, 'out', None):
        with open(args.out, 'w') as f:
            f.write(text)
        print(f'count={count} index=0x{index:x} -> {args.out}')
    else:
        sys.stdout.write(text)


if __name__ == '__main__':
    main()
