/*
 * pb-gbmimport — reproduce catacomb's renderer setup exactly (dlopen'd
 * libEGL, GBM display on card0, config + context, surfaceless current) and
 * import a dma-buf we wrote, then read it back.
 *
 * Usage: pb-gbmimport [w h] [buf-dev] [target: 2d|ext]
 */
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
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
typedef void *(*fn_gpa)(const char *);
typedef PY (*fn_create_image)(PY, PY, unsigned, void *, const int *);

typedef void (*fn_gen)(int, unsigned *);
typedef void (*fn_bind)(unsigned, unsigned);
typedef void (*fn_param)(unsigned, unsigned, int);
typedef void (*fn_fbtex)(unsigned, unsigned, unsigned, unsigned, int);
typedef unsigned (*fn_fbstatus)(unsigned);
typedef void (*fn_readpix)(int, int, int, int, unsigned, unsigned, void *);
typedef unsigned (*fn_geterr)(void);
typedef const unsigned char *(*fn_getstring)(unsigned);
typedef void (*fn_targettex)(unsigned, void *);

struct drm_mode_create_dumb { uint32_t height, width, bpp, flags, handle, pitch; uint64_t size; };
struct drm_mode_map_dumb { uint32_t handle, pad; uint64_t offset; };
struct drm_prime_handle { uint32_t handle, flags; int32_t fd; };
#define DRM_IOCTL_MODE_CREATE_DUMB _IOWR('d', 0xb2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB _IOWR('d', 0xb3, struct drm_mode_map_dumb)
#define DRM_IOCTL_PRIME_HANDLE_TO_FD _IOWR('d', 0x2d, struct drm_prime_handle)

static fn_gen glGenTextures;
static fn_bind glBindTexture;
static fn_param glTexParameteri;
static fn_gen glGenFramebuffers;
static fn_bind glBindFramebuffer;
static fn_fbtex glFramebufferTexture2D;
static fn_fbstatus glCheckFramebufferStatus;
static fn_readpix glReadPixels;
static fn_geterr glGetError;
static fn_getstring glGetString;
static fn_targettex glEGLImageTargetTexture2DOES;

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int w = argc > 1 ? atoi(argv[1]) : 320;
    int h = argc > 2 ? atoi(argv[2]) : 160;
    const char *bufdev = argc > 3 ? argv[3] : "/dev/dri/card0";
    int ext = argc > 4 && !strcmp(argv[4], "ext");

    void *le = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    void *lg = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
    fprintf(stderr, "libEGL=%p libGLESv2=%p\n", le, lg);
    if (!le || !lg) return 1;
    fn_gpd gpd = (fn_gpd)dlsym(le, "eglGetPlatformDisplay");
    fn_bool_pii init = (fn_bool_pii)dlsym(le, "eglInitialize");
    fn_bool_u bindapi = (fn_bool_u)dlsym(le, "eglBindAPI");
    fn_cc choose = (fn_cc)dlsym(le, "eglChooseConfig");
    fn_cc_ctx create = (fn_cc_ctx)dlsym(le, "eglCreateContext");
    fn_mc makecur = (fn_mc)dlsym(le, "eglMakeCurrent");
    fn_err geterr = (fn_err)dlsym(le, "eglGetError");
    fn_gpa gpa = (fn_gpa)dlsym(le, "eglGetProcAddress");
    fn_create_image create_image = (fn_create_image)gpa("eglCreateImageKHR");
#define SYM(n, t) t n = (t)dlsym(lg, #n)
    SYM(glGenTextures, fn_gen);
    SYM(glBindTexture, fn_bind);
    SYM(glTexParameteri, fn_param);
    SYM(glGenFramebuffers, fn_gen);
    SYM(glBindFramebuffer, fn_bind);
    SYM(glFramebufferTexture2D, fn_fbtex);
    SYM(glCheckFramebufferStatus, fn_fbstatus);
    SYM(glReadPixels, fn_readpix);
    SYM(glGetError, fn_geterr);
    SYM(glGetString, fn_getstring);
    glEGLImageTargetTexture2DOES = (fn_targettex)gpa("glEGLImageTargetTexture2DOES");

    int fd = open(bufdev, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    struct gbm_device *gbm = gbm_create_device(fd);
    PY dpy = gpd(0x31D7, gbm, NULL);
    int maj, min;
    printf("dpy=%p init=%d\n", dpy, init(dpy, &maj, &min));
    bindapi(0x30A0);
    int attr[] = { 0x3040, 0x0004, 0x3033, 0x0004,
                   0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, 0x3038 };
    PY cfg = NULL;
    int n = 0;
    choose(dpy, attr, &cfg, 1, &n);
    int cattr[] = { 0x3098, 2, 0x3038 };
    PY ctx = create(dpy, cfg, NULL, cattr);
    printf("ctx=%p makecurrent=%d err=0x%x\n", ctx, makecur(dpy, NULL, NULL, ctx), geterr());
    printf("GL_RENDERER=%s\n", glGetString(0x1F01));

    struct drm_mode_create_dumb cd = { .width = w, .height = h, .bpp = 32 };
    if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0) { perror("dumb"); return 1; }
    struct drm_mode_map_dumb md = { .handle = cd.handle };
    ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md);
    uint8_t *p = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
    for (uint32_t y = 0; y < cd.height; y++) {
        uint32_t *row = (uint32_t *)(p + (uint64_t)y * cd.pitch);
        for (uint32_t x = 0; x < cd.width; x++)
            row[x] = (((x / 32) + (y / 32)) & 1) ? 0xffff0000u : 0xff00ff00u;
    }
    msync(p, cd.size, MS_SYNC);
    struct drm_prime_handle ph = { .handle = cd.handle, .flags = O_CLOEXEC | O_RDWR, .fd = -1 };
    if (ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph) < 0) { perror("prime"); return 1; }
    printf("dumb %ux%u pitch=%u primefd=%d target=%s\n", cd.width, cd.height, cd.pitch,
           ph.fd, ext ? "external" : "2d");

    int attrs[] = { 0x3057, w, 0x3056, h,
                    0x3271, 0x34325241,
                    0x3272, ph.fd, 0x3273, 0, 0x3274, (int)cd.pitch,
                    0x3038 };
    PY img = create_image(dpy, NULL, 0x3270, NULL, attrs);
    printf("image=%p err=0x%x\n", img, geterr());
    if (!img) return 1;
    unsigned tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(ext ? 0x8D65 : 0x0DE1, tex);
    glTexParameteri(ext ? 0x8D65 : 0x0DE1, 0x2801, 0x2601);
    glTexParameteri(ext ? 0x8D65 : 0x0DE1, 0x2800, 0x2601);
    printf("binding image (target %s)\n", ext ? "EXT" : "2D");
    glEGLImageTargetTexture2DOES(ext ? 0x8D65 : 0x0DE1, img);
    printf("target err=0x%x\n", glGetError());
    unsigned fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(0x8D40, fbo);
    if (ext) {
        printf("EXTERNAL texture cannot be a FBO attachment; use a shader pass\n");
        return 0;
    }
    glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, tex, 0);
    printf("fbo status=0x%x (complete 0x%x)\n", glCheckFramebufferStatus(0x8D40), 0x8CD5);
    unsigned char px[8 * 8 * 4];
    glReadPixels(0, 0, 8, 8, 0x1908, 0x1401, px);
    int nz = 0, mx = 0;
    for (int i = 0; i < 8 * 8 * 4; i += 4) {
        if (px[i] | px[i + 1] | px[i + 2]) nz++;
        int m = px[i];
        if (px[i + 1] > m) m = px[i + 1];
        if (px[i + 2] > m) m = px[i + 2];
        if (m > mx) mx = m;
    }
    printf("readback first=(%d,%d,%d,%d) nz=%d/64 max=%d err=0x%x\n",
           px[0], px[1], px[2], px[3], nz, mx, glGetError());
    return 0;
}
