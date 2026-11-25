#!/usr/bin/env python3
"""bochk4.py - is the 8 a fixed multiplier, or ceil(height/64)*64-style tiling?
Allocates a wide range of heights/widths and reports kernel obj->size / visible."""
import ctypes, os, re
FMT=0x34325241
lib=ctypes.CDLL("libgbm.so.1")
lib.gbm_create_device.argtypes=[ctypes.c_int]; lib.gbm_create_device.restype=ctypes.c_void_p
lib.gbm_bo_create.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_uint,ctypes.c_uint,ctypes.c_uint]
lib.gbm_bo_create.restype=ctypes.c_void_p
lib.gbm_bo_destroy.argtypes=[ctypes.c_void_p]
lib.gbm_bo_get_stride.argtypes=[ctypes.c_void_p]; lib.gbm_bo_get_stride.restype=ctypes.c_uint
GEM="/sys/kernel/debug/dri/omapdrm.0/gem"
def sizes():
    r=[]
    for l in open(GEM):
        m=re.match(r"\s*([0-9a-f]{8}):.*\s(\d+)\s*$",l)
        if m: r.append(int(m.group(2)))
    return sorted(r)
fd=os.open("/dev/dri/renderD128", os.O_RDWR); d=lib.gbm_create_device(fd)
print(f"{'request':>12} {'stride':>7} {'kernel obj':>11} {'visible':>10} {'ratio':>7}")
for (w,h) in ((1024,600),(1024,4800),(1024,300),(1024,75),(1024,76),(512,600),(2048,600),(1024,1200)):
    b=sizes(); bo=lib.gbm_bo_create(d,w,h,FMT,1|4)
    if not bo: print(f"{w}x{h}: FAILED"); continue
    a=sizes(); new=list(a)
    for s in b:
        if s in new: new.remove(s)
    st=lib.gbm_bo_get_stride(bo); vis=w*h*4
    obj=new[0] if new else -1
    print(f"{w:>5}x{h:<6} {st:>7} {obj:>11} {vis:>10} {obj/vis:>7.2f}")
    lib.gbm_bo_destroy(bo)
