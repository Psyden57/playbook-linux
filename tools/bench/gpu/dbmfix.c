/* dbmfix.c — LD_PRELOAD shim fixing the PowerVR DDK's dumb-buffer pitch waste.
 *
 * ROOT CAUSE (report 271 §7): libdbm expresses a linear buffer as
 *     width = 1, height = <total bytes>, bpp = 8
 * omapdrm's omap_gem_dumb_create() calls drm_mode_size_dumb(dev, args, SZ_8, 0),
 * i.e. it aligns the SCANLINE PITCH to 8 bytes.  With width = 1 the pitch
 * becomes 8, so the allocation is 8 * height = 8x the intended size.
 * (Measured: width=1,height=2457600 -> pitch=8, size=19,660,800 for a
 *  2,457,600-byte frame.  The same bytes as width=2457600,height=1 -> 1.00x.)
 *
 * THE FIX: re-express the same byte count as width = <bytes>, height = 1, bpp = 8
 * before the kernel sees it.  Same size, no padding, nothing else changes.
 *
 * Enabled only when DBMFIX=1.  Logs every rewrite.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DRM_IOCTL_MODE_CREATE_DUMB 0xC02064B2UL

struct create_dumb {          /* drivers/gpu/drm/drm_mode.h */
    uint32_t height, width, bpp, flags, handle, pitch;
    uint64_t size;
};

typedef int (*ioctl_fn)(int, unsigned long, ...);

int ioctl(int fd, unsigned long req, ...)
{
    va_list ap;
    void *arg;
    va_start(ap, req);
    arg = va_arg(ap, void *);
    va_end(ap);

    static ioctl_fn real;
    if (!real)
        real = (ioctl_fn)dlsym(RTLD_NEXT, "ioctl");

    if (req == DRM_IOCTL_MODE_CREATE_DUMB && arg) {
        struct create_dumb v;
        memcpy(&v, arg, sizeof v);
        uint32_t in_w = v.width, in_h = v.height;

        /* Only the 1-byte-wide linear form is affected; leave anything else. */
        int rewrite = getenv("DBMFIX") && v.width == 1 && v.bpp == 8 && v.height > 8;

        if (rewrite) {
            v.width  = in_h;          /* total bytes become the scanline */
            v.height = 1;
            memcpy(arg, &v, sizeof v);
        }
        int r = real(fd, req, arg);

        if (getenv("DBMFIX_VERBOSE")) {
            struct create_dumb out;
            memcpy(&out, arg, sizeof out);
            fprintf(stderr, "dbmfix: CREATE_DUMB %ux%u -> %ux%u bpp=%u  size=%llu "
                            "pitch=%u rc=%d%s\n",
                    in_w, in_h, out.width, out.height, out.bpp,
                    (unsigned long long)out.size, out.pitch, r,
                    rewrite ? "  [pitch-fixed]" : "");
        }
        return r;
    }
    return real(fd, req, arg);
}
