/*
 * pbmon — call the OMAP4430 secure monitor's RPMB block seal/unseal services.
 *
 * Why: the 224-byte RPMB record block is encrypted on media by the secure
 * monitor (report 144).  The non-secure resmgr (QNX rpmb-omap4) reaches it with
 * two smc calls, both with domain 4 and a physically-addressed command page:
 *
 *   service 0x33: {u32 2, u32 phys(key), u32 keylen}   load the 16-byte key
 *   service 0x2F: {u32 1, u32 phys(desc)}              run the transform
 *
 * The 0x2F descriptor (little-endian, zero otherwise):
 *   +0x00 u16 1          +0x02 u16 op | 0x400 (0x400 = the 0x33 preload)
 *   +0x04 u8  dir        +0x05 u8 0     (dir 1 = seal, 0 = unseal)
 *   +0x0c u32 len        +0x10 u32 0     +0x14 u32 0
 *   +0x18 u32 phys(data) +0x1c u32 len
 *   +0x20 u32 phys(out)  +0x24 u32 len
 * op is 0 on Winchester (rpmb-omap4 0x29cc), and op 0 does not take key2.
 * The key is page+0x20 = 86 78 7c 2a 63 2f be 16 48 ec ab a6 d8 84 01 09.
 *
 * The marshaller (0x22e8) also enables six CM2 L4SEC crypto clocks around the
 * call: 0x4A0095A0/A8/B0/B8/C0/C8 (AES1, AES2, DES3DES, PKA, RNG, SHA2MD5),
 * MODULEMODE=2, restored afterwards.
 *
 * Session 20 correction: RpmbInit's second PRCM write is
 * CM_MPU_STATICDEP[14] (L4SEC_STATDEP) at 0x4A004304 — the MPU_CM1 instance
 * mapped at 0x4A004300, offset 0x004 (report 107 §4/§H3, TRM Table 3-1034).
 * Session 19's driver wrote 0x4A004004, which is an *undefined* offset of
 * INTRCONN_SOCKET_CM1 (0x4A004000), so the MPU->L4SEC dependency was never
 * enabled and the L4SEC domain could idle under the crypto engines the monitor
 * reaches for.  Both QNX components that touch the monitor (rpmb-omap4's
 * RpmbInit and trustzone-omap4's TrustZoneInit) set this bit and never restore
 * it.
 *
 * The address oracle (`/dev/pboracle`, `pbrpmb oracle [n]`) runs one graded,
 * hang-safe 0x12 (nonce fill) probe per write: the nonce appearing in one of
 * three reserved DRAM pages identifies the monitor's address view even when the
 * call returns an error.  Pages (DTS reserved-memory, no-map):
 *   0xA0200000  "low"    — the identity candidate
 *   0x90200000  "shadow" — low − 0x10000000, absorbs the biased-mode write
 *   0xB0200000  "high"   — low + 0x10000000, absorbs the identity-mode write
 * Result (report 147): the monitor is identity — probe 6 passed 0xA0200000 and
 * the nonce appeared in that page.  The low page is therefore also the page
 * every transform uses; the coherent buffer at 0xbf... is out of the monitor's
 * reachable window and only serves the baseline probe.
 *
 * The selftest is read-only: it unseals the on-media bytes of block 0x43 and
 * compares with the plaintext captured from the live rpmb-omap4 page buffer,
 * then seals the plaintext and compares with the media.  A PASS proves the ABI,
 * the key, the clock enables and the physical-address convention before any
 * RPMB write is attempted.
 *
 * The whole call is a guess-free transcription of the QNX code; the only
 * unknown is whether the monitor accepts domain 4 from this OS at all.
 */
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

#define PBMON_LEN 0xE0
#define PBMON_PAGE 0x1000

/*
 * Page layout, mirroring the live QNX page (report 143's dump):
 *   +0x020 key        the 0x33 preload slot QNX keeps the key in
 *   +0x400 command pages (0x33, 0x2F, 0x31, 0x12)
 *   +0x480 descriptor
 *   +0x500 nonce buffer
 *   +0x700 input block (plaintext or ciphertext)
 *   +0x800 output block
 * The input must NOT overlap +0x020: if the monitor takes the key from the
 * fixed slot rather than the pointer, an input at +0x000 would hand it 16
 * bytes of ciphertext as the key — a ret=0 with garbage output, which is
 * exactly what the first identity-era seal test produced.
 */
#define OFF_IN   0x700
#define OFF_OUT  0x800
#define OFF_DESC 0x480
#define OFF_CMD  0x400
#define OFF_KEY  0x020

#define CM2_PHYS 0x4A009000
#define CM2_L4SEC_CLKSTCTRL 0x580
/* MPU_CM1 is the instance at 0x4A004300 (CM_MPU_CLKSTCTRL at +0x000,
 * CM_MPU_STATICDEP at +0x004); 0x4A004000 is INTRCONN_SOCKET_CM1. */
#define CM1_PHYS 0x4A004000
#define CM1_MPU_STATICDEP_OFF 0x304
#define CM_MPU_L4SEC_STATDEP (1u << 14)
#define CM2_L4SEC_AES1    0x5A0
#define CM2_L4SEC_AES2    0x5A8
#define CM2_L4SEC_DES3DES 0x5B0
#define CM2_L4SEC_PKA     0x5B8
#define CM2_L4SEC_RNG     0x5C0
#define CM2_L4SEC_SHA2MD5 0x5C8

/* address-oracle pages (DTS reserved-memory, no-map) */
#define PB_LOW_PA    0xA0200000u
#define PB_SHADOW_PA 0x90200000u
#define PB_HIGH_PA   0xB0200000u
#define PB_DATA_OFF  0x100
#define PB_ORACLE_MAX 7

/*
 * The 16-byte key the 0x33 service preloads, from the live page at +0x20
 * (report 144).  /dev/pbkey overrides it so the key/framing matrix can be run
 * in one boot; the default is QNX's value, restored by a reboot.
 */
static u8 pb_xf_key[16] = {
	0x86, 0x78, 0x7c, 0x2a, 0x63, 0x2f, 0xbe, 0x16,
	0x48, 0xec, 0xab, 0xa6, 0xd8, 0x84, 0x01, 0x09,
};

/*
 * ABI knobs for the known-pair sweep (/dev/pbctl: four bytes
 * {op, kek, desc_mode, key_mode}).
 *
 * op         descriptor op field (rpmb-omap4 accepts 0 and 2)
 * kek        which select_kek(0x31) precedes the transform
 *            (0 = none, 1 = index 0, 2 = index 1 — its read path uses 1)
 * desc_mode  addresses written into the descriptor: 0 = identity,
 *            1 = phys - 0x10000000 (what QNX puts there, measured)
 * key_mode   the 0x33 key pointer: 0 = identity, 1 = phys - 0x10000000
 *
 * The measured convention (session 20, live QNX page): the command page and
 * its arguments are TRUE physical addresses, while the descriptor's data/out
 * fields are stored 0x10000000 LOWER — the monitor adds 0x10000000 back when
 * it hands those to the crypto engine.  Passing identity addresses there made
 * the engine read the wrong buffer, which is why ret=0 came with garbage.
 */
static u8 pb_op = 0;
static u8 pb_kek = 2;
static u8 pb_desc_mode;
static u8 pb_key_mode;

/* block 0x43: plaintext from rpmb-omap4 page+0x700, ciphertext from media */
#include "pbmon-pair.h"
/* the whole captured RpmbInit page, for the key-window sweep */
#include "pbmon-page.h"

static void __iomem *cm2, *cm1;
static void *pb_buf;
static dma_addr_t pb_pa;
static void *pb_low, *pb_shadow, *pb_high;
static char pb_rep[256];
static struct device *pb_dev;
static struct platform_device *pb_pdev;
static DEFINE_MUTEX(pb_lock);
static u8 pb_out[PBMON_LEN];
static bool pb_out_valid;

static const u32 cm2_offs[6] = {
	CM2_L4SEC_AES1, CM2_L4SEC_AES2, CM2_L4SEC_DES3DES,
	CM2_L4SEC_PKA, CM2_L4SEC_RNG, CM2_L4SEC_SHA2MD5,
};
static u32 cm2_saved[6];
static u32 cm2_ctl_saved, cm1_saved;

static noinline u32 pb_smc(u32 svc, u32 x1, u32 dom, u32 arg)
{
	register u32 r0 asm("r0") = svc;
	register u32 r1 asm("r1") = x1;
	register u32 r2 asm("r2") = dom;
	register u32 r3 asm("r3") = arg;

	/*
	 * Match rpmb-omap4 0x21f0 exactly: r6 = 0xff is the "monitor call"
	 * tag, r12 (ip) = 0, ICIALLU, and the monitor clobbers r4-r12, so the
	 * veneer saves them.  r0-r3 carry the service and the return.
	 */
	asm volatile(
		"push {r4-r12, lr}\n\t"
		"mov r6, #0xff\n\t"
		"mov r12, #0\n\t"
		"mcr p15, 0, r0, c7, c5, 6\n\t"
		".word 0xf57ff04f\n\t"	/* dsb sy */
		".word 0xf57ff06f\n\t"	/* isb sy */
		".word 0xf57ff05f\n\t"	/* dmb sy */
		".word 0xe1600071\n\t"	/* smc #1 */
		"pop {r4-r12, lr}\n\t"
		: "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
		:
		: "memory");
	return r0;
}

/*
 * RpmbInit's 0x2218, once, before any crypto-engine use:
 *   CM_L4SEC_CLKSTCTRL (0x4A009580) |= 3          (CLKTRCTRL = HW_AUTO)
 *   CM_MPU_STATICDEP  (0x4A004304) |= 0x4000      (L4SEC_STATDEP = 1)
 * The six module clocks alone are not enough: without the MPU->L4SEC static
 * dependency the L4SEC domain can go idle under the engines the monitor uses.
 * QNX never restores either register, so neither do we.
 */
static void pb_domain_on(void)
{
	u32 v;

	cm2_ctl_saved = readl(cm2 + CM2_L4SEC_CLKSTCTRL);
	writel(cm2_ctl_saved | 3u, cm2 + CM2_L4SEC_CLKSTCTRL);

	cm1_saved = readl(cm1 + CM1_MPU_STATICDEP_OFF);
	writel(cm1_saved | CM_MPU_L4SEC_STATDEP,
	       cm1 + CM1_MPU_STATICDEP_OFF);

	v = readl(cm1 + CM1_MPU_STATICDEP_OFF);
	pr_err("pbmon: CM_L4SEC_CLKSTCTRL 0x%08x CM_MPU_STATICDEP 0x%08x (was 0x%08x)\n",
	       readl(cm2 + CM2_L4SEC_CLKSTCTRL), v, cm1_saved);
}

static void pb_clocks_on(void)
{
	int i;

	for (i = 0; i < 6; i++) {
		cm2_saved[i] = readl(cm2 + cm2_offs[i]);
		/* RNG uses MODULEMODE 1, the others 2 (QNX 0x2384-0x240c) */
		writel((cm2_saved[i] & ~3u) | (i == 4 ? 1u : 2u),
		       cm2 + cm2_offs[i]);
	}
	/* let the crypto modules come up */
	udelay(100);
}

static void pb_clocks_off(void)
{
	int i;

	for (i = 0; i < 6; i++)
		writel(cm2_saved[i] & ~3u, cm2 + cm2_offs[i]);
}

/*
 * Session 19 believed the monitor addressed DRAM 0x10000000 below the
 * non-secure physical address, from QNX's live 0x2F descriptor (data
 * 0xafb2b700 for a page PTINFO reports at 0xbfb2b000).  Report 147 disproved
 * it: that offset is QNX's own (QNX's paddr_t space puts DRAM at 0x90000000),
 * and the monitor reads identity addresses.  The helper survives only to
 * reproduce the old failing call as the oracle's baseline probe 1.
 */
#define PB_DRAM_BIAS 0x10000000u

static inline u32 pb_mon_pa(u32 pa)
{
	return pa >= 0x80000000u ? pa - PB_DRAM_BIAS : pa;
}

/*
 * Descriptor data/out addresses.  QNX stores them as phys - 0x10000000 (the
 * monitor's engine view) while the command page and its arguments are true
 * physical addresses; pb_desc_mode selects which form we write.
 */
static u32 pb_eng_addr(u32 pa)
{
	return pb_desc_mode ? pb_mon_pa(pa) : pa;
}

/*
 * The 0x12 address oracle (report 146 "what to try next", report 145 §4.3).
 *
 * 0x12 fills a 16-byte buffer with a fresh nonce.  The buffer's address is the
 * only thing we vary, and the three reserved pages let us see where the monitor
 * actually wrote even when the service returns an error, so the compare is
 * "did my memory change", not "did the call succeed":
 *
 *   identity mode: pass the true physical address (A -> NS A)
 *   biased mode:   pass A - 0x10000000  (A -> NS A + 0x10000000)
 *
 * Both modes are hang-safe by construction: every address the monitor can
 * resolve from the three candidates is a reserved, no-map page, so a stray
 * 16-byte write lands in our own scratch and never in kernel memory.
 */
static void pb_scrub(void)
{
	if (pb_low)
		memset(pb_low, 0xaa, PBMON_PAGE);
	if (pb_shadow)
		memset(pb_shadow, 0xaa, PBMON_PAGE);
	if (pb_high)
		memset(pb_high, 0xaa, PBMON_PAGE);
	memset(pb_buf + 0x500, 0xaa, 16);
	memset(pb_buf + OFF_CMD, 0, 0x40);
}

static const char *pb_hit(void)
{
	if (pb_low && memchr_inv(pb_low + PB_DATA_OFF, 0xaa, 16))
		return "low";
	if (pb_shadow && memchr_inv(pb_shadow + PB_DATA_OFF, 0xaa, 16))
		return "shadow";
	if (pb_high && memchr_inv(pb_high + PB_DATA_OFF, 0xaa, 16))
		return "high";
	if (memchr_inv(pb_buf + 0x500, 0xaa, 16))
		return "coherent";
	return "none";
}

static u32 pb_oracle_call(u32 svc, u32 dom, void *page, u32 page_pa,
			  u32 count, u32 a1, u32 a2, bool biased)
{
	u32 *p = page;
	u32 r;

	memset(p, 0, 0x40);
	p[0] = count;
	p[1] = a1;
	p[2] = a2;
	mb();
	pb_clocks_on();
	mb();
	r = pb_smc(svc, 0, dom, biased ? pb_mon_pa(page_pa) : page_pa);
	mb();
	pb_clocks_off();
	return r;
}

static int pb_oracle(unsigned n)
{
	void *page = pb_buf + OFF_CMD;
	u32 page_pa = (u32)pb_pa + OFF_CMD;
	u32 dpa = (u32)pb_pa + 0x500;
	u32 svc, dom = 4, count, a1 = 0, a2 = 0, ret;
	bool biased = true;
	const char *verdict;

	switch (n) {
	case 1:		/* baseline: the session-19 call, unchanged */
		svc = 0x12; count = 2; a1 = pb_mon_pa(dpa); a2 = 16;
		break;
	case 2:		/* zero-argument service: is the monitor reachable? */
		svc = 0x3A; count = 0;
		break;
	case 3:		/* the exact bootloader 0x31 form, domain 4 (QNX) */
		svc = 0x31; count = 1;
		break;
	case 4:		/* 0x31 with the bootloader's domain 5 */
		svc = 0x31; count = 1; dom = 5;
		break;
	case 5:		/* 0x12 with the bootloader's domain 0 */
		svc = 0x12; count = 2; dom = 0; a1 = pb_mon_pa(dpa); a2 = 16;
		break;
	case 6:		/* low page, identity address */
		page = pb_low;
		page_pa = PB_LOW_PA;
		dpa = PB_LOW_PA + PB_DATA_OFF;
		biased = false;
		svc = 0x12; count = 2; a1 = dpa; a2 = 16;
		break;
	default:	/* 7: low page, biased address */
		page = pb_low;
		page_pa = PB_LOW_PA;
		dpa = PB_LOW_PA + PB_DATA_OFF;
		biased = true;
		svc = 0x12; count = 2; a1 = pb_mon_pa(dpa); a2 = 16;
		break;
	}

	pb_scrub();
	pr_err("pbmon: oracle %u start svc=0x%x dom=%u page=0x%08x %s\n",
	       n, svc, dom, biased ? pb_mon_pa(page_pa) : page_pa,
	       biased ? "biased" : "identity");
	ret = pb_oracle_call(svc, dom, page, page_pa, count, a1, a2, biased);

	verdict = (ret == 0 && strcmp(pb_hit(), "none")) ? "OK" : "FAIL";
	scnprintf(pb_rep, sizeof(pb_rep),
		  "oracle %u: svc=0x%x dom=%u r3=0x%08x ret=%u w=%s %s\n",
		  n, svc, dom, biased ? pb_mon_pa(page_pa) : page_pa, ret,
		  pb_hit(), verdict);
	pr_err("pbmon: %s", pb_rep);
	return ret == 0 ? 0 : -EIO;
}

/*
 * The transform path.  Everything the monitor touches lives in the reserved
 * low page at PB_LOW_PA and is addressed by its true physical address: report
 * 147 pinned the monitor's view as identity, with a reachable window that
 * excludes 0xBF040000 (where a coherent allocation lands) and includes
 * 0xA0200000.  The session-19 bias is gone.
 */
static int pb_xform(const u8 *in, u8 *out, int dir)
{
	const u32 base = PB_LOW_PA;
	u32 *desc = (u32 *)(pb_low + OFF_DESC);
	u32 *cmd33 = (u32 *)(pb_low + OFF_CMD);
	u32 *cmd2f = (u32 *)(pb_low + OFF_CMD + 0x10);
	u32 *cmd31 = (u32 *)(pb_low + OFF_CMD + 0x20);
	u32 *cmd12 = (u32 *)(pb_low + OFF_CMD + 0x30);
	u32 r12, r31, r33, r2f;

	memcpy(pb_low + OFF_IN, in, PBMON_LEN);
	memcpy(pb_low + OFF_KEY, pb_xf_key, sizeof(pb_xf_key));

	memset(desc, 0, 0x160);
	((u16 *)desc)[0] = 1;
	((u16 *)desc)[1] = pb_op | 0x0400;	/* op | key33 preload */
	((u8 *)desc)[4] = (u8)dir;
	((u8 *)desc)[5] = 0;
	desc[3] = PBMON_LEN;			/* +0x0c */
	desc[4] = 0;				/* +0x10 */
	desc[5] = 0;				/* +0x14 */
	desc[6] = pb_eng_addr(base + OFF_IN);	/* +0x18 data */
	desc[7] = PBMON_LEN;			/* +0x1c */
	desc[8] = pb_eng_addr(base + OFF_OUT);	/* +0x20 out */
	desc[9] = PBMON_LEN;			/* +0x24 */

	cmd33[0] = 2;
	cmd33[1] = pb_key_mode ? pb_mon_pa(base + OFF_KEY) : base + OFF_KEY;
	cmd33[2] = sizeof(pb_xf_key);
	cmd2f[0] = 1;
	cmd2f[1] = base + OFF_DESC;
	cmd31[0] = 1;
	cmd31[1] = 1;				/* 0x31 index 1 */
	cmd12[0] = 2;
	cmd12[1] = base + 0x500;
	cmd12[2] = 16;
	memset(pb_low + 0x500, 0xaa, 16);

	mb();
	pb_clocks_on();
	mb();
	r12 = pb_smc(0x12, 0, 4, base + OFF_CMD + 0x30);
	mb();
	if (pb_kek) {
		cmd31[1] = pb_kek - 1;		/* 1 = index 0, 2 = index 1 */
		mb();
		r31 = pb_smc(0x31, 0, 4, base + OFF_CMD + 0x20);
		mb();
	} else {
		r31 = 0;
	}
	r33 = pb_smc(0x33, 0, 4, base + OFF_CMD);
	mb();
	if (r33 == 0) {
		r2f = pb_smc(0x2F, 0, 4, base + OFF_CMD + 0x10);
		mb();
	} else {
		r2f = 0xffffffff;
	}
	/* QNX's block path closes with select_kek(0) (rpmb-omap4 0x2bd0) */
	if (pb_kek) {
		cmd31[1] = 0;
		mb();
		pb_smc(0x31, 0, 4, base + OFF_CMD + 0x20);
		mb();
	}
	pb_clocks_off();

	pr_err("pbmon: xform dir=%d op=%u kek=%u page=0x%08x 0x12=%u 0x31=%u 0x33=%u 0x2F=%u nonce=%*phN\n",
	       dir, pb_op, pb_kek, base, r12, r31, r33, r2f, 16,
	       (u8 *)(pb_low + 0x500));
	if (r33 != 0 || r2f != 0)
		return -EIO;
	memcpy(out, pb_low + OFF_OUT, PBMON_LEN);
	return 0;
}

static int __init pb_selftest(void)
{
	u8 out[PBMON_LEN];
	int u = -1, s = -1;

	u = pb_xform(pb_c67, out, 0);
	pr_info("pbmon: unseal(C67) -> %s\n",
		u ? "smc error" : (memcmp(out, pb_p67, PBMON_LEN) ?
				  "MISMATCH" : "PASS"));
	if (u == 0 && memcmp(out, pb_p67, PBMON_LEN))
		print_hex_dump(KERN_INFO, "pbmon: unsealed ", DUMP_PREFIX_NONE,
			       16, 1, out, 32, false);

	s = pb_xform(pb_p67, out, 1);
	pr_info("pbmon: seal(P67)   -> %s\n",
		s ? "smc error" : (memcmp(out, pb_c67, PBMON_LEN) ?
				  "MISMATCH" : "PASS"));
	if (s == 0 && memcmp(out, pb_c67, PBMON_LEN))
		print_hex_dump(KERN_INFO, "pbmon: sealed ", DUMP_PREFIX_NONE,
			       16, 1, out, 32, false);

	return (u == 0 && s == 0 && !memcmp(out, pb_c67, PBMON_LEN)) ? 0 : -EIO;
}

static int pb_xform_user(const char __user *ubuf, size_t n, int dir)
{
	u8 in[PBMON_LEN];
	int ret;

	if (n != PBMON_LEN)
		return -EINVAL;
	if (copy_from_user(in, ubuf, PBMON_LEN))
		return -EFAULT;
	mutex_lock(&pb_lock);
	ret = pb_xform(in, pb_out, dir);
	if (ret == 0)
		pb_out_valid = true;
	mutex_unlock(&pb_lock);
	return ret ? ret : (int)n;
}

static ssize_t pbseal_write(struct file *f, const char __user *u, size_t n,
			    loff_t *o)
{
	return pb_xform_user(u, n, 1);
}

static ssize_t pbunseal_write(struct file *f, const char __user *u, size_t n,
			      loff_t *o)
{
	return pb_xform_user(u, n, 0);
}

static ssize_t pb_read(struct file *f, char __user *u, size_t n, loff_t *o)
{
	int ret;

	if (*o != 0)
		return 0;
	mutex_lock(&pb_lock);
	if (!pb_out_valid) {
		mutex_unlock(&pb_lock);
		return -EAGAIN;
	}
	if (n > PBMON_LEN)
		n = PBMON_LEN;
	ret = copy_to_user(u, pb_out, n) ? -EFAULT : (int)n;
	mutex_unlock(&pb_lock);
	return ret;
}

static const struct file_operations pbseal_fops = {
	.owner = THIS_MODULE, .write = pbseal_write, .read = pb_read,
};
static const struct file_operations pbunseal_fops = {
	.owner = THIS_MODULE, .write = pbunseal_write, .read = pb_read,
};

static struct miscdevice pbseal_misc = {
	.minor = MISC_DYNAMIC_MINOR, .name = "pbseal", .fops = &pbseal_fops,
};
static struct miscdevice pbunseal_misc = {
	.minor = MISC_DYNAMIC_MINOR, .name = "pbunseal", .fops = &pbunseal_fops,
};

/*
 * /dev/pbkey: read or replace the transform key (16 bytes).  Test-only knob
 * for the key/framing matrix; a reboot restores QNX's value.
 */
static ssize_t pbkey_write(struct file *f, const char __user *u, size_t n,
			   loff_t *o)
{
	if (n != sizeof(pb_xf_key))
		return -EINVAL;
	mutex_lock(&pb_lock);
	if (copy_from_user(pb_xf_key, u, n)) {
		mutex_unlock(&pb_lock);
		return -EFAULT;
	}
	mutex_unlock(&pb_lock);
	return n;
}

static ssize_t pbkey_read(struct file *f, char __user *u, size_t n, loff_t *o)
{
	if (*o != 0)
		return 0;
	if (n > sizeof(pb_xf_key))
		n = sizeof(pb_xf_key);
	if (copy_to_user(u, pb_xf_key, n))
		return -EFAULT;
	*o += n;
	return n;
}

static const struct file_operations pbkey_fops = {
	.owner = THIS_MODULE, .write = pbkey_write, .read = pbkey_read,
};
static struct miscdevice pbkey_misc = {
	.minor = MISC_DYNAMIC_MINOR, .name = "pbkey", .fops = &pbkey_fops,
};

/* /dev/pbctl: four bytes {op, kek, desc_mode, key_mode} for the ABI sweep */
static ssize_t pbctl_write(struct file *f, const char __user *u, size_t n,
			   loff_t *o)
{
	u8 v[4];

	if (n != sizeof(v))
		return -EINVAL;
	if (copy_from_user(v, u, n))
		return -EFAULT;
	mutex_lock(&pb_lock);
	pb_op = v[0];
	pb_kek = v[1];
	pb_desc_mode = v[2];
	pb_key_mode = v[3];
	mutex_unlock(&pb_lock);
	return n;
}

static ssize_t pbctl_read(struct file *f, char __user *u, size_t n, loff_t *o)
{
	u8 v[4] = { pb_op, pb_kek, pb_desc_mode, pb_key_mode };

	if (*o != 0)
		return 0;
	if (n > sizeof(v))
		n = sizeof(v);
	if (copy_to_user(u, v, n))
		return -EFAULT;
	*o += n;
	return n;
}

static const struct file_operations pbctl_fops = {
	.owner = THIS_MODULE, .write = pbctl_write, .read = pbctl_read,
};
static struct miscdevice pbctl_misc = {
	.minor = MISC_DYNAMIC_MINOR, .name = "pbctl", .fops = &pbctl_fops,
};

/*
 * The known-pair test: unseal the captured media bytes of block 0x43 and
 * compare with the plaintext captured from QNX's page buffer, then seal the
 * plaintext and compare with the media.  This is the gate from report 144 §5:
 * if the seal reproduces the device's bytes, the ABI, the key and the framing
 * are all right.  The /dev/pbkey knob makes it a key candidate sweep.
 */
static int pb_pair(char *rep, size_t sz)
{
	u8 out[PBMON_LEN];
	int u, s, uok, sok;

	u = pb_xform(pb_c67, out, 0);
	uok = (u == 0 && !memcmp(out, pb_p67, PBMON_LEN));
	s = pb_xform(pb_p67, out, 1);
	sok = (s == 0 && !memcmp(out, pb_c67, PBMON_LEN));
	scnprintf(rep, sz, "pair: unseal=%d/%s seal=%d/%s\n",
		  u, uok ? "OK" : "DIFF", s, sok ? "OK" : "DIFF");
	return (uok && sok) ? 0 : 1;
}

/*
 * /dev/pbwsweep: try every 16-byte window of the captured QNX page as the 0x33
 * key and ask whether any of them unseals the known ciphertext into the known
 * plaintext.  That is the "is the transform key anywhere in the page" question,
 * answered in one boot; both descriptor address conventions are tried.
 */
static int pb_wsweep(char *rep, size_t sz)
{
	u8 out[PBMON_LEN], key_saved[16];
	u8 dm_saved = pb_desc_mode;
	int i, mode, hits = 0, ok = 0;
	size_t n = 0;

	memcpy(key_saved, pb_xf_key, sizeof(key_saved));
	for (mode = 0; mode < 2 && !hits; mode++) {
		pb_desc_mode = (u8)mode;
		for (i = 0; i + 16 <= (int)sizeof(pb_page); i += 16) {
			memcpy(pb_xf_key, pb_page + i, 16);
			if (pb_xform(pb_c67, out, 0))
				continue;
			ok++;
			if (!memcmp(out, pb_p67, PBMON_LEN)) {
				n += scnprintf(rep + n, sz - n,
					       "wsweep: HIT desc=%d off=0x%x unseal=OK\n",
					       mode, i);
				hits++;
				break;
			}
		}
	}
	memcpy(pb_xf_key, key_saved, sizeof(key_saved));
	pb_desc_mode = dm_saved;
	if (!hits)
		n += scnprintf(rep + n, sz - n,
			       "wsweep: no window matched (%d transforms ok)\n",
			       ok);
	return hits ? 0 : 1;
}

static ssize_t pbwsweep_read(struct file *f, char __user *u, size_t n, loff_t *o)
{
	size_t len;

	if (*o != 0)
		return 0;
	mutex_lock(&pb_lock);
	pb_wsweep(pb_rep, sizeof(pb_rep));
	mutex_unlock(&pb_lock);
	len = strlen(pb_rep);
	if (len > n)
		len = n;
	if (len && copy_to_user(u, pb_rep, len))
		return -EFAULT;
	*o += len;
	return len;
}

static const struct file_operations pbwsweep_fops = {
	.owner = THIS_MODULE, .read = pbwsweep_read,
};
static struct miscdevice pbwsweep_misc = {
	.minor = MISC_DYNAMIC_MINOR, .name = "pbwsweep", .fops = &pbwsweep_fops,
};

static ssize_t pbpair_read(struct file *f, char __user *u, size_t n, loff_t *o)
{
	size_t len;

	if (*o != 0)
		return 0;
	mutex_lock(&pb_lock);
	pb_pair(pb_rep, sizeof(pb_rep));
	mutex_unlock(&pb_lock);
	len = strlen(pb_rep);
	if (len > n)
		len = n;
	if (len && copy_to_user(u, pb_rep, len))
		return -EFAULT;
	*o += len;
	return len;
}

static const struct file_operations pbpair_fops = {
	.owner = THIS_MODULE, .read = pbpair_read,
};
static struct miscdevice pbpair_misc = {
	.minor = MISC_DYNAMIC_MINOR, .name = "pbpair", .fops = &pbpair_fops,
};

/*
 * /dev/pboracle: write a probe number ("1".."7") to run exactly one probe and
 * read back its one-line report.  One probe per write keeps a hang from taking
 * the rest of the sequence with it, and the report is in printk before the smc
 * as well, so the fbcon keeps the last words.
 */
static size_t pb_rep_len;

static ssize_t pboracle_write(struct file *f, const char __user *u, size_t n,
			      loff_t *o)
{
	char buf[8];
	unsigned id;

	if (n == 0 || n >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, u, n))
		return -EFAULT;
	buf[n] = '\0';
	if (kstrtouint(buf, 0, &id) || id < 1 || id > PB_ORACLE_MAX)
		return -EINVAL;

	/* a failed probe is a result, not an I/O error: it is in the report */
	mutex_lock(&pb_lock);
	pb_oracle(id);
	pb_rep_len = strlen(pb_rep);
	mutex_unlock(&pb_lock);
	return n;
}

static ssize_t pboracle_read(struct file *f, char __user *u, size_t n,
			     loff_t *o)
{
	size_t len;

	if (*o < 0)
		return -EINVAL;
	len = pb_rep_len > (size_t)*o ? pb_rep_len - (size_t)*o : 0;
	if (len > n)
		len = n;
	if (len && copy_to_user(u, pb_rep + *o, len))
		return -EFAULT;
	*o += len;
	return len;
}

static const struct file_operations pboracle_fops = {
	.owner = THIS_MODULE, .write = pboracle_write, .read = pboracle_read,
};
static struct miscdevice pboracle_misc = {
	.minor = MISC_DYNAMIC_MINOR, .name = "pboracle", .fops = &pboracle_fops,
};

static int pbmon_selftest = 0;
module_param_named(selftest, pbmon_selftest, int, 0644);
MODULE_PARM_DESC(selftest, "run the block-0x43 seal selftest at probe (default 0)");

static int pbmon_probe(struct platform_device *pdev)
{
	int ret;

	pb_dev = &pdev->dev;
	dma_set_mask_and_coherent(pb_dev, DMA_BIT_MASK(32));
	pb_buf = dma_alloc_coherent(pb_dev, PBMON_PAGE, &pb_pa, GFP_KERNEL);
	if (!pb_buf)
		return -ENOMEM;
	cm2 = ioremap(CM2_PHYS, 0x1000);
	cm1 = ioremap(CM1_PHYS, 0x1000);
	if (!cm2 || !cm1)
		return -ENOMEM;
	/* the three reserved oracle pages are no-map: they have no linear alias */
	pb_low = ioremap(PB_LOW_PA, PBMON_PAGE);
	pb_shadow = ioremap(PB_SHADOW_PA, PBMON_PAGE);
	pb_high = ioremap(PB_HIGH_PA, PBMON_PAGE);
	if (!pb_low || !pb_shadow || !pb_high)
		return -ENOMEM;
	pr_err("pbmon: buffers at phys 0x%llx (monitor 0x%08x), CM2 at 0x%08x\n",
		(unsigned long long)pb_pa, pb_mon_pa((u32)pb_pa), CM2_PHYS);
	pb_domain_on();

	/*
	 * The smc may hang the non-secure world if the monitor rejects it.
	 * Never run it automatically: userspace drives the test through the
	 * misc nodes, so a hang costs one reboot of this image (not a flash).
	 */
	if (pbmon_selftest)
		pb_selftest();

	ret = misc_register(&pbseal_misc);
	if (ret)
		return ret;
	ret = misc_register(&pbunseal_misc);
	if (ret)
		return ret;
	ret = misc_register(&pbkey_misc);
	if (ret)
		return ret;
	ret = misc_register(&pbctl_misc);
	if (ret)
		return ret;
	ret = misc_register(&pbpair_misc);
	if (ret)
		return ret;
	ret = misc_register(&pbwsweep_misc);
	if (ret)
		return ret;
	return misc_register(&pboracle_misc);
}

static struct platform_driver pbmon_driver = {
	.probe = pbmon_probe,
	.driver = { .name = "pbmon" },
};

static int __init pbmon_init(void)
{
	int ret;

	ret = platform_driver_register(&pbmon_driver);
	if (ret)
		return ret;
	pb_pdev = platform_device_register_simple("pbmon", -1, NULL, 0);
	return IS_ERR(pb_pdev) ? PTR_ERR(pb_pdev) : 0;
}
device_initcall(pbmon_init);
