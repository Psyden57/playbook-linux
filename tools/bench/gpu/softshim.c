/*
 * softshim — per-process software-GL switch for WebKit helpers.
 *
 * Kumo's UI process must keep the pvr driver (it needs a DRM node path from
 * EGL, and only pvr provides one on this stack).  The WebKit helper processes
 * are the ones that fail the pvr render submit, so only they are switched to
 * software GL.  Read this from a constructor, before Mesa initialises.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__attribute__((constructor))
static void soften(void)
{
    char comm[64] = {0};
    FILE *f = fopen("/proc/self/comm", "r");
    if (!f)
        return;
    if (!fgets(comm, sizeof comm, f)) {
        fclose(f);
        return;
    }
    fclose(f);
    char *nl = strchr(comm, '\n');
    if (nl)
        *nl = '\0';

    if (strncmp(comm, "WPE", 3) == 0) {
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
        setenv("MESA_LOADER_DRIVER_OVERRIDE", "swrast", 1);
        fprintf(stderr, "[softshim] %s -> software GL (swrast)\n", comm);
        fflush(stderr);
    }
}
