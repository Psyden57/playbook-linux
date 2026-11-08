/*
 * qnx-hwi — dump the QNX syspage hwinfo tree (read-only).
 *
 * The WL1283 driver (devnp-ti1283.so) carries its own hwi_find(): it looks up
 * the "WLAN" item and reads the WLAN_EN_GPIO / WLAN_INT_GPIO tags, falling
 * back to 54/53 when the node is absent.  QNX libc exposes only the low-level
 * walk, so this tool implements the same by-name search and prints every tag.
 *
 * Build:  ntoarmv7-gcc -std=gnu99 -O2 -Wall -o /tmp/qnx-hwi tools/qnx-hwi.c
 * Run:    /tmp/qnx-hwi [name|all]
 *
 * Read-only: it only reads the syspage hwinfo section through libc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <hw/sysinfo.h>

static const char *sname(unsigned off)
{
    const char *s = __hwi_find_string(off);
    return s ? s : "?";
}

static unsigned item_next(unsigned off)
{
    unsigned n = hwi_next_item(off);

    if (n == HWI_NULL_OFF || n == off)
        return HWI_NULL_OFF;
    return n;
}

/* First item whose name matches, case-insensitively; unit is the ordinal. */
static unsigned find_item(const char *name, int unit)
{
    unsigned off = 0;
    int seen = 0;

    for (int i = 0; i < 512; i++) {
        hwi_tag *tag = hwi_off2tag(off);
        struct hwi_item *item;
        const char *nm;

        if (!tag)
            break;
        item = (struct hwi_item *)tag;
        nm = sname(item->itemname);
        if (strcasecmp(nm, name) == 0) {
            if (seen == unit)
                return off;
            seen++;
        }
        off = item_next(off);
        if (off == HWI_NULL_OFF)
            break;
    }
    return HWI_NULL_OFF;
}

static void dump_item(unsigned off)
{
    hwi_tag *tag = hwi_off2tag(off);
    struct hwi_item *item;

    if (!tag) {
        printf("  (null tag at 0x%x)\n", off);
        return;
    }
    item = (struct hwi_item *)tag;
    printf("item off=0x%x name=%s itemsize=%u owner=0x%x kids=0x%x\n",
           off, sname(item->itemname), item->itemsize,
           item->owner, item->kids);

    unsigned t = off;
    for (int i = 0; i < 128; i++) {
        hwi_tag *p = hwi_off2tag(t);
        unsigned words;
        const char *nm;

        if (!p)
            break;
        words = p->prefix.size;      /* size is in 4-byte words */
        nm = sname(p->prefix.name);
        printf("  [%02d] off=0x%03x words=%u tag=%s", i, t, words, nm);
        if (words) {
            const uint32_t *w = (const uint32_t *)p;
            unsigned n = words;

            if (n > 16)
                n = 16;
            printf(" raw=");
            for (unsigned j = 0; j < n; j++)
                printf("%s%08x", j ? " " : "", w[j]);
        }
        if (strcmp(nm, "optstr") == 0 && words >= 2) {
            const uint32_t *w = (const uint32_t *)p;

            printf(" optstr=\"%s\"", sname(w[1]));
        }
        if (strcmp(nm, "location") == 0 && words >= 6) {
            struct hwi_location *loc = (struct hwi_location *)p;

            printf(" base=0x%llx len=0x%x regshift=%u addrspace=%u",
                   (unsigned long long)loc->base, loc->len,
                   loc->regshift, loc->addrspace);
        }
        if (strcmp(nm, "Device") == 0 && words >= 4) {
            struct hwi_device *dev = (struct hwi_device *)p;

            printf(" pnpid=0x%x", dev->pnpid);
        }
        printf("\n");
        t = hwi_next_tag(t, 1);
        if (t == HWI_NULL_OFF)
            break;
    }
}

static void dump_all_items(void)
{
    unsigned off = 0;

    printf("=== all hwinfo items ===\n");
    for (int i = 0; i < 512; i++) {
        hwi_tag *tag = hwi_off2tag(off);
        struct hwi_item *item;

        if (!tag)
            break;
        item = (struct hwi_item *)tag;
        printf("ITEM off=0x%04x name=%s itemsize=%u owner=0x%x kids=0x%x\n",
               off, sname(item->itemname), item->itemsize,
               item->owner, item->kids);
        off = item_next(off);
        if (off == HWI_NULL_OFF)
            break;
    }
}

int main(int argc, char **argv)
{
    static const char *names[] = {
        "WLAN", "wlan", "WL1283", "wl1283", "ti1283",
        "sdio", "mmcsd", "MMC_SD5", "HSMMC5", NULL
    };

    if (argc > 1 && strcmp(argv[1], "all") == 0) {
        dump_all_items();
        return 0;
    }

    for (int i = 0; names[i]; i++) {
        unsigned off = find_item(names[i], 0);

        printf("find(\"%s\", 0) -> 0x%x\n", names[i], off);
        if (off != HWI_NULL_OFF)
            dump_item(off);
    }

    if (argc > 1) {
        unsigned off = find_item(argv[1], 0);

        printf("find(\"%s\", 0) -> 0x%x\n", argv[1], off);
        if (off != HWI_NULL_OFF)
            dump_item(off);
    }
    return 0;
}
