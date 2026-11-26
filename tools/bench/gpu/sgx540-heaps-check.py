#!/usr/bin/env python3
"""Check the SGX540 entry's heap layout against the driver's own rules.

Mirrors pvrsgx_core.c's heaps_fit()/pds_reach() and pvrsgx_core_family_usable()
so a heap mistake is caught on the host instead of on the unit.
"""
import sys

VA_BITS = 28
SZ_64M = 64 * 1024 * 1024

# From sgx540_heaps, copied verbatim.
heaps = {
    "general":     (0x00001000, 0x04400000),
    "texture":     (0x04400000, 0x08800000),
    "params_3d":   (0x08800000, 0x0C800000),
    "ta_data":     (0x0C800000, 0x0D800000),
    "sync":        (0x0D800000, 0x0DC00000),
    "pds_pixel":   (0x0DC00000, 0x0E400000),
    "kernel_code": (0x0E400000, 0x0E800000),
    "pds_vertex":  (0x0E800000, 0x0F000000),
    "kernel_data": (0x0F000000, 0x0F400000),
    "usse_pixel":  (0x0F400000, 0x0F900000),
    "usse_vertex": (0x0FC00000, 0x0FE00000),
}

fail = 0

limit = 1 << VA_BITS
for name, (base, end) in heaps.items():
    if base >= end:
        print("FAIL %-12s base 0x%08x >= end 0x%08x" % (name, base, end))
        fail += 1
    if end > limit:
        print("FAIL %-12s end 0x%08x exceeds va_bits (%d = 0x%08x)"
              % (name, end, VA_BITS, limit))
        fail += 1

# The client heaps must not overlap: two heaps covering one address would let
# two allocations collide. A core's layout gives each a distinct range.
names = list(heaps)
for i, a in enumerate(names):
    for b in names[i + 1:]:
        (a0, a1), (b0, b1) = heaps[a], heaps[b]
        if a0 < b1 and b0 < a1:
            print("FAIL overlap: %-12s [0x%08x,0x%08x) and %-12s [0x%08x,0x%08x)"
                  % (a, a0, a1, b, b0, b1))
            fail += 1

# pds_reach(): without EDM_VERTEX_PDSADDR_FULL_RANGE, the heaps the EDM runs
# PDS code from lie within 64 MiB above PDS_EXEC_BASE (pds_pixel.base).
exec_base = heaps["pds_pixel"][0]
for name in ("kernel_code", "pds_vertex"):
    base, end = heaps[name]
    if not (base >= exec_base and end - exec_base <= SZ_64M):
        print("FAIL pds_reach(%s): base 0x%08x, end-exec 0x%x > 0x%x"
              % (name, base, end - exec_base, SZ_64M))
        fail += 1
    else:
        print("ok   pds_reach(%-12s): end-exec = 0x%08x (<= 0x%08x)"
              % (name, end - exec_base, SZ_64M))

# Overlap is not itself a fault in the driver (heaps run to the next base), but
# the *named* heaps that the firmware layout treats as distinct should not.
# general (0x1000..0x08800000) already covers the space texture starts at, so
# texture is checked to be the top of general, as sgx530_heaps does.
g = heaps["general"]
t = heaps["texture"]
if t[0] != g[1]:
    print("FAIL texture.base 0x%08x != general.end 0x%08x" % (t[0], g[1]))
    fail += 1

# usize windows must sit inside their declared sizes.
for name, size in (("usse_pixel", 0x500000), ("usse_vertex", 0x200000)):
    b, e = heaps[name]
    if e - b != size:
        print("FAIL %s size 0x%x != 0x%x" % (name, e - b, size))
        fail += 1

print()
print("SGX540 heap layout: %s" % ("FAIL (%d)" % fail if fail else "all checks pass"))
sys.exit(1 if fail else 0)
