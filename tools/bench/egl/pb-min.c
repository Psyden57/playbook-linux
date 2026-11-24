#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <gbm.h>
#include <EGL/egl.h>

#ifndef EGL_PLATFORM_GBM_MESA
#define EGL_PLATFORM_GBM_MESA 0x31D7
#endif

int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/dri/card0";
    int fd = open(dev, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    struct gbm_device *gbm = gbm_create_device(fd);
    printf("gbm=%p err=0x%x\n", (void *)gbm, eglGetError());
    typedef EGLDisplay (*getpd_fn)(EGLenum, void *, const EGLint *);
    getpd_fn gpd_ext = (getpd_fn)eglGetProcAddress("eglGetPlatformDisplayEXT");
    printf("gpd_ext=%p\n", (void *)gpd_ext);
    EGLDisplay dpy = gpd_ext ? gpd_ext(EGL_PLATFORM_GBM_MESA, gbm, NULL)
                             : eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA, gbm, NULL);
    printf("dpy=%p err=0x%x\n", (void *)dpy, eglGetError());
    EGLint maj, min;
    printf("init=%d err=0x%x\n", eglInitialize(dpy, &maj, &min), eglGetError());
    printf("bind=%d err=0x%x\n", eglBindAPI(EGL_OPENGL_ES_API), eglGetError());
    EGLContext ctx = eglCreateContext(dpy, NULL, EGL_NO_CONTEXT, NULL);
    printf("ctx=%p err=0x%x\n", (void *)ctx, eglGetError());
    printf("makecurrent=%d err=0x%x\n",
           eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx), eglGetError());
    return 0;
}
