#include <dlfcn.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <gbm.h>

typedef void *PY;
typedef PY (*fn_gpd)(unsigned, void *, const int *);
typedef int (*fn_bool_pii)(PY, int *, int *);
typedef int (*fn_bool_u)(unsigned);
typedef int (*fn_cc)(PY, const int *, PY *, int, int *);
typedef PY (*fn_cc_ctx)(PY, PY, PY, const int *);
typedef int (*fn_mc)(PY, PY, PY, PY);
typedef int (*fn_err)(void);

int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/dri/card0";
    void *lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    printf("dlopen=%p err=%s\n", lib, lib ? "-" : dlerror());
    if (!lib) return 1;
    fn_gpd gpd = (fn_gpd)dlsym(lib, "eglGetPlatformDisplay");
    fn_bool_pii init = (fn_bool_pii)dlsym(lib, "eglInitialize");
    fn_bool_u bindapi = (fn_bool_u)dlsym(lib, "eglBindAPI");
    fn_cc choose = (fn_cc)dlsym(lib, "eglChooseConfig");
    fn_cc_ctx create = (fn_cc_ctx)dlsym(lib, "eglCreateContext");
    fn_mc makecur = (fn_mc)dlsym(lib, "eglMakeCurrent");
    fn_err geterr = (fn_err)dlsym(lib, "eglGetError");
    printf("syms %p %p %p %p %p %p %p\n", (void *)gpd, (void *)init, (void *)bindapi,
           (void *)choose, (void *)create, (void *)makecur, (void *)geterr);
    int fd = open(dev, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    struct gbm_device *gbm = gbm_create_device(fd);
    printf("gbm=%p\n", (void *)gbm);
    PY dpy = gpd(0x31D7, gbm, NULL);
    printf("dpy=%p err=0x%x\n", dpy, geterr());
    int maj, min;
    printf("init=%d err=0x%x\n", init(dpy, &maj, &min), geterr());
    printf("bind=%d err=0x%x\n", bindapi(0x30A0), geterr());
    int attr[] = { 0x3040, 0x0004, 0x3033, 0x0004,
                   0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, 0x3038 };
    PY cfg = NULL;
    int n = 0;
    printf("choose=%d n=%d err=0x%x\n", choose(dpy, attr, &cfg, 1, &n), n, geterr());
    int cattr[] = { 0x3098, 2, 0x3038 };
    PY ctx = create(dpy, cfg, NULL, cattr);
    printf("ctx=%p err=0x%x\n", ctx, geterr());
    printf("makecurrent=%d err=0x%x\n", makecur(dpy, NULL, NULL, ctx), geterr());
    return 0;
}
