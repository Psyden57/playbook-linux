#!/usr/bin/env python3
"""sgxcoreid.py - read-only SGX540 identity registers through /dev/mem.

The zero-risk first step of the powervr_sgx SGX540 port (report 276, fact sheet
Gaps 1 and 2): read CORE_ID/CORE_REVISION/DESIGNER_REV_FIELD1/2 at 0x0020/0x0024/
0x0028/0x002c, plus the ti-sysc module id registers at 0x5600fe00/0x5600fe10.

Offsets are the SGX540 ones from the vendored GPL DDK (sgx540defs.h).  Also reads
the SGX545 offsets (0x001c/0x0020) so a wrong table is obvious.  Nothing is
written; every read is a 4-byte load from a mapped register page.

Run on the unit as root while pvrsrvkm clocks the GPU:
    python3 sgxcoreid.py
"""

import mmap
import os
import struct
import sys

GPU_BASE = 0x56000000
MOD_BASE = 0x5600FE00

REGS = [
    ("SGX540 CORE_ID          [0x0020]", 0x0020),
    ("SGX540 CORE_REVISION    [0x0024]", 0x0024),
    ("SGX540 DESIGNER_REV_F1  [0x0028]", 0x0028),
    ("SGX540 DESIGNER_REV_F2  [0x002c]", 0x002C),
    ("SGX545 CORE_ID?         [0x001c]", 0x001C),
    ("SGX545 CORE_REVISION?   [0x0020]", 0x0020),
    # The registers the upstream kernel guide names as safe to read before the
    # reset sequence has run (docs/porting/kernel.md 2): CLKGATECTL,
    # CLKGATESTATUS, SOFT_RESET, EVENT_STATUS, EVENT_HOST_ENABLE, BIF_CTRL,
    # CORE_ID and CORE_REVISION.  Nothing else is read: registers past these are
    # only touched once a microkernel run names them (fact sheet Gap 12), and
    # the write-only *_CLEAR registers are not read at all.
    ("CLKGATECTL              [0x0000]", 0x0000),
    ("CLKGATECTL2             [0x0004]", 0x0004),
    ("CLKGATESTATUS           [0x0008]", 0x0008),
    ("CLKGATECTLOVR           [0x000c]", 0x000C),
    ("SOFT_RESET              [0x0080]", 0x0080),
    ("EVENT_STATUS2           [0x0118]", 0x0118),
    ("EVENT_STATUS            [0x012c]", 0x012C),
    ("EVENT_HOST_ENABLE       [0x0130]", 0x0130),
    ("BIF_CTRL                [0x0c00]", 0x0C00),
    ("BIF_INT_STAT            [0x0c04]", 0x0C04),
]
MODS = [
    ("ti-sysc rev             [+0x00]", 0x00),
    ("ti-sysc sysc            [+0x10]", 0x10),
]

AREA = 0x10000  # one 64 KiB page is enough


def rd32(mm, off):
    return struct.unpack("<I", mm[off:off + 4])[0]


def decode_rev(v):
    maint, minor, major, designer = v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF
    return "maintenance=%d minor=%d major=%d designer=0x%02x" % (maint, minor, major, designer)


def decode_id(v):
    cfg, ident = v & 0xFFFF, (v >> 16) & 0xFFFF
    return "config=0x%04x id=0x%04x" % (cfg, ident)


def region(base, page_off, size=AREA):
    """mmap a /dev/mem window; the offset must be page aligned, so round down
    and return (mapping, bias) where bias is the in-page byte offset."""
    rel = base + page_off
    aligned = rel & ~0xFFF
    bias = rel - aligned
    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    try:
        mm = mmap.mmap(fd, size + bias, mmap.MAP_SHARED, mmap.PROT_READ,
                       offset=aligned)
    finally:
        os.close(fd)
    return mm, bias


def main():
    if os.geteuid() != 0:
        print("must run as root (reads /dev/mem)", file=sys.stderr)
        return 1

    print("GPU register window 0x%08x (read-only, /dev/mem)" % GPU_BASE)
    mm, bias = region(GPU_BASE, 0)
    for name, off in REGS:
        v = rd32(mm, bias + off)
        extra = ""
        if "CORE_REVISION" in name:
            extra = "  -> " + decode_rev(v)
        elif "CORE_ID" in name:
            extra = "  -> " + decode_id(v)
        print("  0x%08x  %-30s = 0x%08x%s" % (GPU_BASE + off, name, v, extra))

    cid = rd32(mm, bias + 0x0020)
    crev = rd32(mm, bias + 0x0024)
    print()
    print("EXPECTED CORE_REVISION 0x00010200 (1.2.0, 'Rev 120'): %s" %
          ("MATCH" if crev == 0x00010200 else "DIFFERS (got 0x%08x)" % crev))
    print("CORE_ID id field (SGX545 is 0x0115): 0x%04x" % ((cid >> 16) & 0xFFFF))

    print()
    print("ti-sysc module id @ 0x%08x" % MOD_BASE)
    mm2, bias2 = region(MOD_BASE, 0, 0x1000)
    for name, off in MODS:
        print("  0x%08x  %-30s = 0x%08x" % (MOD_BASE + off, name, rd32(mm2, bias2 + off)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
