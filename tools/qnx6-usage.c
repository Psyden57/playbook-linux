/*
 * qnx6-usage — read-only allocation map of a QNX6 filesystem.
 *
 * Built for the PlayBook bring-up (session 16/17) to answer one question
 * before shrinking the 58 GB TabletOS root: does every live block live below
 * the proposed new filesystem size?  It parses the superblock (MMI and plain
 * layouts), walks the block bitmap and every live inode's pointer tree, and
 * prints the high-water mark.  It never writes to the device.
 *
 * Build (QNX 6.6 toolchain, see docs/project/SETUP.md §2):
 *   QNX_HOST=$Q/host/linux/x86 QNX_TARGET=$Q/target/qnx6 \
 *     $Q/host/linux/x86/usr/bin/arm-unknown-nto-qnx6.6.0eabi-gcc \
 *     -O2 -Wall -o tools/qnx6-usage tools/qnx6-usage.c
 *
 * Usage: /tmp/qnx6-usage [-v] <device-or-image> [blocksize]
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QNX6_MAGIC	0x68191122u
#define QNX6_SB_SIZE	0x200
#define QNX6_SB_AREA	0x1000
#define QNX6_BOOTBLOCK	0x2000
#define QNX6_INODE_SIZE	0x80
#define NPTR		16

static int fd;
static int be;			/* big-endian fs */
static unsigned bs;		/* fs block size */
static unsigned blks_off;	/* SUPERBLOCK_AREA / bs */
static unsigned ptrbits;
static uint32_t num_blocks;
static uint64_t high_bitmap;	/* highest set bit, in fs block numbers */

static uint32_t rd32(const unsigned char *p)
{
	uint32_t v;
	memcpy(&v, p, 4);
	return be ? __builtin_bswap32(v) : v;
}

static uint64_t rd64(const unsigned char *p)
{
	uint64_t v;
	memcpy(&v, p, 8);
	return be ? __builtin_bswap64(v) : v;
}

static uint16_t rd16(const unsigned char *p)
{
	uint16_t v;
	memcpy(&v, p, 2);
	return be ? (uint16_t)((v >> 8) | (v << 8)) : v;
}

static int read_at(unsigned long long off, void *buf, size_t len)
{
	ssize_t n;
	unsigned char *p = buf;

	while (len) {
		n = pread(fd, p, len, (off_t)off);
		if (n <= 0)
			return -1;
		p += n;
		off += n;
		len -= n;
	}
	return 0;
}

struct rootnode {
	uint64_t size;
	uint32_t ptr[NPTR];
	unsigned levels;
};

static void parse_rootnode(const unsigned char *p, struct rootnode *r)
{
	int i;

	r->size = rd64(p);
	for (i = 0; i < NPTR; i++)
		r->ptr[i] = rd32(p + 8 + 4 * i);
	r->levels = p[8 + 4 * NPTR];
}

static unsigned devblock(uint32_t b)
{
	return b + blks_off;
}

/*
 * Map logical block `no` of a rootnode file to a device block, following the
 * same scheme as fs/qnx6/inode.c:qnx6_block_map().  Returns 0 on a hole.
 */
static unsigned map_block(const struct rootnode *r, unsigned long no)
{
	unsigned block;
	unsigned char *buf;
	uint32_t ptr;
	int levelptr, i, bitdelta, mask;

	if (!r->size)
		return 0;
	bitdelta = ptrbits * r->levels;
	levelptr = no >> bitdelta;
	if (levelptr >= NPTR)
		return 0;
	if (r->ptr[levelptr] == 0xffffffffu)
		return 0;
	block = devblock(r->ptr[levelptr]);
	buf = malloc(bs);
	if (!buf)
		return 0;
	mask = (1 << ptrbits) - 1;
	for (i = 0; i < (int)r->levels; i++) {
		if (read_at((unsigned long long)block * bs, buf, bs)) {
			free(buf);
			return 0;
		}
		bitdelta -= ptrbits;
		levelptr = (no >> bitdelta) & mask;
		ptr = rd32(buf + 4 * levelptr);
		if (ptr == 0xffffffffu) {
			free(buf);
			return 0;
		}
		block = devblock(ptr);
	}
	free(buf);
	return block;
}

static int valid_block(uint32_t b)
{
	return b != 0xffffffffu && b != 0 &&
	       (unsigned long long)devblock(b) <
	       (unsigned long long)num_blocks + blks_off;
}

/* Walk every pointer block of a file, tracking the highest device block. */
static void walk_tree(uint32_t blk, int level, unsigned *maxblk,
		      unsigned long *count)
{
	unsigned char *buf = malloc(bs);
	unsigned i;

	if (!buf)
		return;
	if (read_at((unsigned long long)devblock(blk) * bs, buf, bs)) {
		free(buf);
		return;
	}
	for (i = 0; i < bs / 4; i++) {
		uint32_t p = rd32(buf + 4 * i);

		if (!valid_block(p))
			continue;
		if (devblock(p) > *maxblk)
			*maxblk = devblock(p);
		(*count)++;
		if (level > 1)
			walk_tree(p, level - 1, maxblk, count);
	}
	free(buf);
}

static void walk_rootnode(const struct rootnode *r, unsigned *maxblk,
			  unsigned long *count)
{
	unsigned i;

	for (i = 0; i < NPTR; i++) {
		if (!valid_block(r->ptr[i]))
			continue;
		if (devblock(r->ptr[i]) > *maxblk)
			*maxblk = devblock(r->ptr[i]);
		(*count)++;
		if (r->levels)
			walk_tree(r->ptr[i], r->levels, maxblk, count);
	}
}

/* Min/max block of one inode's data, including its pointer blocks. */
static void tree_range(uint32_t blk, int level, unsigned *mn, unsigned *mx)
{
	unsigned char *buf = malloc(bs);
	unsigned i;

	if (!buf)
		return;
	if (read_at((unsigned long long)devblock(blk) * bs, buf, bs)) {
		free(buf);
		return;
	}
	for (i = 0; i < bs / 4; i++) {
		uint32_t p = rd32(buf + 4 * i);
		unsigned d;

		if (!valid_block(p))
			continue;
		d = devblock(p);
		if (!*mn || d < *mn)
			*mn = d;
		if (d > *mx)
			*mx = d;
		if (level > 1)
			tree_range(p, level - 1, mn, mx);
	}
	free(buf);
}

static void inode_range(const struct rootnode *r, unsigned *mn, unsigned *mx)
{
	unsigned i;

	for (i = 0; i < NPTR; i++) {
		unsigned d;

		if (!valid_block(r->ptr[i]))
			continue;
		d = devblock(r->ptr[i]);
		if (!*mn || d < *mn)
			*mn = d;
		if (d > *mx)
			*mx = d;
		if (r->levels)
			tree_range(r->ptr[i], r->levels, mn, mx);
	}
}

struct layout {
	const char *name;
	unsigned o_bs, o_ni, o_nb;
	unsigned o_inode, o_bitmap, o_longfile;
};

static const struct layout layouts[] = {
	/* plain qnx6: blocksize at 48, Inode 72, Bitmap 152 (qnx6_fs.h) */
	{ "plain", 48, 52, 60, 72, 152, 232 },
	/* Audi MMI: blocksize at 40, Inode 64, Bitmap 144 */
	{ "mmi", 40, 44, 52, 64, 144, 224 },
};

static unsigned sb_off;		/* byte offset of the superblock in the fs */
static const struct layout *lay;

static void dump_sb(const char *tag, const unsigned char *sb)
{
	struct rootnode in, bm, lf;

	printf("%s magic %08x checksum %08x serial %016" PRIx64 "\n", tag,
	       rd32(sb), rd32(sb + 4), rd64(sb + 8));
	printf("%s layout %s blocksize %u num_inodes %u free_inodes %u "
	       "num_blocks %u free_blocks %u\n", tag, lay->name,
	       rd32(sb + lay->o_bs), rd32(sb + lay->o_ni),
	       rd32(sb + lay->o_ni + 4), rd32(sb + lay->o_nb),
	       rd32(sb + lay->o_nb + 4));
	parse_rootnode(sb + lay->o_inode, &in);
	parse_rootnode(sb + lay->o_bitmap, &bm);
	parse_rootnode(sb + lay->o_longfile, &lf);
	printf("%s Inode    size %" PRIu64 " levels %u ptr0 %08x\n", tag,
	       in.size, in.levels, in.ptr[0]);
	printf("%s Bitmap   size %" PRIu64 " levels %u ptr0 %08x\n", tag,
	       bm.size, bm.levels, bm.ptr[0]);
	printf("%s Longfile size %" PRIu64 " levels %u ptr0 %08x\n", tag,
	       lf.size, lf.levels, lf.ptr[0]);
}

int main(int argc, char **argv)
{
	unsigned char sb[QNX6_SB_SIZE];
	unsigned char *buf;
	struct rootnode in, bm;
	unsigned long n, i;
	unsigned long long set = 0, hi_lsb = 0, hi_msb = 0;
	unsigned maxinode = 0;
	unsigned long ptrs = 0;
	const char *dev;
	unsigned long total_inodes = 0;
	unsigned long want_ino = 0;
	int have_ino = 0;

	{
		int ai = 1;

		if (argc > 2 && !strcmp(argv[1], "-i")) {
			want_ino = strtoul(argv[2], NULL, 0);
			have_ino = 1;
			ai = 3;
		}
		dev = argc > ai ? argv[ai] : "/dev/hd3";
	}
	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", dev, strerror(errno));
		return 1;
	}
	/* The superblock is either at byte 0 or after the 0x2000 boot block
	 * (fs/qnx6/inode.c tries 0x2000 first, then 0). */
	{
		unsigned long cand[2] = { QNX6_BOOTBLOCK, 0 };
		int ci, found = 0;

		for (ci = 0; ci < 2 && !found; ci++) {
			if (read_at(cand[ci], sb, sizeof(sb)))
				continue;
			if (rd32(sb) == QNX6_MAGIC ||
			    __builtin_bswap32(rd32(sb)) == QNX6_MAGIC) {
				sb_off = cand[ci];
				found = 1;
			}
		}
		if (!found) {
			fprintf(stderr, "not qnx6: no magic at 0 or 0x%x\n",
				QNX6_BOOTBLOCK);
			return 1;
		}
	}
	be = rd32(sb) != QNX6_MAGIC;
	/* The bootblock form is the plain layout; try mmi if the fields are
	 * implausible. */
	lay = &layouts[0];
	bs = rd32(sb + lay->o_bs);
	if (bs < 512 || bs > 65536 || (bs & (bs - 1))) {
		lay = &layouts[1];
		bs = rd32(sb + lay->o_bs);
	}
	if (bs < 512 || bs > 65536 || (bs & (bs - 1))) {
		fprintf(stderr, "implausible blocksize %u\n", bs);
		return 1;
	}
	printf("device %s  endian %s  superblock at 0x%x  layout %s\n", dev,
	       be ? "big" : "little", sb_off, lay->name);
	dump_sb("sb1", sb);

	blks_off = (sb_off / bs) + (QNX6_SB_AREA / bs);
	ptrbits = 0;
	while ((1u << ptrbits) < bs / 4)
		ptrbits++;
	num_blocks = rd32(sb + lay->o_nb);
	printf("blocksize %u  blks_off %u  ptrbits %u  num_blocks %u "
	       "(%" PRIu64 " MiB)\n", bs, blks_off, ptrbits, num_blocks,
	       ((uint64_t)num_blocks * bs) >> 20);

	parse_rootnode(sb + lay->o_inode, &in);
	parse_rootnode(sb + lay->o_bitmap, &bm);

	/* ---- block bitmap ---- */
	buf = malloc(bm.size < bs ? bs : bs);
	if (!buf)
		return 1;
	n = (unsigned long)((bm.size + bs - 1) / bs);
	printf("bitmap: %" PRIu64 " bytes, %lu fs blocks, %lu bits\n",
	       bm.size, n, (unsigned long)bm.size * 8);
	for (i = 0; i < n; i++) {
		unsigned blk = map_block(&bm, i);
		unsigned long j, chunk = bm.size - (unsigned long long)i * bs;

		if (!blk) {
			fprintf(stderr, "bitmap hole at block %lu\n", i);
			continue;
		}
		if (read_at((unsigned long long)blk * bs, buf, bs)) {
			fprintf(stderr, "bitmap read failed at %lu\n", i);
			continue;
		}
		if (chunk > bs)
			chunk = bs;
		for (j = 0; j < chunk; j++) {
			unsigned char v = buf[j];
			unsigned long base = (i * bs + j) * 8;
			int k;

			if (!v)
				continue;
			for (k = 0; k < 8; k++) {
				/* Bits past num_blocks are mkqnx6fs padding; they
				 * are set to 1 and must not count. */
				if (v & (1u << k)) {
					unsigned long bit = base + k;

					if (bit < num_blocks) {
						set++;
						if (bit > hi_lsb)
							hi_lsb = bit;
					}
				}
				if (v & (0x80u >> k)) {
					unsigned long bit = base + k;

					if (bit < num_blocks && bit > hi_msb)
						hi_msb = bit;
				}
			}
		}
	}
	printf("bitmap: %llu set bits; first 64 bytes:\n", (unsigned long long)set);
	{
		unsigned blk = map_block(&bm, 0);

		if (blk && read_at((unsigned long long)blk * bs, buf, bs) == 0) {
			for (i = 0; i < 64; i++) {
				printf("%02x%s", buf[i], (i % 16 == 15) ? "\n" : " ");
			}
		}
	}
	printf("bitmap high-water (bit N = device block N+%u): LSB-first "
	"device block %llu (byte %llu, %.2f GiB), MSB-first device block "
	"%llu (byte %llu, %.2f GiB)\n", blks_off,
	(unsigned long long)(hi_lsb + blks_off),
	(unsigned long long)(hi_lsb + blks_off) * bs,
	(double)((unsigned long long)(hi_lsb + blks_off) * bs) /
	(1ull << 30),
	(unsigned long long)(hi_msb + blks_off),
	(unsigned long long)(hi_msb + blks_off) * bs,
	(double)((unsigned long long)(hi_msb + blks_off) * bs) /
	(1ull << 30));
	high_bitmap = hi_lsb > hi_msb ? hi_lsb : hi_msb;

	/* ---- inode table ---- */
	{
		unsigned long ninodes = (unsigned long)rd32(sb + lay->o_ni);
		unsigned long inode_bytes = ninodes * QNX6_INODE_SIZE;
		unsigned long inode_blocks = (inode_bytes + bs - 1) / bs;
		unsigned char *ibuf = malloc(bs);
		unsigned long iidx = 0;

		printf("inode table: %lu inodes, %lu bytes, %lu fs blocks\n",
		       ninodes, inode_bytes, inode_blocks);
		for (i = 0; i < inode_blocks && iidx < ninodes; i++) {
			unsigned blk = map_block(&in, i);
			unsigned long j;

			if (!blk || read_at((unsigned long long)blk * bs, ibuf, bs))
				continue;
			if (blk > maxinode)
				maxinode = blk;
			for (j = 0; j + QNX6_INODE_SIZE <= bs && iidx < ninodes;
			     j += QNX6_INODE_SIZE, iidx++) {
				unsigned char *ip = ibuf + j;
				/* di_status at +101, di_filelevels at +100 */
				unsigned status = ip[101];
				struct rootnode r;
				uint64_t fsize = rd64(ip);
				uint16_t mode = rd16(ip + 32);
				int k;

				/* QNX inode numbers are 1-based; iidx is the
				 * 0-based slot in the table. */
				if (have_ino && iidx + 1 == want_ino) {
					unsigned mn = 0, mx = 0;

					r.size = fsize;
					for (k = 0; k < NPTR; k++)
						r.ptr[k] = rd32(ip + 36 + 4 * k);
					r.levels = ip[100];
					inode_range(&r, &mn, &mx);
					printf("inode %lu: size %" PRIu64
					       " mode %04o status %u levels %u "
					       "blocks %u..%u (bytes 0x%llx..0x%llx)\n",
					       want_ino, fsize, mode, status,
					       r.levels, mn, mx,
					       (unsigned long long)mn * bs,
					       (unsigned long long)mx * bs);
					close(fd);
					return 0;
				}
				if (status != 1 && status != 3)
					continue;
				if (fsize == 0)
					continue;
				total_inodes++;
				r.size = fsize;
				for (k = 0; k < NPTR; k++)
					r.ptr[k] = rd32(ip + 36 + 4 * k);
				r.levels = ip[100];
				if (r.levels > 5)
					continue;
				walk_rootnode(&r, &maxinode, &ptrs);
				(void)mode;
			}
		}
		free(ibuf);
	}
	printf("inode walk: %lu live inodes, %lu pointers; highest device block "
	       "%u (byte %llu, %.2f GiB)\n", total_inodes, ptrs, maxinode,
	       (unsigned long long)maxinode * bs,
	       (double)((unsigned long long)maxinode * bs) / (1ull << 30));

	{
		unsigned long long bmdev = high_bitmap + blks_off;
		unsigned hi = maxinode > bmdev ? maxinode : (unsigned)bmdev;

		printf("HIGH-WATER: bitmap device block %llu, inodes %u -> %u "
		       "(byte %llu, %.2f GiB)\n", bmdev, maxinode, hi,
		       (unsigned long long)hi * bs,
		       (double)((unsigned long long)hi * bs) / (1ull << 30));
	}
	close(fd);
	return 0;
}
