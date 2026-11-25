#!/usr/bin/env python3
"""stride.py - ask the blob's stride/size helper what it reports vs what it allocates.
If dbm_get_buffer_stride_and_size says 2.4MB but the GEM object is 19.6MB, the
mismatch is inside the blob's own alloc, not the size it believes in."""
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
for (w,h,bpp) in ((1024,600,32),(1024,600,16),(1024,600,8)):
    b=sizes(); st=ctypes.c_uint(0)
    buf=SUP.PVRDRIBufferCreate(impl,w,h,bpp,1,ctypes.byref(st))
    a=sizes(); new=list(a)
    for s in b:
        if s in new: new.remove(s)
    obj=new[0] if new else -1
    print(f"{w}x{h} bpp={bpp:<3} stride={st.value:<6} obj={obj:<10} "
          f"stride*h={st.value*h:<10} obj/(stride*h)={obj/(st.value*h) if st.value else 0:.2f}")
