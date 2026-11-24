/*
 * pb-gbmsample — import a dma-buf the way catacomb does and SAMPLE it in a
 * shader (the compositor's real operation), then read the destination.
 *
 * dlopen'd EGL/GLES, GBM display on card0, config+context, surfaceless.
 * Usage: pb-gbmsample [w h] [buf-dev]
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
static void *le, *lg;

#define E(n, t) static t n
typedef PY (*fn_gpd)(unsigned, void *, const int *);
static fn_gpd gpd; static int (*e_init)(PY, int *, int *); static int (*e_bindapi)(unsigned);
static int (*e_choose)(PY, const int *, PY *, int, int *); static PY (*e_create)(PY, PY, PY, const int *);
static int (*e_makecur)(PY, PY, PY, PY); static int (*e_geterr)(void); static void *(*e_gpa)(const char *);
typedef PY (*fn_ci)(PY, PY, unsigned, void *, const int *);
static fn_ci e_create_image; static void (*gl_target_tex)(unsigned, void *);
static void (*glGenTextures)(int, unsigned *), (*glBindTexture)(unsigned, unsigned);
static void (*glTexParameteri)(unsigned, unsigned, int);
static void (*glGenFramebuffers)(int, unsigned *), (*glBindFramebuffer)(unsigned, unsigned);
static void (*glFramebufferTexture2D)(unsigned, unsigned, unsigned, unsigned, int);
static unsigned (*glCheckFramebufferStatus)(unsigned);
static void (*glReadPixels)(int, int, int, int, unsigned, unsigned, void *);
static unsigned (*glGetError)(void);
static const unsigned char *(*glGetString)(unsigned);
static void (*glTexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
static void (*glViewport)(int, int, int, int);
static unsigned (*glCreateShader)(unsigned); static void (*glShaderSource)(unsigned, int, const char *const *, const int *);
static void (*glCompileShader)(unsigned); static void (*glGetShaderiv)(unsigned, unsigned, int *);
static unsigned (*glCreateProgram)(void); static void (*glAttachShader)(unsigned, unsigned);
static void (*glLinkProgram)(unsigned); static void (*glUseProgram)(unsigned);
static int (*glGetAttribLocation)(unsigned, const char *); static void (*glEnableVertexAttribArray)(unsigned);
static void (*glVertexAttribPointer)(unsigned, int, unsigned, unsigned char, int, const void *);
static int (*glGetUniformLocation)(unsigned, const char *); static void (*glUniform1i)(int, int);
static void (*glDrawArrays)(unsigned, int, int); static void (*glClearColor)(float, float, float, float);
static void (*glClear)(unsigned);

static void load(void)
{
    le = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    lg = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!le || !lg) { fprintf(stderr, "dlopen failed\n"); exit(1); }
    gpd = (fn_gpd)dlsym(le, "eglGetPlatformDisplay");
    e_init = dlsym(le, "eglInitialize"); e_bindapi = dlsym(le, "eglBindAPI");
    e_choose = dlsym(le, "eglChooseConfig"); e_create = dlsym(le, "eglCreateContext");
    e_makecur = dlsym(le, "eglMakeCurrent"); e_geterr = dlsym(le, "eglGetError");
    e_gpa = dlsym(le, "eglGetProcAddress");
    e_create_image = (fn_ci)e_gpa("eglCreateImageKHR");
    gl_target_tex = (void (*)(unsigned, void *))e_gpa("glEGLImageTargetTexture2DOES");
#define G(n) gl##n = dlsym(lg, "gl" #n)
    G(GenTextures); G(BindTexture); G(TexParameteri); G(GenFramebuffers); G(BindFramebuffer);
    G(FramebufferTexture2D); G(CheckFramebufferStatus); G(ReadPixels); G(GetError); G(GetString);
    G(TexImage2D); G(Viewport); G(CreateShader); G(ShaderSource); G(CompileShader); G(GetShaderiv);
    G(CreateProgram); G(AttachShader); G(LinkProgram); G(UseProgram); G(GetAttribLocation);
    G(EnableVertexAttribArray); G(VertexAttribPointer); G(GetUniformLocation); G(Uniform1i);
    G(DrawArrays); G(ClearColor); G(Clear);
}

struct drm_mode_create_dumb { uint32_t height, width, bpp, flags, handle, pitch; uint64_t size; };
struct drm_mode_map_dumb { uint32_t handle, pad; uint64_t offset; };
struct drm_prime_handle { uint32_t handle, flags; int32_t fd; };
#define DRM_IOCTL_MODE_CREATE_DUMB _IOWR('d', 0xb2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB _IOWR('d', 0xb3, struct drm_mode_map_dumb)
#define DRM_IOCTL_PRIME_HANDLE_TO_FD _IOWR('d', 0x2d, struct drm_prime_handle)

static unsigned shader(unsigned type, const char *src)
{
    unsigned s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, 0x8B81, &ok);
    if (!ok) { fprintf(stderr, "shader compile failed\n"); }
    return s;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int w = argc > 1 ? atoi(argv[1]) : 320;
    int h = argc > 2 ? atoi(argv[2]) : 160;
    const char *bufdev = argc > 3 ? argv[3] : "/dev/dri/card0";
    int use_ext = argc > 4 && !strcmp(argv[4], "ext");
    load();

    int fd = open(bufdev, O_RDWR);
    struct gbm_device *gbm = gbm_create_device(fd);
    PY dpy = gpd(0x31D7, gbm, NULL);
    int maj, min;
    e_init(dpy, &maj, &min);
    e_bindapi(0x30A0);
    int attr[] = { 0x3040, 0x0004, 0x3033, 0x0004, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, 0x3038 };
    PY cfg = NULL;
    int n = 0, cattr[] = { 0x3098, 2, 0x3038 };
    e_choose(dpy, attr, &cfg, 1, &n);
    PY ctx = e_create(dpy, cfg, NULL, cattr);
    printf("current=%d err=0x%x GL=%s\n", e_makecur(dpy, NULL, NULL, ctx), e_geterr(),
           glGetString(0x1F01));

    struct drm_mode_create_dumb cd = { .width = w, .height = h, .bpp = 32 };
    ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd);
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
    ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph);

    int attrs[] = { 0x3057, w, 0x3056, h, 0x3271, 0x34325241, 0x3272, ph.fd,
                    0x3273, 0, 0x3274, (int)cd.pitch, 0x3038 };
    PY img = e_create_image(dpy, NULL, 0x3270, NULL, attrs);
    printf("image=%p err=0x%x\n", img, e_geterr());
    unsigned tgt = use_ext ? 0x8D65u : 0x0DE1u;
    unsigned src = 0;
    glGenTextures(1, &src);
    glBindTexture(tgt, src);
    glTexParameteri(tgt, 0x2801, 0x2601);
    glTexParameteri(tgt, 0x2800, 0x2601);
    glTexParameteri(tgt, 0x2802, 0x812F);
    glTexParameteri(tgt, 0x2803, 0x812F);
    gl_target_tex(tgt, img);
    printf("bound err=0x%x\n", glGetError());

    /* Destination texture + FBO. */
    unsigned dst = 0, fbo = 0;
    glGenTextures(1, &dst);
    glBindTexture(0x0DE1, dst);
    glTexImage2D(0x0DE1, 0, 0x1908, 64, 64, 0, 0x1908, 0x1401, NULL);
    glTexParameteri(0x0DE1, 0x2801, 0x2601);
    glTexParameteri(0x0DE1, 0x2800, 0x2601);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(0x8D40, fbo);
    glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, dst, 0);
    printf("dst fbo=0x%x\n", glCheckFramebufferStatus(0x8D40));

    const char *vs =
        "attribute vec2 pos; varying vec2 uv;\n"
        "void main(){ uv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }\n";
    const char *fs = use_ext ?
        "#extension GL_OES_EGL_image_external : require\n"
        "precision mediump float; varying vec2 uv; uniform samplerExternalOES s;\n"
        "void main(){ gl_FragColor = texture2D(s, uv); }\n" :
        "precision mediump float; varying vec2 uv; uniform sampler2D s;\n"
        "void main(){ gl_FragColor = texture2D(s, uv); }\n";
    unsigned prog = glCreateProgram();
    glAttachShader(prog, shader(0x8B31, vs));
    glAttachShader(prog, shader(0x8B30, fs));
    glLinkProgram(prog);
    glUseProgram(prog);
    int loc = glGetAttribLocation(prog, "pos");
    glEnableVertexAttribArray(loc);
    static const float quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    glVertexAttribPointer(loc, 2, 0x1406, 0, 0, quad);
    glUniform1i(glGetUniformLocation(prog, "s"), 0);
    glBindTexture(tgt, src);
    glViewport(0, 0, 64, 64);
    glClearColor(0, 0, 1, 1);
    glClear(0x4000);
    printf("drawing...\n");
    glDrawArrays(0x0005, 0, 4);
    printf("draw err=0x%x\n", glGetError());
    unsigned char px[64 * 4];
    glReadPixels(0, 0, 64, 1, 0x1908, 0x1401, px);
    int nz = 0, mx = 0, blue = 0;
    for (int i = 0; i < 64 * 4; i += 4) {
        if (px[i] | px[i + 1] | px[i + 2]) nz++;
        int m = px[i];
        if (px[i + 1] > m) m = px[i + 1];
        if (px[i + 2] > m) m = px[i + 2];
        if (m > mx) mx = m;
        if (px[i + 2] > 128 && px[i] < 64) blue++;
    }
    printf("sampled row: first=(%d,%d,%d,%d) nz=%d/64 max=%d blue=%d err=0x%x\n",
           px[0], px[1], px[2], px[3], nz, mx, blue, glGetError());
    return 0;
}
