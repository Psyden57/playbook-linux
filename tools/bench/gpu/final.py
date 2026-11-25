#!/usr/bin/env python3
"""final.py - is the 8x a fixed design constant? Sweep bpp x size and confirm
obj == 8 * stride * height in every case."""
import ctypes, os, re
SUP=ctypes.CDLL("libpvr_dri_support.so.1")
SUP.PVRDRICreateScreenImpl.argtypes=[ctypes.c_int]; SUP.PVRDRICreateScreenImpl.restype=ctypes.c_void_p
SUP.PVRDRIBufferCreate.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,
                                 ctypes.c_uint,ctypes.c_uint,ctypes.POINTER(ctypes.c_uint)]
SUP.PVRDRIBufferCreate.restype=ctypes.c_void_p
GEM="/sys/kernel/debug/dri/omapdrm.0/gem"
def sizes():
    r=[]
    for l in open(GEM):
        m=re.match(r"\s*([0-9a-f]{8}):.*\s(\d+)\s*$",l)
        if m: r.append(int(m.group(2)))
    return sorted(r)
fd=os.open("/dev/dri/renderD128", os.O_RDWR); impl=SUP.PVRDRICreateScreenImpl(fd)
ratios=[]
for (w,h) in ((1024,600),(800,480),(640,480),(320,240),(1024,64)):
    for bpp in (32,16,8):
        b=sizes(); st=ctypes.c_uint(0)
        buf=SUP.PVRDRIBufferCreate(impl,w,h,bpp,1,ctypes.byref(st))
        a=sizes(); new=list(a)
        for s in b:
            if s in new: new.remove(s)
        if not new: print(f"{w}x{h}@{bpp}: no object"); continue
        obj=new[0]; r=obj/(st.value*h)
        ratios.append(r)
        print(f"{w:>5}x{h:<4} bpp={bpp:<3} stride={st.value:<6} obj={obj:<10} obj/(stride*h)={r:.2f}")
print(f"\nALL ratios == 8.0: {all(abs(r-8.0)<1e-9 for r in ratios)}  (n={len(ratios)})")
