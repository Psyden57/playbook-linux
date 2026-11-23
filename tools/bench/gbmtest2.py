import ctypes, sys

EGL_PLATFORM_GBM_MESA = 0x31D7
EGL_VENDOR, EGL_VERSION, EGL_CLIENT_APIS = 0x3053, 0x3054, 0x308D
EGL_EXTENSIONS = 0x3055
EGL_OPENGL_ES_API = 0x30A0
EGL_SURFACE_TYPE, EGL_WINDOW_BIT = 0x3033, 0x0004
EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT = 0x3040, 0x0004
EGL_NONE = 0x3038
EGL_CONTEXT_CLIENT_VERSION = 0x3098
EGL_WIDTH, EGL_HEIGHT = 0x3057, 0x3056
GL_VENDOR, GL_RENDERER, GL_VERSION = 0x1F00, 0x1F01, 0x1F02

node = sys.argv[1] if len(sys.argv) > 1 else "/dev/dri/renderD129"

gbm = ctypes.CDLL("libgbm.so.1")
gbm.gbm_create_device.restype = ctypes.c_void_p
gbm.gbm_create_device.argtypes = [ctypes.c_int]
gbm.gbm_device_get_backend_name.restype = ctypes.c_char_p
gbm.gbm_device_get_backend_name.argtypes = [ctypes.c_void_p]

fd = __import__("os").open(node, __import__("os").O_RDWR)
dev = gbm.gbm_create_device(fd)
if not dev:
    print("gbm_create_device(%s) FAILED" % node)
    sys.exit(1)
print("gbm backend :", (gbm.gbm_device_get_backend_name(dev) or b"?").decode())

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
egl.eglCreatePbufferSurface.restype = ctypes.c_void_p
egl.eglCreatePbufferSurface.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
egl.eglMakeCurrent.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
egl.eglGetProcAddress.restype = ctypes.c_void_p
egl.eglGetProcAddress.argtypes = [ctypes.c_char_p]

attribs0 = (ctypes.c_int * 1)(EGL_NONE)
dpy = egl.eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA, dev, attribs0)
major, minor = ctypes.c_int(), ctypes.c_int()
if not dpy or not egl.eglInitialize(dpy, ctypes.byref(major), ctypes.byref(minor)):
    print("eglInitialize(GBM) FAILED")
    sys.exit(1)
print("EGL vendor  :", (egl.eglQueryString(dpy, EGL_VENDOR) or b"?").decode())
print("EGL version :", (egl.eglQueryString(dpy, EGL_VERSION) or b"?").decode(), "(%d.%d)" % (major.value, minor.value))
ext = (egl.eglQueryString(dpy, EGL_EXTENSIONS) or b"").decode()
print("dmabuf export:", "EGL_MESA_image_dma_buf_export" in ext)

egl.eglBindAPI(EGL_OPENGL_ES_API)
cfg_attr = (ctypes.c_int * 5)(EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE)
cfgs = (ctypes.c_void_p * 32)()
n = ctypes.c_int()
egl.eglChooseConfig(dpy, cfg_attr, cfgs, 32, ctypes.byref(n))
print("window configs:", n.value)
if n.value == 0:
    sys.exit(1)
ctx_attr = (ctypes.c_int * 3)(EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE)
ctx = egl.eglCreateContext(dpy, cfgs[0], None, ctx_attr)
if not ctx:
    print("eglCreateContext FAILED")
    sys.exit(1)
if not egl.eglMakeCurrent(dpy, None, None, ctx):
    pb_attr = (ctypes.c_int * 5)(EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE)
    surf = egl.eglCreatePbufferSurface(dpy, cfgs[0], pb_attr)
    if not surf or not egl.eglMakeCurrent(dpy, surf, surf, ctx):
        print("makeCurrent FAILED (neither surfaceless nor pbuffer)")
        sys.exit(1)
print("context     : current")

gles = ctypes.CDLL("libGLESv2.so.2")
gles.glGetString.restype = ctypes.c_char_p
gles.glGetString.argtypes = [ctypes.c_uint]
print("GL_VENDOR   :", (gles.glGetString(GL_VENDOR) or b"?").decode())
print("GL_RENDERER :", (gles.glGetString(GL_RENDERER) or b"?").decode())
print("GL_VERSION  :", (gles.glGetString(GL_VERSION) or b"?").decode())
print("OK")
