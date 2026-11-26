/*
 * gbmprime2 — bounded DDK/GBM export test.
 *
 *   gbmprime2 <node> <rend|scan|rendscan> <master 0|1>
 *
 * Optionally drmSetMaster()s the fd (root; only meaningful on a primary node),
 * then creates a GBM BO and reports whether the pvr DDK can export it:
 *   - gbm_bo_get_fd           (DRI queryImage ATTRIB_FD -> PVRDRIBufferGetFd)
 *   - gbm_bo_get_fd_for_plane (per-plane)
 *   - handle_for_plane + drmPrimeHandleToFD
 * Build: $MUSL_CC -O2 -o gbmprime2 gbmprime2.c -ldl
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>

typedef union { void *ptr; int32_t s32; uint32_t u32; int64_t s64; uint64_t u64; } gbm_bo_handle_t;
typedef struct gbm_device gbm_device;
typedef struct gbm_bo gbm_bo;

int main(int argc, char **argv)
{
    const char *node = argc > 1 ? argv[1] : "/dev/dri/renderD129";
    const char *u = argc > 2 ? argv[2] : "rend";
    int master = argc > 3 ? atoi(argv[3]) : 0;
    uint32_t usage = 4; /* GBM_BO_USE_RENDERING */
    if (!strcmp(u, "scan") || !strcmp(u, "rendscan")) usage |= 1; /* | SCANOUT */

    void *g = dlopen("libgbm.so.1", RTLD_NOW);
    void *d = dlopen("libdrm.so.2", RTLD_NOW);
    if (!g || !d) { printf("dlopen failed\n"); return 2; }
    gbm_device *(*create_dev)(int) = (gbm_device *(*)(int))dlsym(g, "gbm_create_device");
    gbm_bo *(*bo_create)(gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t) =
        (gbm_bo *(*)(gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t))dlsym(g, "gbm_bo_create");
    int (*plane_count)(gbm_bo *) = (int (*)(gbm_bo *))dlsym(g, "gbm_bo_get_plane_count");
    int (*get_fd)(gbm_bo *) = (int (*)(gbm_bo *))dlsym(g, "gbm_bo_get_fd");
    int (*get_fd_plane)(gbm_bo *, int) = (int (*)(gbm_bo *, int))dlsym(g, "gbm_bo_get_fd_for_plane");
    gbm_bo_handle_t (*get_handle)(gbm_bo *, int) =
        (gbm_bo_handle_t (*)(gbm_bo *, int))dlsym(g, "gbm_bo_get_handle_for_plane");
    int (*prime)(int, uint32_t, uint32_t, int *) =
        (int (*)(int, uint32_t, uint32_t, int *))dlsym(d, "drmPrimeHandleToFD");
    int (*set_master)(int) = (int (*)(int))dlsym(d, "drmSetMaster");
    int (*drop_master)(int) = (int (*)(int))dlsym(d, "drmDropMaster");

    printf("node %s usage=0x%x master=%d\n", node, usage, master);
    int fd = open(node, O_RDWR | O_CLOEXEC);
    printf("fd %d\n", fd);
    if (fd < 0) return 1;
    if (master && set_master) {
        int r = set_master(fd);
        printf("drmSetMaster rc=%d errno=%d\n", r, r ? errno : 0);
    }
    gbm_device *dev = create_dev(fd);
    printf("gbm device %p\n", (void *)dev);
    if (!dev) return 1;
    gbm_bo *bo = bo_create(dev, 64, 64, 0x34325241u /* AR24 */, usage);
    printf("bo %p\n", (void *)bo);
    if (!bo) return 1;
    printf("plane_count %d  gbm_bo_get_fd %d  fd_for_plane0 %d\n",
           plane_count(bo), get_fd(bo), get_fd_plane ? get_fd_plane(bo, 0) : -99);
    gbm_bo_handle_t h = get_handle(bo, 0);
    int outfd = -1;
    int rc = prime ? prime(fd, h.u32, 0, &outfd) : -99;
    printf("handle_for_plane0=0x%x drmPrimeHandleToFD rc=%d outfd=%d\n", h.u32, rc, outfd);
    if (master && drop_master) drop_master(fd);
    return 0;
}
