/*
 * pbhello -- the "modules loadable" proof for the region-1 Linux root.
 *
 * Nothing in the boot path needs it: it exists so `insmod pbhello.ko` (and
 * `modprobe pbhello`) exercises the module loader, vermagic and modversions
 * against the image on media.  It imports only core symbols.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static int __init pbhello_init(void)
{
	pr_info("pbhello: loaded from the region-1 root\n");
	return 0;
}

static void __exit pbhello_exit(void)
{
	pr_info("pbhello: unloaded\n");
}

module_init(pbhello_init);
module_exit(pbhello_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PlayBook region-1 root module-load proof");
