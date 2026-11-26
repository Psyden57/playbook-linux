/*
 * Can the PowerVR GBM export a dmabuf by PRIME handle when gbm_bo_get_fd is -1?
 * dlopen the unit's libgbm/libdrm (no gbm headers needed); handles the
 * gbm_bo_handle union return correctly, which a ctypes probe cannot.
 *
 * Build: $MUSL_CC -O2 -o gbmprime gbmprime.c -ldl
 * Run:   ./gbmprime /dev/dri/renderD129
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>

typedef union { void *ptr; int32_t s32; uint32_t u32; int64_t s64; uint64_t u64; } gbm_bo_handle_t;
typedef struct gbm_device gbm_device;
typedef struct gbm_bo gbm_bo;

int main(int argc, char **argv)
{
    const char *node = argc > 1 ? argv[1] : "/dev/dri/renderD129";
    void *g = dlopen("libgbm.so.1", RTLD_NOW);
    void *d = dlopen("libdrm.so.2", RTLD_NOW);
    if (!g || !d) { printf("dlopen failed g=%p d=%p\n", g, d); return 2; }

    gbm_device *(*create_dev)(int) = (gbm_device *(*)(int))dlsym(g, "gbm_create_device");
    gbm_bo *(*bo_create)(gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t) =
        (gbm_bo *(*)(gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t))dlsym(g, "gbm_bo_create");
    int (*plane_count)(gbm_bo *) = (int (*)(gbm_bo *))dlsym(g, "gbm_bo_get_plane_count");
    int (*get_fd)(gbm_bo *) = (int (*)(gbm_bo *))dlsym(g, "gbm_bo_get_fd");
    gbm_bo_handle_t (*get_handle)(gbm_bo *, int) =
        (gbm_bo_handle_t (*)(gbm_bo *, int))dlsym(g, "gbm_bo_get_handle_for_plane");
    int (*prime)(int, uint32_t, uint32_t, int *) =
        (int (*)(int, uint32_t, uint32_t, int *))dlsym(d, "drmPrimeHandleToFD");

    printf("node %s\n", node);
    if (!create_dev || !bo_create || !get_handle || !prime) {
        printf("missing symbol create_dev=%p bo_create=%p get_handle=%p prime=%p\n",
               (void *)create_dev, (void *)bo_create, (void *)get_handle, (void *)prime);
        return 2;
    }
    int fd = open(node, O_RDWR | O_CLOEXEC);
    printf("fd %d\n", fd);
    if (fd < 0) return 1;
    gbm_device *dev = create_dev(fd);
    printf("gbm device %p\n", (void *)dev);
    if (!dev) return 1;
    gbm_bo *bo = bo_create(dev, 64, 64, 0x34325241u /* AR24 */, 4 /* RENDERING */);
    printf("bo %p\n", (void *)bo);
    if (!bo) return 1;
    printf("plane_count %d  gbm_bo_get_fd %d\n", plane_count ? plane_count(bo) : -99, get_fd ? get_fd(bo) : -99);
    gbm_bo_handle_t h = get_handle(bo, 0);
    printf("handle_for_plane(0) u32=0x%x\n", h.u32);
    int outfd = -1;
    int rc = prime(fd, h.u32, 0, &outfd);
    printf("drmPrimeHandleToFD rc=%d outfd=%d\n", rc, outfd);
    return 0;
}
