#!/usr/bin/env python3
"""sgxusecodebase.py - probe the SGX540 USE_CODE_BASE registers.

        ***  DO NOT RUN THIS ON THE UNIT.  IT WEDGES IT.  ***

Session 73 ran this read-only probe against the booted closed DDK and the whole
device hung: the read of 0x0a0c + 4n timed out, the RNDIS link went "no route to
host", the ACM console stopped answering and the UI froze.  A manual reboot was
needed (trap 376).

The mistake it encodes: USE_CODE_BASE is *named* in the GPL sgx540defs.h, but it
is NOT on the upstream guide's safe-to-read list for a core in any state
(CLKGATECTL, CLKGATESTATUS, SOFT_RESET, EVENT_STATUS, EVENT_HOST_ENABLE,
BIF_CTRL, CORE_ID, CORE_REVISION).  "Named in a header" is not the same test as
"safe to touch", and reading past that list can hang the bus until the reset
sequence has run.

Kept as evidence of exactly what was read, and as the reference for the one place
this question *can* be settled: inside the open driver's own reset/init path,
writing a known value, reading it back and restoring, in a boot from RAM.  That
belongs in drm/powervr_sgx/, not in a userspace script.

Fact-sheet Gap 7 (is USE_CODE_BASE present on rev 120, and what granule?) is
therefore still **open**.  See docs/project/KNOWN-ISSUES.md trap 376.
"""

import mmap
import os
import struct
import sys

GPU_BASE = 0x56000000
USE_CODE_BASE0 = 0x0A0C
NUM_ENTRIES = 16
PDS_EXEC_BASE = 0x0AB8

# The 28-bit heap bases from the DDK's sgxconfig.h (see sgx540_heaps).
HEAP_BASES = {
    0x00001000: "general",
    0x08800000: "shared 3D params",
    0x0C800000: "TA data",
    0x0D800000: "sync info",
    0x0DC00000: "PDS pixel  == PDS_EXEC_BASE",
    0x0E400000: "kernel code",
    0x0E800000: "PDS vertex",
    0x0F000000: "kernel data",
    0x0F400000: "USSE pixel",
    0x0FC00000: "USSE vertex",
}


def rd32(mm, off, bias=0):
    return struct.unpack("<I", mm[bias + off:bias + off + 4])[0]


def main():
    if os.geteuid() != 0:
        print("must run as root (reads /dev/mem)", file=sys.stderr)
        return 1

    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    try:
        mm = mmap.mmap(fd, 0x10000, mmap.MAP_SHARED, mmap.PROT_READ,
                       offset=GPU_BASE)
    finally:
        os.close(fd)

    pds = rd32(mm, PDS_EXEC_BASE)
    print("PDS_EXEC_BASE (0x%04x) = 0x%08x" % (PDS_EXEC_BASE, pds))

    vals = []
    print("\nUSE_CODE_BASE[0..15] (0x%04x + 4n), read-only:" % USE_CODE_BASE0)
    for n in range(NUM_ENTRIES):
        v = rd32(mm, USE_CODE_BASE0 + 4 * n)
        vals.append(v)
        print("  [%2d] 0x%04x = 0x%08x" % (n, USE_CODE_BASE0 + 4 * n, v))

    nonzero = [v for v in vals if v]
    print("\n%d of %d registers read non-zero." % (len(nonzero), NUM_ENTRIES))

    if not nonzero:
        print("""
All zero: either the registers are absent on this revision, or nothing has
programmed them (the DDK's kernel does not; the microkernel programs its own).
This is consistent with the upstream guide's "no USE_CODE_BASE" and does not
prove the registers exist.  A write/read-back is the only way to settle it.""")
        return 0

    # Decode: try each plausible granule and see which lands on a known base.
    print("\nDecoding the address field (low 24 bits) under each granule:")
    print("  %-4s %-10s %-12s %-12s" % ("idx", "raw", "addr if 256B", "addr if 1B"))
    for n, v in enumerate(vals):
        if not v:
            continue
        low = v & 0x00FFFFFF
        dm = (v >> 24) & 0x3
        a256, a1 = low << 8, low
        m256 = HEAP_BASES.get(a256, "")
        m1 = HEAP_BASES.get(a1, "")
        print("  [%2d] 0x%08x dm=%d  0x%08x%-14s 0x%08x%s"
              % (n, v, dm, a256, (" " + m256) if m256 else "", a1,
                 (" " + m1) if m1 else ""))

    hits256 = sum(1 for v in nonzero
                  if ((v & 0x00FFFFFF) << 8) in HEAP_BASES)
    hits1 = sum(1 for v in nonzero if (v & 0x00FFFFFF) in HEAP_BASES)
    print("\nKnown heap bases matched: 256-byte units %d, 1-byte units %d"
          % (hits256, hits1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
