#!/usr/bin/env python3
"""dumbchk.py - call DRM_IOCTL_MODE_CREATE_DUMB directly on the omapdrm node and
print what the KERNEL returns for pitch and size.  Bypasses Mesa/gbm entirely,
so it separates "the kernel rounds the allocation" from "Mesa asks for 8x".

struct drm_mode_create_dumb { u32 height, width, bpp, flags, handle, pitch; u64 size; }
"""
import ctypes, fcntl, os, struct, sys

DRM_IOCTL_MODE_CREATE_DUMB = (3 << 30) | (32 << 16) | (ord('d') << 8) | 0xb2
FORMAT_ARGB8888 = 0x34325241

def create_dumb(fd, w, h, bpp=32):
    buf = bytearray(32)
    struct.pack_into("<IIIIII", buf, 0, h, w, bpp, 0, 0, 0)
    fcntl.ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, buf, True)
    hh, ww, bb, fl, handle, pitch = struct.unpack_from("<IIIIII", buf, 0)
    size = struct.unpack_from("<Q", buf, 24)[0]
    return handle, pitch, size

dev = sys.argv[1] if len(sys.argv) > 1 else "/dev/dri/card0"
fd = os.open(dev, os.O_RDWR)
print(f"node {dev}")
for (w, h) in ((1024, 600), (1024, 300), (256, 256), (1024, 20)):
    try:
        handle, pitch, size = create_dumb(fd, w, h)
        vis = w * h * 4
        print(f"  {w:>4}x{h:<4} pitch={pitch:<6} size={size:<10} "
              f"visible={vis:<9} ratio={size/vis:5.2f}  pitch*h={pitch*h}")
    except OSError as e:
        print(f"  {w}x{h}: ioctl failed: {e}")
