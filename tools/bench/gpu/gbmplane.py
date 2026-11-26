#!/usr/bin/env python3
# Probe the GBM multi-plane dmabuf-export API that WebKit's AcceleratedSurface uses:
#   gbm_bo_get_plane_count / gbm_bo_get_fd_for_plane / gbm_bo_get_offset / gbm_bo_get_stride_for_plane
import ctypes, os, sys

lib = ctypes.CDLL("libgbm.so.1")
lib.gbm_create_device.argtypes = [ctypes.c_int]; lib.gbm_create_device.restype = ctypes.c_void_p
lib.gbm_device_destroy.argtypes = [ctypes.c_void_p]
lib.gbm_bo_create.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32]
lib.gbm_bo_create.restype = ctypes.c_void_p
lib.gbm_bo_destroy.argtypes = [ctypes.c_void_p]
lib.gbm_bo_get_plane_count.argtypes = [ctypes.c_void_p]; lib.gbm_bo_get_plane_count.restype = ctypes.c_int
lib.gbm_bo_get_fd.argtypes = [ctypes.c_void_p]; lib.gbm_bo_get_fd.restype = ctypes.c_int
lib.gbm_bo_get_fd_for_plane.argtypes = [ctypes.c_void_p, ctypes.c_int]; lib.gbm_bo_get_fd_for_plane.restype = ctypes.c_int
lib.gbm_bo_get_stride.argtypes = [ctypes.c_void_p]; lib.gbm_bo_get_stride.restype = ctypes.c_uint32
lib.gbm_bo_get_stride_for_plane.argtypes = [ctypes.c_void_p, ctypes.c_int]; lib.gbm_bo_get_stride_for_plane.restype = ctypes.c_uint32
lib.gbm_bo_get_offset.argtypes = [ctypes.c_void_p, ctypes.c_int]; lib.gbm_bo_get_offset.restype = ctypes.c_uint32
lib.gbm_bo_get_modifier.argtypes = [ctypes.c_void_p]; lib.gbm_bo_get_modifier.restype = ctypes.c_uint64

AR24 = 0x34325241  # DRM_FORMAT_ARGB8888 little-endian
RENDERING = 4
SCANOUT = 1

nodes = ["/dev/dri/renderD129", "/dev/dri/card1", "/dev/dri/card0", "/dev/dri/renderD128"]
for node in nodes:
    if not os.path.exists(node):
        print(node, "-> missing"); continue
    try:
        fd = os.open(node, os.O_RDWR | os.O_CLOEXEC)
    except OSError as e:
        print(node, "-> open failed:", e); continue
    dev = lib.gbm_create_device(fd)
    if not dev:
        print(node, "-> gbm_create_device FAILED"); os.close(fd); continue
    for usage, uname in ((RENDERING | SCANOUT, "rend|scan"), (RENDERING, "rend")):
        bo = lib.gbm_bo_create(dev, 64, 64, AR24, usage)
        if not bo:
            print(f"{node} [{uname}] -> gbm_bo_create FAILED"); continue
        pc = lib.gbm_bo_get_plane_count(bo)
        fd0 = lib.gbm_bo_get_fd_for_plane(bo, 0)
        fdg = lib.gbm_bo_get_fd(bo)
        st = lib.gbm_bo_get_stride(bo)
        st0 = lib.gbm_bo_get_stride_for_plane(bo, 0)
        off0 = lib.gbm_bo_get_offset(bo, 0)
        mod = lib.gbm_bo_get_modifier(bo)
        print(f"{node} [{uname}] plane_count={pc} fd_for_plane0={fd0} gbm_bo_get_fd={fdg} stride={st} stride_plane0={st0} offset0={off0} modifier=0x{mod:x}")
        for f in (fd0, fdg):
            if f >= 0:
                try: os.close(f)
                except OSError: pass
        lib.gbm_bo_destroy(bo)
    lib.gbm_device_destroy(dev)
    os.close(fd)
