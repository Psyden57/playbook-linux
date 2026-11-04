#!/usr/bin/env python3
"""NRV2B (UCL) block codec, as used by QNX mkifs imagefs compression.

WHY THIS EXISTS
  A BB10 imagefs (10.3.03.2049: 13.7 MB) does not fit the PlayBook's 11.0 MiB
  region-0 IFS slot uncompressed, and the stock image stores it as a chain of
  independently compressed 64 KiB blocks, each prefixed with a 2-byte
  big-endian compressed length (reports 171/172).  The QNX 6.6 SDP ships only a
  QNX-ABI static libucl, so this module provides a host-side encoder.

PROVENANCE
  The token coding follows UCL 0.91 (n2_99.ch, code_match/code_run/
  code_prefix_ss11; n2b_d.c for the decoder; getbit.h for the bit packing),
  Copyright (C) 1996-2002 Markus Franz Xaver Johannes Oberhumer, GPL-2+.
  This file is a Python re-implementation, not a copy; see DECISIONS.md for the
  project's licence position.

USAGE
  tools/ucl_nrv2b.py selftest
  tools/ucl_nrv2b.py encode   <in> <out>          # one raw NRV2B stream
  tools/ucl_nrv2b.py decode   <in> <out> <len>
  tools/ucl_nrv2b.py stream   <in> <out>          # [u16 BE len][block] chain

The stream command is what an IFS imagefs payload uses: 64 KiB input blocks,
compressed independently, each prefixed with its big-endian length.
"""

import struct
import sys

M2_MAX_OFFSET = 0xD00
MAX_MATCH = 2048
CHUNK = 65536


class BitWriter:
    """Control bytes are reserved lazily: on the first bit after exhaustion.

    Reserving the placeholder when the previous control byte fills would place
    it *before* data bytes that follow the last bit (e.g. the match offset low
    byte), which the decoder reads before it needs another control byte.
    """

    def __init__(self):
        self.out = bytearray()
        self.pos = None
        self.bits = 0
        self.count = 0

    def bit(self, b):
        if self.count == 0:
            self.pos = len(self.out)
            self.out.append(0)
        self.bits = (self.bits << 1) | (b & 1)
        self.count += 1
        if self.count == 8:
            self.out[self.pos] = self.bits
            self.bits = 0
            self.count = 0

    def byte(self, b):
        self.out.append(b & 0xFF)

    def flush(self):
        if self.count:
            self.out[self.pos] = (self.bits << (8 - self.count)) & 0xFF
            self.count = 0


def code_prefix_ss11(w, i):
    if i >= 2:
        t = 4
        i += 2
        while i >= t:
            t <<= 1
        t >>= 1
        while True:
            t >>= 1
            w.bit(1 if (i & t) else 0)
            w.bit(0)
            if t <= 2:
                break
    w.bit(i & 1)
    w.bit(1)


def code_match(w, m_len, m_off, last_m_off):
    w.bit(0)
    if m_off == last_m_off:
        w.bit(0)
        w.bit(1)
    else:
        code_prefix_ss11(w, 1 + ((m_off - 1) >> 8))
        w.byte(m_off - 1)
    m_len = m_len - 1 - (1 if m_off > M2_MAX_OFFSET else 0)
    if m_len >= 4:
        w.bit(0)
        w.bit(0)
        code_prefix_ss11(w, m_len - 4)
    else:
        w.bit(1 if m_len > 1 else 0)
        w.bit(m_len & 1)
    return m_off


def encode(data, chain=8, nice=MAX_MATCH):
    """One raw NRV2B stream (no length prefix)."""
    w = BitWriter()
    n = len(data)
    last_m_off = 1
    lit_start = 0
    lit_len = 0
    table = {}
    i = 0

    def find(p):
        if p + 2 >= n:
            return 0, 0
        key = data[p] << 16 | data[p + 1] << 8 | data[p + 2]
        cand = table.get(key)
        best_len = 0
        best_off = 0
        if cand:
            for q in reversed(cand[-chain:]):
                off = p - q
                if off <= 0 or off > 0xFFFF:
                    continue
                # compare
                ln = 0
                lim = min(nice, n - p)
                while ln < lim and data[q + ln] == data[p + ln]:
                    ln += 1
                if ln > best_len:
                    best_len = ln
                    best_off = off
                    if ln >= nice:
                        break
        return best_len, best_off

    def insert(p):
        if p + 2 < n:
            key = data[p] << 16 | data[p + 1] << 8 | data[p + 2]
            lst = table.get(key)
            if lst is None:
                table[key] = [p]
            else:
                lst.append(p)
                if len(lst) > 64:
                    del lst[:-32]

    while i < n:
        m_len, m_off = find(i)
        if m_len >= 3 or (m_len == 2 and m_off and m_off <= M2_MAX_OFFSET):
            if lit_len:
                for k in range(lit_start, lit_start + lit_len):
                    w.bit(1)
                    w.byte(data[k])
                lit_len = 0
            last_m_off = code_match(w, m_len, m_off, last_m_off)
            for p in range(i, i + m_len):
                insert(p)
            i += m_len
        else:
            if lit_len == 0:
                lit_start = i
            lit_len += 1
            insert(i)
            i += 1

    if lit_len:
        for k in range(lit_start, lit_start + lit_len):
            w.bit(1)
            w.byte(data[k])

    # EOF
    w.bit(0)
    code_prefix_ss11(w, 0x1000000)
    w.byte(0xFF)
    w.flush()
    return bytes(w.out)


def decode(src, out_len=None):
    """Port of ucl n2b_d.c (getbit_8), stops at the EOF marker."""
    bb = 0
    ilen = 0
    last_m_off = 1
    dst = bytearray()
    slen = len(src)

    def getbit():
        nonlocal bb, ilen
        if bb & 0x7F:
            bb = bb * 2
        else:
            if ilen >= slen:
                raise ValueError("input overrun")
            bb = src[ilen] * 2 + 1
            ilen += 1
        return (bb >> 8) & 1

    while True:
        while getbit():
            if ilen >= slen:
                raise ValueError("input overrun")
            dst.append(src[ilen])
            ilen += 1
        m_off = 1
        while True:
            m_off = m_off * 2 + getbit()
            if getbit():
                break
        if m_off == 2:
            m_off = last_m_off
        else:
            if ilen >= slen:
                raise ValueError("input overrun")
            m_off = (m_off - 3) * 256 + src[ilen]
            ilen += 1
            if m_off == 0xFFFFFFFF:
                break
            last_m_off = m_off + 1
            m_off = last_m_off
        m_len = getbit() * 2 + getbit()
        if m_len == 0:
            m_len = 1
            while True:
                m_len = m_len * 2 + getbit()
                if getbit():
                    break
            m_len += 2
        if m_off > M2_MAX_OFFSET:
            m_len += 1
        pos = len(dst) - m_off
        if pos < 0:
            raise ValueError("lookbehind overrun")
        for k in range(m_len + 1):
            dst.append(dst[pos + k])
        if out_len is not None and len(dst) > out_len:
            raise ValueError("output overrun")
    return bytes(dst)


def compress_stream(data):
    """[u16 BE length][NRV2B block] chain over 64 KiB blocks (mkifs layout)."""
    out = bytearray()
    pos = 0
    n = len(data)
    while pos < n:
        take = min(CHUNK, n - pos)
        while True:
            blob = encode(data[pos:pos + take])
            if len(blob) <= 0xFFFF or take <= 1:
                break
            take -= 0x1000
        if len(blob) > 0xFFFF:
            raise ValueError("block does not fit 16 bits")
        out += struct.pack(">H", len(blob))
        out += blob
        pos += take
    return bytes(out)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    cmd = sys.argv[1]
    if cmd == "selftest":
        import os
        for data in (b"", b"a", b"abc" * 1000, bytes(range(256)) * 64,
                     os.urandom(4096), (b"QNX" * 5000) + os.urandom(5000)):
            enc = encode(data)
            dec = decode(enc)
            if dec != data:
                print("FAIL round trip len", len(data))
                return 1
        blob = compress_stream(b"imagefs" + bytes(200000))
        print("selftest PASSED (round trips + stream %d bytes)" % len(blob))
        return 0
    if cmd == "encode":
        data = open(sys.argv[2], "rb").read()
        open(sys.argv[3], "wb").write(encode(data))
        return 0
    if cmd == "decode":
        data = open(sys.argv[2], "rb").read()
        out = decode(data, int(sys.argv[4], 0))
        open(sys.argv[3], "wb").write(out)
        return 0
    if cmd == "stream":
        data = open(sys.argv[2], "rb").read()
        out = compress_stream(data)
        open(sys.argv[3], "wb").write(out)
        print("stream: %d -> %d bytes (%.3f)" % (len(data), len(out), len(out) / max(1, len(data))))
        return 0
    print("unknown command", cmd)
    return 2


if __name__ == "__main__":
    sys.exit(main())
