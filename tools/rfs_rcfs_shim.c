/*
 * rfs_rcfs_shim.c -- replacement for /proc/boot/rfs_validator on BB10.
 *
 * WHY
 *   BB10's rfs_validator verifies a signed hash table of the RCFS (/base) and
 *   mounts it through fs-rcfs.so as a side effect.  Editing the RCFS (session
 *   25: deckard/platform/blacklist.pyc) invalidates that hash, so the
 *   verification has to be skipped.  The mount is the part the boot needs.
 *
 * WHAT IT DOES
 *   Probes the device paths the stock binary names ("/dev/emmc/os%ut179",
 *   plus the TabletOS-style hd1/hd2 for a cross-check) and mounts the first
 *   one that exists at /base as type "rcfs" (fs-rcfs.so is in the IFS and on
 *   LD_LIBRARY_PATH).  EBUSY means /base is already mounted: success.
 *   Nothing is written; the stock -c/-v arguments are accepted and ignored.
 *
 * BUILD (QNX 6.6 SDP, from the repository root)
 *   . tools/env.sh
 *   ntoarmv7-gcc -O2 -Wall -o rfs_rcfs_shim tools/rfs_rcfs_shim.c
 */

#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

static const char *devs[] = {
    "/dev/emmc/os0t179",
    "/dev/emmc/os1t179",
    "/dev/hd1t179",
    "/dev/hd2t179",
    NULL
};

int main(int argc, char **argv)
{
    int i;
    (void)argc;
    (void)argv;

    for (i = 0; devs[i]; i++) {
        struct stat st;
        if (stat(devs[i], &st) != 0)
            continue;
        if (mount(devs[i], "/base", 0, "rcfs", NULL, 0) == 0) {
            fprintf(stderr, "rfs_validator: mounted %s at /base\n", devs[i]);
            return 0;
        }
        if (errno == EBUSY)
            return 0;
        fprintf(stderr, "rfs_validator: mounting %s at /base failed (%d)\n",
                devs[i], errno);
    }
    fprintf(stderr, "rfs_validator: no RCFS device found\n");
    return 1;
}
