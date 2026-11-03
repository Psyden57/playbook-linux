#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
89-crc_tool.py -- host-side model of the RIM PlayBook boot-stage OS-region
verifier (hd0 0x800142A8 / 0x80014148) for the *factory / CRC* path.

Implements, byte-exactly:
  * 0x80013ED4  CRC table generator   (poly 0x04C11DB7, reflected -> 0xEDB88320)
  * 0x8001A7B4  CRC init              (mvn r0,#0 -> 0xFFFFFFFF)
  * 0x80013770  CRC update            (crc = tbl[(crc^b)&0xff] ^ (crc>>8))
  * 0x8001A7C8  CRC final             (eor r0,r0,#0xFFFFFFFF)
  * 0x80013FB6  CRC-path gate         (uxtb(desc[0xD4]) == 6)
  * 0x80014788  CRC verify            (crc32 [start,end-4) == u32 at end-4;
                                       stored == 0xFFFFFFFF -> status 7)
  * 0x80018688  ECDSA/SHA-512 trailer (hash [start,end-0x230), 0x230-byte
                                       trailer, u32(end-0x178)==0xB5A60BFD,
                                       u32(end-0x210) != 0 and != 0xFFFFFFFF,
                                       u32(end-8)==0xD7C82D1F)
  * 0x80014610  structural checks     (version >= 0x20000, desc magic
                                       0xC3D0E193 at desc+0x9C, end-8 cookie,
                                       end-4 != 0xFFFFFFFF, self pointers)

READ-ONLY ON DEVICE. This tool only touches host-side files.

Usage
-----
  # self checks (algorithm proof + round trip)
  89-crc_tool.py selftest

  # show the 256-entry table generated exactly like 0x80013ED4
  89-crc_tool.py table

  # CRC-32/ISO-HDLC of a file (or a slice of it)
  89-crc_tool.py crc IMAGE.bin [--offset N] [--length N]

  # parse + fully verify one region image (raw /dev/hdN dump or a slice)
  89-crc_tool.py verify IMAGE.bin [--image-off 0x100000] [--size N]
                                  [--desc-off 0x2C]

  # parse only the 0x200-byte header block captured by the Phase-0 probe
  89-crc_tool.py probe HEADER200.bin

  # reseal: set the OS-type byte to 6 and rewrite the CRC trailer (writes a
  # NEW file; never edits in place)
  89-crc_tool.py reseal IN.bin OUT.bin [--image-off 0x100000] [--size N]
                                       [--desc-off 0x2C] [--type 6]
                                       [--patch OFFSET:HEX ...]

  # finalize an image whose size changed (the IFS rebuild): append the OS
  # trailer, patch LoadEnd, set os_type and write the CRC
  89-crc_tool.py reseal IN.bin OUT.bin --image-off 0 --length auto \
                                       --append-os-trailer

Conventions
-----------
  * All offsets are little-endian ARM (QNX/OMAP4430).
  * "media offset" = offset inside a raw region device (/dev/hd1 or /dev/hd2).
    The IFS image starts at media offset 0x100000 (winchester.py _OFF_IFS).
  * "image offset"  = offset inside the IFS image (= RAM address - LoadStart).
  * success exit status 0 = verified / OK, 1 = failed verification, 2 = usage
    or parse error.
"""

import argparse
import hashlib
import os
import struct
import sys
import zlib

# ---------------------------------------------------------------------------
# constants recovered from hd0head.bin
# ---------------------------------------------------------------------------
CRC_POLY_MSB   = 0x04C11DB7          # literal at 0x80013F2C
CRC_POLY_REFL  = 0xEDB88320          # bit-reversed form (effective)
CRC_INIT       = 0xFFFFFFFF          # 0x8001A7B4: mvn r0,#0
CRC_XOROUT     = 0xFFFFFFFF          # 0x8001A7C8: eor r0,r0,#0xFFFFFFFF
CHUNK          = 10000               # 0x2710, movw r6,#0x2710 @0x800147f6

DESC_MAGIC     = 0xC3D0E193          # cmp against literal 0x800148F4
HDR_COOKIE     = 0xD7A82D1F          # OS image cookie, header+0x20
END_COOKIE     = 0xD7C82D1F          # OS_SIGNATURE_COOKIE, image end - 8
ECC512_COOKIE  = 0xB5A60BFD          # trailer+0xB8 == end-0x178
ECC256_COOKIE  = 0xC6B71C0E          # trailer+0x174
TRAILER_SIZE   = 0x230
SIG_BLOCK_OFF  = 0x28                # ECDSA (r,s) inside the ECC-512 block
DESC_MIN_VERSION = 0x00020000        # 0x80014610: cmp r0,#0x20000 / blo
DESC_FEATURE_VERSION = 0x0003000C    # 0x80013F64: cmp r1,r2 / blo
PAGE_WINDOW_LO = 0x800FF800          # literal 0x800143D8
PAGE_WINDOW_HI = 0x800102B0          # literal 0x800143DC (holds the limit)

OFF_IFS   = 0x100000                 # winchester.py _OFF_IFS
OFF_SIG   = 0xF0000                  # winchester.py _OFF_SIG
OFF_RFS   = 0xC00000                 # winchester.py _OFF_RFS
OFF_END   = 0x20000000               # winchester.py _OFF_END

DEFAULT_DESC_OFF = 0x2C              # MetricsPtr(header+0x28) - LoadStart


# ---------------------------------------------------------------------------
# CRC-32 / ISO-HDLC, modelled on the bootloader code
# ---------------------------------------------------------------------------
def _bitrev(x, n):
    r = 0
    for _ in range(n):
        r = ((r << 1) | (x & 1)) & 0xFFFFFFFF
        x >>= 1
    return r


def bootloader_table():
    """Emulate 0x80013ED4 exactly (bitrev8 -> MSB poly steps -> bitrev32)."""
    tbl = []
    for i in range(256):
        r0 = _bitrev(i, 8)                      # bl 0x80013F0C(r0=i, r1=1)
        r0 = (r0 << 24) & 0xFFFFFFFF            # lsls r0,r0,#0x18
        for _ in range(8):                      # cmp r1,#8 / blo
            if r0 & 0x80000000:
                r0 = ((r0 << 1) & 0xFFFFFFFF) ^ CRC_POLY_MSB
            else:
                r0 = (r0 << 1) & 0xFFFFFFFF
        tbl.append(_bitrev(r0, 32))             # bl 0x80013F0C(r0, r1=4)
    return tbl


TABLE = bootloader_table()


def crc32_update(crc, data):
    """0x80013770: crc = table[(crc ^ b) & 0xff] ^ (crc >> 8)"""
    for b in data:
        crc = TABLE[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return crc


def crc32_iso_hdlc(data, chunk=CHUNK):
    """Full 0x80014788 sequence: init, chunked update, final XOR."""
    crc = CRC_INIT
    mv = memoryview(data)
    for i in range(0, len(mv), chunk):
        crc = crc32_update(crc, mv[i:i + chunk])
    return crc ^ CRC_XOROUT


def crc32_reflected_bits(data):
    """Independent bit-by-bit implementation (no table) for cross-checking."""
    crc = CRC_INIT
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ CRC_POLY_REFL if crc & 1 else crc >> 1
    return crc ^ CRC_XOROUT


# ---------------------------------------------------------------------------
# header / descriptor parsing
# ---------------------------------------------------------------------------
def u8(b, o):
    return b[o]


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def cstr(b, o, n):
    raw = bytes(b[o:o + n])
    return raw.split(b'\0', 1)[0].decode('latin-1', 'replace')


def parse_header(hdr, desc_off=None):
    """Parse the first 0x200 bytes of an IFS image.

    Returns (info dict, warnings list).  Raises ValueError when the header is
    not recognisable.
    """
    if len(hdr) < 0x200:
        raise ValueError("header block must be at least 0x200 bytes "
                         "(got 0x%X)" % len(hdr))
    w = [u32(hdr, i) for i in range(0, 0x40, 4)]
    info = {
        'branch': w[0],
        'load_start': w[1],
        'load_end': w[2],
        'cookie': w[8],
        'load_base': w[9],
        'metrics_ptr': w[10],
    }
    warn = []

    # ---- descriptor (TOSMetrics) location --------------------------------
    cand = []
    if desc_off is not None:
        cand = [(desc_off, 'explicit')]
    else:
        if info['metrics_ptr'] and info['load_start']:
            cand.append((info['metrics_ptr'] - info['load_start'], 'MetricsPtr'))
        cand.append((0x00, 'header-base'))
        cand.append((DEFAULT_DESC_OFF, 'default 0x2C'))
    chosen = None
    for off, why in cand:
        if off < 0 or off + 0x1CC > len(hdr):
            continue
        if u32(hdr, off + 0x9C) == DESC_MAGIC:
            chosen = (off, why)
            break
    if chosen is None:
        raise ValueError(
            "no descriptor found: 0xC3D0E193 is neither at "
            "header+0x%X (MetricsPtr-relative) nor at header+0x9C"
            % cand[0][0] if cand else "no candidates")
    doff, why = chosen
    info['desc_off'] = doff
    info['desc_source'] = why

    d = hdr[doff:]
    info.update({
        'version':       u32(d, 0x00),
        'length':        u32(d, 0x04),
        'load_base_ptr': u32(d, 0x08),
        'load_end_ptr':  u32(d, 0x0C),
        'ram_end':       u32(d, 0x10),
        'build_user':    cstr(d, 0x14, 16),
        'build_date':    cstr(d, 0x24, 12),
        'build_time':    cstr(d, 0x30, 12),
        'device_string': cstr(d, 0x3C, 64),
        'hardware_id':   u32(d, 0x7C),
        'os_version':    u32(d, 0x80),
        'magic':         u32(d, 0x9C),
        'os_type':       u8(d, 0xD4),
        'os_type_media_off': OFF_IFS + doff + 0xD4,
        'tail_ptr':      u32(d, 0x1CC),
    })
    # ---- structural checks, exactly as 0x80014610 -------------------------
    if info['cookie'] != HDR_COOKIE:
        warn.append("header+0x20 cookie = 0x%08X, expected 0x%08X"
                    % (info['cookie'], HDR_COOKIE))
    if info['load_base'] != info['load_start']:
        warn.append("header+0x24 LoadBase (0x%08X) != LoadStart (0x%08X) "
                    "-- 0x80014610 requires *(global+0x24) == global"
                    % (info['load_base'], info['load_start']))
    if info['version'] < DESC_MIN_VERSION:
        warn.append("desc version 0x%08X < 0x%08X (0x80014610 rejects)"
                    % (info['version'], DESC_MIN_VERSION))
    if info['length'] != doff + 0x1CC and info['length'] != 0x1CC:
        warn.append("desc length 0x%X != 0x1CC (metrics block size)" % info['length'])
    info['size'] = (info['load_end'] - info['load_start']) & 0xFFFFFFFF
    if info['size'] == 0 or info['size'] > OFF_RFS - OFF_IFS:
        warn.append("derived image size 0x%X is not in (0, 0xB00000]" % info['size'])
    return info, warn


def dump_info(info):
    o = sys.stdout.write
    o("  branch (hdr+0x00)        : 0x%08X" % info['branch'])
    if (info['branch'] >> 24) == 0xEA:
        imm = info['branch'] & 0x00FFFFFF
        if imm & 0x800000:
            imm -= 0x1000000
        o("  (ARM b, target = image+0x%X)\n" % ((imm * 4 + 8) & 0xFFFFFFFF))
    else:
        o("\n")
    o("  LoadStart (hdr+0x04)     : 0x%08X\n" % info['load_start'])
    o("  LoadEnd   (hdr+0x08)     : 0x%08X\n" % info['load_end'])
    o("  cookie    (hdr+0x20)     : 0x%08X %s\n"
      % (info['cookie'], "OK" if info['cookie'] == HDR_COOKIE else "MISMATCH"))
    o("  LoadBase  (hdr+0x24)     : 0x%08X\n" % info['load_base'])
    o("  MetricsPtr(hdr+0x28)     : 0x%08X\n" % info['metrics_ptr'])
    o("  -> descriptor at hdr+0x%X (from %s)\n"
      % (info['desc_off'], info['desc_source']))
    o("     version      +0x00    : 0x%08X\n" % info['version'])
    o("     length       +0x04    : 0x%X\n" % info['length'])
    o("     load_base_ptr+0x08    : 0x%08X\n" % info['load_base_ptr'])
    o("     load_end_ptr +0x0C    : 0x%08X\n" % info['load_end_ptr'])
    o("     ram_end      +0x10    : 0x%08X\n" % info['ram_end'])
    o("     build_user   +0x14    : %r\n" % info['build_user'])
    o("     build_date   +0x24    : %r\n" % info['build_date'])
    o("     build_time   +0x30    : %r\n" % info['build_time'])
    o("     device_string+0x3C    : %r\n" % info['device_string'])
    o("     hardware_id  +0x7C    : 0x%08X\n" % info['hardware_id'])
    o("     os_version   +0x80    : 0x%08X\n" % info['os_version'])
    o("     magic        +0x9C    : 0x%08X %s\n"
      % (info['magic'], "OK" if info['magic'] == DESC_MAGIC else "MISMATCH"))
    o("     os_type      +0xD4    : %d  (media offset 0x%X) %s\n"
      % (info['os_type'], info['os_type_media_off'],
         "-> CRC path eligible" if info['os_type'] == 6 else "-> CRC path REJECTED"))
    o("     tail_ptr     +0x1CC   : 0x%08X\n" % info['tail_ptr'])
    o("  image size (LoadEnd-LoadStart) : 0x%X (%d bytes)\n"
      % (info['size'], info['size']))
    o("  end-0x230 (trailer)      : image offset 0x%X\n" % (info['size'] - TRAILER_SIZE))
    o("  end-4     (CRC)          : image offset 0x%X\n" % (info['size'] - 4))


# ---------------------------------------------------------------------------
# bootloader-path verification
# ---------------------------------------------------------------------------
def verify_image(img, size=None, off=0, desc_off=None, verbose=True):
    """Model 0x80014610 + 0x80014788 (+ the 0x80018688 trailer shape)."""
    out = []
    ok = True

    def say(s):
        out.append(s)
        if verbose:
            print(s)

    if size is None:
        if len(img) < off + 0x200:
            raise ValueError("file too short")
        info, warn = parse_header(img[off:off + 0x200], desc_off)
        size = info['size']
    else:
        info, warn = parse_header(img[off:off + 0x200], desc_off)
        info['size'] = size

    for w in warn:
        say("  WARN  %s" % w)

    if size > len(img) - off:
        say("  FAIL  declared size 0x%X exceeds available data 0x%X"
            % (size, len(img) - off))
        return False, info, out

    region = bytes(img[off:off + size])

    # 0x80014610 structural checks
    say("  ---- 0x80014610 structural checks ----")
    checks = [
        ("LoadStart == page-window base 0x800FF800",
         info['load_start'] == PAGE_WINDOW_LO, "0x%08X" % info['load_start']),
        ("desc->load_base_ptr -> hdr+0x04",
         info['load_base_ptr'] == (info['load_start'] + 0x04) & 0xFFFFFFFF,
         "0x%08X" % info['load_base_ptr']),
        ("desc->load_end_ptr  -> hdr+0x08",
         info['load_end_ptr'] == (info['load_start'] + 0x08) & 0xFFFFFFFF,
         "0x%08X" % info['load_end_ptr']),
        ("MetricsPtr == LoadStart+desc_off",
         info['metrics_ptr'] == (info['load_start'] + info['desc_off']) & 0xFFFFFFFF,
         "0x%08X" % info['metrics_ptr']),
        ("u32(end-8) == 0xD7C82D1F",
         u32(region, size - 8) == END_COOKIE,
         "0x%08X" % u32(region, size - 8)),
        ("u32(end-4) != 0xFFFFFFFF",
         u32(region, size - 4) != 0xFFFFFFFF,
         "0x%08X" % u32(region, size - 4)),
        ("desc magic 0xC3D0E193",
         info['magic'] == DESC_MAGIC, "0x%08X" % info['magic']),
        ("desc version >= 0x00020000",
         info['version'] >= DESC_MIN_VERSION, "0x%08X" % info['version']),
    ]
    for name, good, extra in checks:
        say("  %s  %-34s %s" % ("PASS " if good else "FAIL ", name, extra))
        ok = ok and good

    # 0x80013FB6 gate
    say("  ---- 0x80013FB6 gate ----")
    gate = (info['os_type'] & 0xFF) == 6
    say("  %s  desc[0xD4] == 6                          %d"
        % ("PASS " if gate else "FAIL ", info['os_type']))
    ok = ok and gate

    # 0x80014788 CRC
    say("  ---- 0x80014788 CRC-32/ISO-HDLC ----")
    stored = u32(region, size - 4)
    if stored == 0xFFFFFFFF:
        say("  FAIL  stored trailer == 0xFFFFFFFF -> status 7 "
            "(0x800147E0); image not CRC-sealed")
        ok = False
    else:
        calc = crc32_iso_hdlc(region[:size - 4])
        good = calc == stored
        say("  %s  crc32[0x0,0x%X) = 0x%08X, stored 0x%08X"
            % ("PASS " if good else "FAIL ", size - 4, calc, stored))
        ok = ok and good

    # 0x80018688 trailer shape (informational: the CRC path never uses it)
    say("  ---- 0x80018688 ECDSA trailer shape (informational) ----")
    t = size - TRAILER_SIZE
    if t < 0:
        say("  FAIL  image smaller than 0x230")
        ok = False
    else:
        c1 = u32(region, size - 0x178) == ECC512_COOKIE
        k = u32(region, size - 0x210)
        c2 = k not in (0, 0xFFFFFFFF)
        c3 = u32(region, size - 8) == END_COOKIE
        say("  %s  u32(end-0x178) == 0xB5A60BFD            0x%08X"
            % ("PASS " if c1 else "INFO ", u32(region, size - 0x178)))
        say("  %s  u32(end-0x210) != {0, -1}              0x%08X"
            % ("PASS " if c2 else "INFO ", k))
        say("  %s  u32(end-8) == 0xD7C82D1F               0x%08X"
            % ("PASS " if c3 else "FAIL ", u32(region, size - 8)))
        say("  INFO sha512[0x0,0x%X) = %s"
            % (t, hashlib.sha512(region[:t]).hexdigest()[:32] + "..."))
        say("  INFO ECDSA (r,s) would be at image offset 0x%X (len 0x88)"
            % (t + SIG_BLOCK_OFF))
        # The CRC path (0x80014610/0x80014788) requires the end-8 cookie but
        # not the ECDSA material; a CRC-only image legitimately fails c1/c2.
        ok = ok and c3

    say("  ---- RESULT: %s ----" % ("VERIFIED (CRC path)" if ok else "FAILED"))
    return ok, info, out


# ---------------------------------------------------------------------------
# reseal
# ---------------------------------------------------------------------------
def reseal(data, desc_off=None, os_type=6, patches=(), length=None,
           verbose=True):
    """Return a new bytearray with the image length, os_type and CRC set.

    `length` (if not None) is the new image size in bytes.  It is written into
    the OS image header's LoadEnd (image+0x08, the field the descriptor's
    load_end_ptr points at), the end-8 cookie is (re)written, and the CRC is
    recomputed over [0, length-4).  Without it the declared size is reused.
    """
    buf = bytearray(data)
    info, _ = parse_header(buf[:0x200], desc_off)
    size = info['size'] if length is None else int(length)
    doff = info['desc_off']
    if size <= 0 or size % 4:
        raise ValueError("image length 0x%X is not a positive multiple of 4"
                         % size)
    if len(buf) < size:
        raise ValueError("input shorter (0x%X) than declared image size (0x%X)"
                         % (len(buf), size))
    # 1. payload / field patches (applied before the CRC)
    for off, val in patches:
        buf[off:off + len(val)] = val
        if verbose:
            print("  patched image offset 0x%X (%d bytes) -> %s"
                  % (off, len(val), val.hex()))
    # 2. the image length (LoadEnd = LoadStart + size), then the OS cookie
    if length is not None:
        old_end = info['load_end']
        new_end = (info['load_start'] + size) & 0xFFFFFFFF
        struct.pack_into('<I', buf, 0x08, new_end)
        if verbose:
            print("  LoadEnd (image+0x08): 0x%08X -> 0x%08X (size 0x%X)"
                  % (old_end, new_end, size))
        struct.pack_into('<I', buf, size - 8, END_COOKIE)
    # 3. the OS-type byte
    old = buf[doff + 0xD4]
    buf[doff + 0xD4] = os_type & 0xFF
    if verbose:
        print("  os_type: image offset 0x%X %d -> %d"
              % (doff + 0xD4, old, os_type & 0xFF))
    # 4. the CRC trailer
    new = crc32_iso_hdlc(bytes(buf[:size - 4]))
    struct.pack_into('<I', buf, size - 4, new)
    if verbose:
        print("  CRC trailer at image offset 0x%X = 0x%08X" % (size - 4, new))
        print("  NOTE: the ECDSA signature in [end-0x230,end) is now invalid; "
              "the CRC path does not use it.")
    return buf


# ---------------------------------------------------------------------------
# commands
# ---------------------------------------------------------------------------
def cmd_selftest(_a):
    print("== CRC algorithm proof ==")
    std = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ CRC_POLY_REFL if c & 1 else c >> 1
        std.append(c)
    print("  0x80013ED4 table == reflected CRC-32 table : %s"
          % (TABLE == std))
    print("  table[0]=%08X table[1]=%08X table[0xFF]=%08X"
          % (TABLE[0], TABLE[1], TABLE[0xFF]))
    vec = b"123456789"
    got = crc32_iso_hdlc(vec)
    print("  check vector '123456789' : 0x%08X (expect 0xCBF43926) %s"
          % (got, "OK" if got == 0xCBF43926 else "FAIL"))
    print("  empty string             : 0x%08X (expect 0x00000000)" % crc32_iso_hdlc(b""))
    blob = bytes(range(256)) * 200
    print("  table==bitwise           : %s"
          % (crc32_iso_hdlc(blob) == crc32_reflected_bits(blob)))
    print("  table==zlib.crc32        : %s" % (crc32_iso_hdlc(blob) == zlib.crc32(blob)))
    # chunk-boundary behaviour of the 0x2710 loop
    big = bytes((i * 37) & 0xFF for i in range(0x2710 * 3 + 1234))
    print("  chunked(0x2710)==zlib    : %s" % (crc32_iso_hdlc(big) == zlib.crc32(big)))

    print("== synthetic image round trip ==")
    size = 0x40 + 0x2000 + TRAILER_SIZE
    img = bytearray(os.urandom(size))
    # header
    struct.pack_into('<I', img, 0x00, 0xEA0001FE)
    struct.pack_into('<I', img, 0x04, 0x800FF800)
    struct.pack_into('<I', img, 0x08, 0x800FF800 + size)
    struct.pack_into('<I', img, 0x20, HDR_COOKIE)
    struct.pack_into('<I', img, 0x24, 0x800FF800)
    struct.pack_into('<I', img, 0x28, 0x800FF800 + 0x2C)
    d = 0x2C
    struct.pack_into('<I', img, d + 0x00, 0x00030012)
    struct.pack_into('<I', img, d + 0x04, 0x1CC)
    struct.pack_into('<I', img, d + 0x08, 0x800FF804)
    struct.pack_into('<I', img, d + 0x0C, 0x800FF808)
    struct.pack_into('<I', img, d + 0x7C, 0x06001A06)
    struct.pack_into('<I', img, d + 0x80, 0x02004869)
    struct.pack_into('<I', img, d + 0x9C, DESC_MAGIC)
    img[d + 0xD4] = 0
    struct.pack_into('<I', img, d + 0x1CC, 0)
    struct.pack_into('<I', img, size - 8, END_COOKIE)
    struct.pack_into('<I', img, size - 4, 0xFFFFFFFF)
    struct.pack_into('<I', img, size - 0x178, ECC512_COOKIE)
    struct.pack_into('<I', img, size - 0x210, 0x00000088)
    ok0, _, _ = verify_image(bytes(img), verbose=False)
    print("  unsealed image accepted   : %s (expect False)" % ok0)
    sealed = reseal(bytes(img), verbose=False)
    ok1, _, _ = verify_image(bytes(sealed), verbose=False)
    print("  resealed image accepted   : %s (expect True)" % ok1)
    tampered = bytearray(sealed)
    tampered[0x400] ^= 0xFF
    ok2, _, _ = verify_image(bytes(tampered), verbose=False)
    print("  1-bit corruption rejected : %s (expect False)" % (not ok2))
    return 0 if (TABLE == std and got == 0xCBF43926 and not ok0
                 and ok1 and ok2 is False) else 1


def cmd_table(_a):
    for i in range(0, 256, 4):
        print("  %02X: %s" % (i, " ".join("%08X" % TABLE[j]
                                          for j in range(i, i + 4))))
    return 0


def cmd_crc(a):
    data = open(a.file, 'rb').read()
    off = int(a.offset, 0)
    ln = int(a.length, 0) if a.length else len(data) - off
    if off + ln > len(data):
        print("range exceeds file size 0x%X" % len(data))
        return 2
    slice_ = data[off:off + ln]
    c = crc32_iso_hdlc(slice_)
    print("crc32/ISO-HDLC  file=%s offset=0x%X length=0x%X  = 0x%08X"
          % (a.file, off, ln, c))
    print("  bitwise cross-check = 0x%08X   zlib = 0x%08X"
          % (crc32_reflected_bits(slice_), zlib.crc32(slice_)))
    return 0


def pick_image_off(path, explicit):
    """A raw region dump has the IFS at 0x100000; a bare image starts at 0."""
    if explicit:
        return int(explicit, 0)
    n = os.path.getsize(path)
    return OFF_IFS if n >= OFF_IFS + 0x200 else 0


def cmd_probe(a):
    off = pick_image_off(a.file, a.image_off)
    fh = open(a.file, 'rb')
    fh.seek(off)
    hdr = fh.read(0x200)
    fh.close()
    try:
        info, warn = parse_header(hdr, int(a.desc_off, 0) if a.desc_off else None)
    except ValueError as e:
        print("PARSE ERROR: %s" % e)
        ls = u32(hdr, 0x04)
        le = u32(hdr, 0x08)
        print("  hdr+0x04=0x%08X hdr+0x08=0x%08X size=0x%X" % (ls, le, le - ls))
        print("  hdr+0x20=0x%08X hdr+0x24=0x%08X hdr+0x28=0x%08X"
              % (u32(hdr, 0x20), u32(hdr, 0x24), u32(hdr, 0x28)))
        for off in (0x9C, 0xC8):
            print("  hdr+0x%X = 0x%08X" % (off, u32(hdr, off)))
        return 2
    print("== %s: IFS header block ==" % a.file)
    dump_info(info)
    for w in warn:
        print("  WARN  %s" % w)
    # extra reconnaissance bytes for the report
    print("  ---- raw reconnaissance ----")
    for off in (0x00, 0x04, 0x08, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38,
                0x9C, 0xC8, 0xD4, 0x100, 0x1F8, 0x1FC):
        print("  hdr+0x%03X : 0x%08X" % (off, u32(hdr, off)))
    return 0


def cmd_verify(a):
    data = open(a.file, 'rb').read()
    off = pick_image_off(a.file, a.image_off)
    size = int(a.size, 0) if a.size else None
    try:
        ok, info, _ = verify_image(data, size, off,
                                   int(a.desc_off, 0) if a.desc_off else None)
    except ValueError as e:
        print("PARSE ERROR: %s" % e)
        return 2
    return 0 if ok else 1


def cmd_reseal(a):
    data = open(a.file, 'rb').read()
    off = pick_image_off(a.file, a.image_off)
    size = int(a.size, 0) if a.size else None
    desc = int(a.desc_off, 0) if a.desc_off else None
    if size is not None and len(data) > off + size:
        data = data[off:off + size]
        off = 0
    else:
        data = data[off:]
        off = 0
    if a.append_os_trailer:
        # The imagefs emitter ends at the imagefs trailer; the CRC path's
        # OS trailer (cookie + CRC) lives after it.
        data = data + b'\0' * 8
    if a.length == 'auto':
        length = len(data)
    elif a.length:
        length = int(a.length, 0)
    else:
        length = None
    patches = []
    for p in a.patch or []:
        o, v = p.split(':', 1)
        patches.append((int(o, 0), bytes.fromhex(v)))
    print("== reseal %s -> %s ==" % (a.file, a.out))
    buf = reseal(data, desc, int(a.type, 0), patches, length)
    with open(a.out, 'wb') as f:
        f.write(buf)
    print("  wrote 0x%X bytes" % len(buf))
    ok, _, _ = verify_image(bytes(buf))
    return 0 if ok else 1


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd')

    s = sub.add_parser('selftest'); s.set_defaults(func=cmd_selftest)
    s = sub.add_parser('table'); s.set_defaults(func=cmd_table)

    s = sub.add_parser('crc'); s.add_argument('file')
    s.add_argument('--offset', default='0'); s.add_argument('--length')
    s.set_defaults(func=cmd_crc)

    s = sub.add_parser('probe'); s.add_argument('file')
    s.add_argument('--image-off', default=None,
                   help='default: 0x100000 for a raw region dump, else 0')
    s.add_argument('--desc-off')
    s.set_defaults(func=cmd_probe)

    s = sub.add_parser('verify'); s.add_argument('file')
    s.add_argument('--image-off', default=None,
                   help='default: 0x100000 for a raw region dump, else 0')
    s.add_argument('--size'); s.add_argument('--desc-off')
    s.set_defaults(func=cmd_verify)

    s = sub.add_parser('reseal'); s.add_argument('file'); s.add_argument('out')
    s.add_argument('--image-off', default=None,
                   help='default: 0x100000 for a raw region dump, else 0')
    s.add_argument('--size'); s.add_argument('--desc-off')
    s.add_argument('--type', default='6')
    s.add_argument('--patch', action='append', metavar='OFFSET:HEX')
    s.add_argument('--length', metavar='N|auto',
                   help='new image size: patch LoadEnd (image+0x08), write the '
                        'end-8 cookie and CRC over [0,length-4)')
    s.add_argument('--append-os-trailer', action='store_true',
                   help='append the 8-byte OS trailer after the input')
    s.set_defaults(func=cmd_reseal)

    a = ap.parse_args(argv)
    if not getattr(a, 'func', None):
        ap.print_help()
        return 2
    return a.func(a)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
