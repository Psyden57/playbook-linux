#!/usr/bin/env python3
"""sizein.py - does the kernel honour the size field, or recompute from pitch*h?
Send a deliberately wrong size with correct w/h and see what comes back."""
import ctypes, fcntl, os, struct
IOCTL=(3<<30)|(32<<16)|(ord('d')<<8)|0xb2
def dumb(fd,w,h,bpp=32,claim_size=None):
    buf=bytearray(32)
    struct.pack_into("<IIIIII",buf,0,h,w,bpp,0,0,0)
    if claim_size is not None:
        struct.pack_into("<Q",buf,24,claim_size)
    fcntl.ioctl(fd,IOCTL,buf,True)
    _,_,_,_,handle,pitch=struct.unpack_from("<IIIIII",buf,0)
    size=struct.unpack_from("<Q",buf,24)[0]
    return pitch,size
fd=os.open("/dev/dri/card0",os.O_RDWR)
for claim,label in ((None,"normal"),(999999999,"claimed huge"),(1,"claimed 1 byte")):
    p,s=dumb(fd,1024,600,claim_size=claim)
    print(f"1024x600 {label:>16}: pitch={p} returned size={s}  (pitch*h={p*600})")
