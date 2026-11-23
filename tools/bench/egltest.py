import ctypes, ctypes.util, sys

EGL_PLATFORM_SURFACELESS_MESA = 0x31DD
EGL_VENDOR, EGL_VERSION, EGL_CLIENT_APIS = 0x3053, 0x3054, 0x308D
EGL_OPENGL_ES_API = 0x30A0
EGL_SURFACE_TYPE, EGL_PBUFFER_BIT = 0x3033, 0x0001
EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT = 0x3040, 0x0004
EGL_NONE = 0x3038
EGL_CONTEXT_CLIENT_VERSION = 0x3098
EGL_WIDTH, EGL_HEIGHT = 0x3057, 0x3056
GL_VENDOR, GL_RENDERER, GL_VERSION, GL_EXTENSIONS = 0x1F00, 0x1F01, 0x1F02, 0x1F03

egl = ctypes.CDLL("libEGL.so.1")
egl.eglGetPlatformDisplay.restype = ctypes.c_void_p
egl.eglGetPlatformDisplay.argtypes = [ctypes.c_uint, ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
egl.eglGetDisplay.restype = ctypes.c_void_p
egl.eglGetDisplay.argtypes = [ctypes.c_void_p]
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

attribs0 = (ctypes.c_int * 1)(EGL_NONE)
dpy = egl.eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, None, attribs0)
if not dpy:
    print("eglGetPlatformDisplay(surfaceless) -> NULL; trying eglGetDisplay(0)")
    dpy = egl.eglGetDisplay(None)
major, minor = ctypes.c_int(), ctypes.c_int()
if not egl.eglInitialize(dpy, ctypes.byref(major), ctypes.byref(minor)):
    print("eglInitialize FAILED")
    sys.exit(1)
print("EGL vendor  :", (egl.eglQueryString(dpy, EGL_VENDOR) or b"?").decode())
print("EGL version :", (egl.eglQueryString(dpy, EGL_VERSION) or b"?").decode(), "(%d.%d)" % (major.value, minor.value))
print("EGL clientAPIs:", (egl.eglQueryString(dpy, EGL_CLIENT_APIS) or b"?").decode())

egl.eglBindAPI(EGL_OPENGL_ES_API)
cfg_attr = (ctypes.c_int * 7)(EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE, EGL_NONE, EGL_NONE)
cfg = ctypes.c_void_p()
n = ctypes.c_int()
if not egl.eglChooseConfig(dpy, cfg_attr, ctypes.byref(cfg), 1, ctypes.byref(n)) or n.value == 0:
    print("eglChooseConfig found", n.value, "configs")
    sys.exit(1)
ctx_attr = (ctypes.c_int * 3)(EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE)
ctx = egl.eglCreateContext(dpy, cfg, None, ctx_attr)
if not ctx:
    print("eglCreateContext FAILED")
    sys.exit(1)
pb_attr = (ctypes.c_int * 5)(EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE)
surf = egl.eglCreatePbufferSurface(dpy, cfg, pb_attr)
if not surf or not egl.eglMakeCurrent(dpy, surf, surf, ctx):
    print("pbuffer/makeCurrent FAILED")
    sys.exit(1)

gles = ctypes.CDLL("libGLESv2.so.2")
gles.glGetString.restype = ctypes.c_char_p
gles.glGetString.argtypes = [ctypes.c_uint]
print("GL_VENDOR   :", (gles.glGetString(GL_VENDOR) or b"?").decode())
print("GL_RENDERER :", (gles.glGetString(GL_RENDERER) or b"?").decode())
print("GL_VERSION  :", (gles.glGetString(GL_VERSION) or b"?").decode())
print("OK")
