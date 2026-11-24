/*
 * pb-layerdrm — a layer-shell surface carrying a wl_drm PRIME buffer whose
 * pixels we wrote ourselves (omapdrm dumb buffer).  Layer placement is the
 * same class as wvkbd's (which displays), so this isolates the wl_drm buffer
 * path from catacomb's window/layout handling.
 *
 * Usage: pb-layerdrm [width height] [seconds]
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wayland-drm-client-protocol.h"
#include "wlr-layer-shell-client-protocol.h"

#define DRM_FORMAT_ARGB8888 0x34325241u

struct drm_mode_create_dumb {
    uint32_t height, width, bpp, flags, handle, pitch;
    uint64_t size;
};
struct drm_mode_map_dumb {
    uint32_t handle, pad;
    uint64_t offset;
};
struct drm_prime_handle {
    uint32_t handle, flags;
    int32_t fd;
};
#define DRM_IOCTL_MODE_CREATE_DUMB _IOWR('d', 0xb2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB _IOWR('d', 0xb3, struct drm_mode_map_dumb)
#define DRM_IOCTL_PRIME_HANDLE_TO_FD _IOWR('d', 0x2d, struct drm_prime_handle)

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_output *output;
static struct zwlr_layer_shell_v1 *shell;
static struct wl_drm *drm;
static struct wl_shm *shm;
static struct wl_surface *surface;
static struct zwlr_layer_surface_v1 *lsurf;
static int configured, cfg_w, cfg_h;
static int prime_ok;
static char devname[256];

static void drm_device(void *d, struct wl_drm *r, const char *name)
{
    (void)d; (void)r; snprintf(devname, sizeof(devname), "%s", name);
    printf("wl_drm device: %s\n", name);
}
static void drm_format(void *d, struct wl_drm *r, uint32_t f)
{
    (void)d; (void)r; printf("wl_drm format: 0x%x\n", f);
}
static void drm_auth(void *d, struct wl_drm *r) { (void)d; (void)r; }
static void drm_caps(void *d, struct wl_drm *r, uint32_t v)
{
    (void)d; (void)r; prime_ok = (v & WL_DRM_CAPABILITY_PRIME) != 0;
    printf("wl_drm capabilities: 0x%x prime=%d\n", v, prime_ok);
}
static const struct wl_drm_listener drm_listener = {
    .device = drm_device, .format = drm_format,
    .authenticated = drm_auth, .capabilities = drm_caps,
};

static void ls_configure(void *d, struct zwlr_layer_surface_v1 *s, uint32_t serial,
                         uint32_t w, uint32_t h)
{
    (void)d;
    cfg_w = w; cfg_h = h; configured = 1;
    zwlr_layer_surface_v1_ack_configure(s, serial);
}
static void ls_closed(void *d, struct zwlr_layer_surface_v1 *s) { (void)d; (void)s; }
static const struct zwlr_layer_surface_v1_listener ls_listener = {
    .configure = ls_configure, .closed = ls_closed,
};

static void registry_global(void *d, struct wl_registry *r, uint32_t name,
                            const char *iface, uint32_t version)
{
    (void)d;
    if (!strcmp(iface, "wl_compositor"))
        compositor = wl_registry_bind(r, name, &wl_compositor_interface,
                                      version < 4 ? version : 4);
    else if (!strcmp(iface, "wl_output") && !output)
        output = wl_registry_bind(r, name, &wl_output_interface, 1);
    else if (!strcmp(iface, "zwlr_layer_shell_v1"))
        shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, version < 1 ? version : 1);
    else if (!strcmp(iface, "wl_shm"))
        shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, "wl_drm")) {
        drm = wl_registry_bind(r, name, &wl_drm_interface, version < 2 ? version : 2);
        wl_drm_add_listener(drm, &drm_listener, NULL);
    }
}
static void registry_remove(void *d, struct wl_registry *r, uint32_t n)
{
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener registry_listener = {
    .global = registry_global, .global_remove = registry_remove,
};

static int make_shm_file(size_t size)
{
    const char *dir = getenv("XDG_RUNTIME_DIR");
    char path[256];
    snprintf(path, sizeof(path), "%s/pb-shm-XXXXXX", dir ? dir : "/tmp");
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    unlink(path);
    if (ftruncate(fd, size) < 0) { close(fd); return -1; }
    return fd;
}

int main(int argc, char **argv)
{
    int w = argc > 1 ? atoi(argv[1]) : 640;
    int h = argc > 2 ? atoi(argv[2]) : 200;
    int secs = argc > 3 ? atoi(argv[3]) : 20;
    int use_shm = argc > 4 && !strcmp(argv[4], "shm");

    display = wl_display_connect(NULL);
    if (!display) { fprintf(stderr, "no display\n"); return 1; }
    struct wl_registry *reg = wl_display_get_registry(display);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);
    printf("comp=%p output=%p shell=%p drm=%p\n", (void *)compositor, (void *)output,
           (void *)shell, (void *)drm);
    if (!compositor || !shell || !drm) return 1;

    int fd = open("/dev/dri/card0", O_RDWR);
    struct drm_mode_create_dumb cd = { .width = w, .height = h, .bpp = 32 };
    if (fd < 0 || ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0) { perror("dumb"); return 1; }
    struct drm_mode_map_dumb md = { .handle = cd.handle };
    if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md) < 0) { perror("map_dumb"); return 1; }
    uint8_t *p = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    for (uint32_t y = 0; y < cd.height; y++) {
        uint32_t *row = (uint32_t *)(p + (uint64_t)y * cd.pitch);
        for (uint32_t x = 0; x < cd.width; x++) {
            int c = ((x / 32) + (y / 32)) & 1;
            row[x] = c ? 0xffff0000u : 0xff00ff00u;  /* red / green */
        }
    }
    msync(p, cd.size, MS_SYNC);
    struct drm_prime_handle ph = { .handle = cd.handle, .flags = O_CLOEXEC | O_RDWR, .fd = -1 };
    if (ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph) < 0) { perror("prime"); return 1; }
    printf("dumb %dx%d pitch=%u primefd=%d device=%s\n", w, h, cd.pitch, ph.fd, devname);
    struct wl_buffer *buf = NULL;
    if (use_shm) {
        if (!shm) { fprintf(stderr, "no wl_shm\n"); return 1; }
        int sfd = make_shm_file(cd.size);
        if (sfd < 0) { perror("shm"); return 1; }
        uint8_t *sp = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, sfd, 0);
        if (sp == MAP_FAILED) { perror("shm mmap"); return 1; }
        memcpy(sp, p, cd.size);
        struct wl_shm_pool *pool = wl_shm_create_pool(shm, sfd, cd.size);
        buf = wl_shm_pool_create_buffer(pool, 0, cd.width, cd.height, cd.pitch,
                                        WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        printf("using shm buffer\n");
    } else {
        buf = wl_drm_create_prime_buffer(
            drm, ph.fd, cd.width, cd.height, DRM_FORMAT_ARGB8888, 0, cd.pitch, 0, 0, 0, 0);
        if (!buf) { fprintf(stderr, "create_prime_buffer failed\n"); return 1; }
    }

    surface = wl_compositor_create_surface(compositor);
    lsurf = zwlr_layer_shell_v1_get_layer_surface(shell, surface, output,
                                                  ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
                                                  "pb-layerdrm");
    zwlr_layer_surface_v1_add_listener(lsurf, &ls_listener, NULL);
    zwlr_layer_surface_v1_set_size(lsurf, w, h);
    zwlr_layer_surface_v1_set_anchor(lsurf, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                           ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_exclusive_zone(lsurf, 0);
    wl_surface_commit(surface);
    while (!configured)
        if (wl_display_dispatch(display) < 0) { fprintf(stderr, "dispatch\n"); return 1; }
    printf("configured %ux%u\n", cfg_w, cfg_h);
    fflush(stdout);

    wl_surface_attach(surface, buf, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, cd.width, cd.height);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);
    printf("attached\n"); fflush(stdout);

    time_t end = time(NULL) + secs;
    while (time(NULL) < end) {
        wl_display_dispatch_pending(display);
        wl_display_flush(display);
        usleep(200000);
    }
    return 0;
}
