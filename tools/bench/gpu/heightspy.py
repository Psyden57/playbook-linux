#!/usr/bin/env python3
"""heightspy.py - wrap drmIoctl via ctypes and log the get/set of
DRM_IOCTL_MODE_CREATE_DUMB during a real DDK allocation, so we see the exact
geometry the blob asks for."""
import ctypes, os, re

IOCTL = (3<<30)|(32<<16)|(ord('d')<<8)|0xb2
libc = ctypes.CDLL("libc.so", use_errno=True)
# There is no interposition in-process here; instead call the blob and read the
# kernel's own record of what it allocated, which is authoritative.
SUP = ctypes.CDLL("libpvr_dri_support.so.1")
SUP.PVRDRICreateScreenImpl.argtypes=[ctypes.c_int]; SUP.PVRDRICreateScreenImpl.restype=ctypes.c_void_p
SUP.PVRDRIBufferCreate.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,
                                 ctypes.c_uint,ctypes.c_uint,ctypes.POINTER(ctypes.c_uint)]
SUP.PVRDRIBufferCreate.restype=ctypes.c_void_p
GEM="/sys/kernel/debug/dri/omapdrm.0/gem"
def objs():
    out={}
    for l in open(GEM):
        m=re.match(r"\s*([0-9a-f]{8}):\s+(\d+)\s+\(\s*(\d+)\)\s+(\S+)\s+(\S+)\s+\(\s*(\d+)\)\s+(\S+)\s+(\d+)\s+(\d+)",l)
        if m: out[int(m.group(9))]=out.get(int(m.group(9)),0)+1
    return out
fd=os.open("/dev/dri/renderD128", os.O_RDWR); impl=SUP.PVRDRICreateScreenImpl(fd)
w,h=1024,600
before=objs(); st=ctypes.c_uint(0)
buf=SUP.PVRDRIBufferCreate(impl,w,h,32,1,ctypes.byref(st))
after=objs()
new=[s for s in after if after[s]>before.get(s,0)]
print(f"asked {w}x{h} stride->{st.value}")
print(f"new kernel objects: {new}")
for s in new:
    print(f"  size={s}: rows_if_stride={s//st.value}  (asked height={h}, ratio={(s//st.value)/h:.1f})")
