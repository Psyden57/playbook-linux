import ctypes, os, sys

# EGL platform + enums
EGL_PLATFORM_WAYLAND_KHR = 0x31D8
EGL_VENDOR, EGL_VERSION, EGL_CLIENT_APIS = 0x3053, 0x3054, 0x308D
EGL_EXTENSIONS = 0x3055
EGL_OPENGL_ES_API = 0x30A0
EGL_SURFACE_TYPE, EGL_PBUFFER_BIT = 0x3033, 0x0001
EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT = 0x3040, 0x0004
EGL_NONE = 0x3038
EGL_CONTEXT_CLIENT_VERSION = 0x3098
GL_VENDOR, GL_RENDERER, GL_VERSION = 0x1F00, 0x1F01, 0x1F02

wl = ctypes.CDLL("libwayland-client.so.0")
wl.wl_display_connect.restype = ctypes.c_void_p
wl.wl_display_connect.argtypes = [ctypes.c_char_p]
wl_dpy = wl.wl_display_connect(None)
if not wl_dpy:
    print("wl_display_connect(%r) failed" % os.environ.get("WAYLAND_DISPLAY"))
    sys.exit(1)
print("wl_display   : connected (%s)" % os.environ.get("WAYLAND_DISPLAY"))

egl = ctypes.CDLL("libEGL.so.1")
egl.eglGetPlatformDisplay.restype = ctypes.c_void_p
egl.eglGetPlatformDisplay.argtypes = [ctypes.c_uint, ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
egl.eglInitialize.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
egl.eglQueryString.restype = ctypes.c_char_p
egl.eglQueryString.argtypes = [ctypes.c_void_p, ctypes.c_int]
egl.eglBindAPI.argtypes = [ctypes.c_uint]
egl.eglChooseConfig.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_void_p), ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
egl.eglCreateContext.restype = ctypes.c_void_p
egl.eglCreateContext.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
egl.eglMakeCurrent.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
egl.eglGetError.restype = ctypes.c_int

attribs0 = (ctypes.c_int * 1)(EGL_NONE)
dpy = egl.eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, ctypes.c_void_p(wl_dpy), attribs0)
if not dpy:
    print("eglGetPlatformDisplay(wayland) -> NULL")
    sys.exit(1)
major, minor = ctypes.c_int(), ctypes.c_int()
if not egl.eglInitialize(dpy, ctypes.byref(major), ctypes.byref(minor)):
    print("eglInitialize(wayland) FAILED, eglGetError=0x%x" % egl.eglGetError())
    sys.exit(1)
print("EGL vendor  :", (egl.eglQueryString(dpy, EGL_VENDOR) or b"?").decode())
print("EGL version :", (egl.eglQueryString(dpy, EGL_VERSION) or b"?").decode(), "(%d.%d)" % (major.value, minor.value))
ext = (egl.eglQueryString(dpy, EGL_EXTENSIONS) or b"").decode()
print("dmabuf export:", "EGL_MESA_image_dma_buf_export" in ext)

egl.eglBindAPI(EGL_OPENGL_ES_API)
cfg_attr = (ctypes.c_int * 3)(EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE)
cfgs = (ctypes.c_void_p * 32)()
n = ctypes.c_int()
egl.eglChooseConfig(dpy, cfg_attr, cfgs, 32, ctypes.byref(n))
print("ES2 configs :", n.value)
if n.value == 0:
    sys.exit(1)
cfg = cfgs[0]
ctx_attr = (ctypes.c_int * 3)(EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE)
ctx = egl.eglCreateContext(dpy, cfg, None, ctx_attr)
if not ctx:
    print("eglCreateContext FAILED, eglGetError=0x%x" % egl.eglGetError())
    sys.exit(1)
if not egl.eglMakeCurrent(dpy, None, None, ctx):
    print("eglMakeCurrent(surfaceless) FAILED, eglGetError=0x%x" % egl.eglGetError())
    sys.exit(1)

gles = ctypes.CDLL("libGLESv2.so.2")
gles.glGetString.restype = ctypes.c_char_p
gles.glGetString.argtypes = [ctypes.c_uint]
print("GL_VENDOR   :", (gles.glGetString(GL_VENDOR) or b"?").decode())
print("GL_RENDERER :", (gles.glGetString(GL_RENDERER) or b"?").decode())
print("GL_VERSION  :", (gles.glGetString(GL_VERSION) or b"?").decode())

drivers = set()
for line in open("/proc/self/maps"):
    for tag in ("pvr_dri", "swrast_dri", "kms_swrast_dri"):
        if tag in line:
            drivers.add(tag)
print("dri mapped  :", sorted(drivers))
print("OK")
