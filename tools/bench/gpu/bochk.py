#!/usr/bin/env python3
"""bochk.py - ask Mesa's libgbm for buffers on the omapdrm node and report the
sizes.  Uses ctypes so it needs no compiler (the unit has no binutils/gcc, and
the host's musl cross toolchain used for the session-69 probes is not present).

Reads /sys/kernel/debug/dri/omapdrm.0/gem before and after each allocation, so
the sizes printed are the KERNEL's view (omapdrm GEM obj->size), not gbm's.
"""
import ctypes, os, re, sys

GBM_FORMAT_ARGB8888 = 0x34325241
GBM_BO_USE_SCANOUT = 1 << 0
GBM_BO_USE_RENDERING = 1 << 2

lib = ctypes.CDLL("libgbm.so.1")
lib.gbm_create_device.argtypes = [ctypes.c_int]
lib.gbm_create_device.restype = ctypes.c_void_p
lib.gbm_device_get_backend_name.argtypes = [ctypes.c_void_p]
lib.gbm_device_get_backend_name.restype = ctypes.c_char_p
lib.gbm_bo_create.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint,
                              ctypes.c_uint, ctypes.c_uint]
lib.gbm_bo_create.restype = ctypes.c_void_p
for fn in ("gbm_bo_get_width", "gbm_bo_get_height", "gbm_bo_get_stride",
           "gbm_bo_get_format"):
    getattr(lib, fn).argtypes = [ctypes.c_void_p]
    getattr(lib, fn).restype = ctypes.c_uint
lib.gbm_bo_get_fd.argtypes = [ctypes.c_void_p]
lib.gbm_bo_get_fd.restype = ctypes.c_int
lib.gbm_bo_destroy.argtypes = [ctypes.c_void_p]

GEM = "/sys/kernel/debug/dri/omapdrm.0/gem"

def gem_sizes():
    """Return the multiset of obj->size values omapdrm currently accounts."""
    out = []
    try:
        for line in open(GEM):
            m = re.match(r"\s*([0-9a-f]{8}):.*\s(\d+)\s*$", line)
            if m:
                out.append(int(m.group(2)))
    except OSError:
        pass
    return sorted(out)

def probe(dev, w, h, flags, label):
    before = gem_sizes()
    fd = os.open(dev, os.O_RDWR)
    d = lib.gbm_create_device(fd)
    if not d:
        print(f"{label}: gbm_create_device FAILED on {dev}")
        return
    backend = lib.gbm_device_get_backend_name(d).decode()
    bo = lib.gbm_bo_create(d, w, h, GBM_FORMAT_ARGB8888, flags)
    if not bo:
        print(f"{label}: {dev} backend={backend} {w}x{h} -> gbm_bo_create FAILED")
        return
    bw, bh = lib.gbm_bo_get_width(bo), lib.gbm_bo_get_height(bo)
    stride = lib.gbm_bo_get_stride(bo)
    visible = bw * bh * 4
    after = gem_sizes()
    new = list(after)
    for s in before:
        if s in new:
            new.remove(s)
    print(f"{label}: backend={backend}")
    print(f"   gbm reports {bw}x{bh} stride={stride} -> stride*h={stride*bh} B")
    print(f"   visible pixels {visible} B ({visible/1048576:.2f} MiB)")
    print(f"   NEW omapdrm GEM objects: {new}  ({[round(x/visible,2) for x in new]} x visible)")
    lib.gbm_bo_destroy(bo)

if __name__ == "__main__":
    dev = sys.argv[1] if len(sys.argv) > 1 else "/dev/dri/renderD128"
    print("baseline gem:", gem_sizes())
    for (w, h) in ((1024, 600), (1024, 300), (512, 600), (256, 256), (1024, 20)):
        probe(dev, w, h, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING, f"scanout|render {w}x{h}")
