#!/usr/bin/env python3
"""Dump / decode the PlayBook DEBUG_LL DRAM ring (capture v2, 32 KB).

Layout (per ring page, ring1 = PA 0x88000000 is the canonical one):
    +0x00 .. +0x7C   chainloader / stage breadcrumb words
    +0x80            u32 monotonic character count
    +0x84            u32 wrapping write index (0 .. 0x7EFF)
    +0x100 .. +0x7FFF 32512 characters

The kernel flushes ring1 (L1 DCCMVAC + PL310 clean/invalidate by PA) every
8 chars, so reading PA 0x88000000 gives the real boot tail.  The decompressor
still writes ring2 (0x90000000) before the kernel starts.

Usage:
    pb_ring.py dump [--ring 88000000] [--out FILE]
    pb_ring.py decode FILE
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

RING_BASE = 0x88000000
SIZE = 0x7F00
CHARS = 0x100
HEADER_LEN = 0x8

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
    count = words.get(ring + 0x80, 0)
    index = words.get(ring + 0x84, 0)
    raw = bytearray()
    for i in range(size):
        w = words.get(ring + CHARS + (i & ~3), 0)
        raw.append((w >> (8 * (i & 3))) & 0xFF)

    if count <= 0 or count > 1 << 28:
        raise SystemExit(f'implausible count 0x{count:08x} (index 0x{index:08x})')
    if index >= size:
        raise SystemExit(f'implausible index 0x{index:08x}')

    if count < size:
        data = raw[:count]
    else:
        data = raw[index:size] + raw[:index]
    return count, index, bytes(data)


def to_text(data):
    return ''.join(
        chr(c) if 32 <= c < 127 else ('\n' if c in (0, 10, 13) else '.')
        for c in data
    )


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    d = sub.add_parser('dump')
    d.add_argument('--ring', default='88000000')
    d.add_argument('--out')
    dec = sub.add_parser('decode')
    dec.add_argument('file')

    args = ap.parse_args()

    if args.cmd == 'dump':
        ring = int(args.ring, 16)
        words = dump(ring)
        count, index, data = decode(words, ring)
        text = to_text(data)
        if args.out:
            with open(args.out, 'w') as f:
                f.write(f'# count=0x{count:x} ({count}) index=0x{index:x}\n')
                f.write(text)
                f.write('\n')
            print(f'count={count} index=0x{index:x} -> {args.out}')
        else:
            print(f'# count=0x{count:x} ({count}) index=0x{index:x}')
            print(text)
        return

    words = {}
    for line in open(args.file):
        m = re.match(r'\s*([0-9a-fA-F]{8}):\s*([0-9a-fA-F]{8})', line)
        if m:
            words[int(m.group(1), 16)] = int(m.group(2), 16)
    if not words:
        raise SystemExit('no words parsed')
    ring = min(words) & ~0xFFFF
    count, index, data = decode(words, ring)
    print(f'# count=0x{count:x} ({count}) index=0x{index:x} ring=0x{ring:08x}')
    print(to_text(data))


if __name__ == '__main__':
    main()
