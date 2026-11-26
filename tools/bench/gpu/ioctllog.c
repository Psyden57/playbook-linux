/* LD_PRELOAD ioctl logger: print DRM ('d'=0x64 group) ioctl requests.
 * Tells CREATE_DUMB (0xc02064b2, dumb fallback) from the DRI-image path.
 * musl declares ioctl(int, int, ...) — match it. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <sys/ioctl.h>

typedef int (*ioctl_fn)(int, int, ...);

int ioctl(int fd, int req, ...)
{
    static ioctl_fn real;
    if (!real)
        real = (ioctl_fn)dlsym(RTLD_NEXT, "ioctl");
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    unsigned int r = (unsigned int)req;
    if ((r & 0xff00U) == 0x6400U) { /* DRM ioctl group 'd' */
        const char *name = "DRM?";
        if (r == 0xc02064b2U) name = "CREATE_DUMB";
        else if (r == 0xc00c642dU) name = "PRIME_HANDLE_TO_FD";
        else if (r == 0xc00c642eU) name = "PRIME_FD_TO_HANDLE";
        else if (r == 0x4008642aU) name = "GEM_CLOSE";
        else if (r == 0xc00c6422U) name = "GEM_FLINK";
        fprintf(stderr, "[ioctllog] fd=%d req=0x%x %s\n", fd, r, name);
        fflush(stderr);
    }
    return real(fd, req, arg);
}
