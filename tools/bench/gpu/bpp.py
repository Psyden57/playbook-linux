#!/usr/bin/env python3
"""bpp.py - what does the blob's PVRDRIPixFmtGetBPP() return for ARGB8888?
If it returns 32 (bits) and the blob multiplies width*height*bpp for the
allocation, that is exactly the 8x we measured (32 bits vs 4 bytes)."""
import ctypes
lib = ctypes.CDLL("libpvr_dri_support.so.1")
lib.PVRDRIPixFmtGetBPP.argtypes = [ctypes.c_uint]
lib.PVRDRIPixFmtGetBPP.restype = ctypes.c_uint
# IMG_PIXFMT enum values: probe a range and report the nonzero answers
print(f"{'fmt':>4} {'BPP':>5}")
for f in range(1, 96):
    v = lib.PVRDRIPixFmtGetBPP(f)
    if v:
        print(f"{f:>4} {v:>5}")
