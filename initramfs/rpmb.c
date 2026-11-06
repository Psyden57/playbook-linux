/*
 * pbrpmb — Linux-native RPMB read/write for the PlayBook OMAP4430 (report 141,
 * 143).
 *
 * Step 0/1 (read-only): open /dev/mmcblk1rpmb, GET_WRITE_COUNTER (no MAC),
 * authenticated READ_DATA, verify the frame MAC against the pinned key.
 *
 * Step 3 (one write): the BOOTCTR_FCTRY record carries a 4-byte value; the
 * only legal write is 0x32, only when the record currently reads 1..0x32 and
 * is not already 0x32.  The record block is patched IN PLACE in the byte image
 * the device returned, both HMACs are recomputed, and exactly one write is
 * issued (CMD25+REL_WR, CMD25 result-read, CMD18), followed by an
 * authenticated readback.  A rejected write leaves the value unchanged and
 * the unit bootable; read it back before doing anything else.
 *
 * The RPMB frame key was pinned in session 19 by reading the live QNX page
 * (rpmb-omap4 page+0x460) and verifying HMAC-SHA256(key, resp[0xE4..0x200))
 * against the MAC the device returned to pbrpmb (report 143).  The 16-byte
 * block-HMAC key is seed[0:16] (rpmb-omap4 page+0x440), likewise verified
 * against a real record block.  Both are deterministic per CID and survive a
 * reboot.
 *
 * The MMC UAPI is embedded because the cross-toolchain sysroot does not ship
 * <linux/mmc/ioctl.h>; the constants match the 6.15.11 kernel used to build the
 * image (MMC_IOC_CMD=0xc048b300, MMC_IOC_MULTI_CMD=0xc008b301).
 *
 * Usage:
 *   pbrpmb selftest             SHA-256/HMAC vectors + ioctl-number check
 *   pbrpmb counter              GET_WRITE_COUNTER only
 *   pbrpmb read <addr>          raw authenticated read of one block
 *   pbrpmb probe                counter + read of block 0 + frame MAC check
 *   pbrpmb scan                 find BOOTCTR_FCTRY (zero writes)
 *   pbrpmb prep <addr>          unseal, patch to 0x32, seal, print the frame
 *   pbrpmb write <addr> yes     the operator's ONE verified 0x32 write
 *   pbrpmb oracle [n]           the 0x12 monitor address oracle (no RPMB)
 *   pbrpmb keytest              key matrix for the two-stage transform (no write)
 *   pbrpmb pairtest             reference block-0x3B pair across key candidates
 *   pbrpmb blockscan [a [b]]    unseal blocks a..b and look for the record store
 *   pbrpmb topup [thresh]       boot-time top-up: write 0x32 only if the value
 *                               is below <thresh> (default 0x1E); one attempt
 */
#define _GNU_SOURCE
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* ---- embedded MMC UAPI (include/uapi/linux/mmc/ioctl.h + major.h) ---- */
struct mmc_ioc_cmd {
	int write_flag;
	int is_acmd;
	uint32_t opcode;
	uint32_t arg;
	uint32_t response[4];
	unsigned int flags;
	unsigned int blksz;
	unsigned int blocks;
	unsigned int postsleep_min_us;
	unsigned int postsleep_max_us;
	unsigned int data_timeout_ns;
	unsigned int cmd_timeout_ms;
	uint32_t __pad;
	uint64_t data_ptr;
};
struct mmc_ioc_multi_cmd {
	uint64_t num_of_cmds;
	struct mmc_ioc_cmd cmds[];
};
#define MMC_BLOCK_MAJOR 179
#define MMC_IOC_CMD _IOWR(MMC_BLOCK_MAJOR, 0, struct mmc_ioc_cmd)
#define MMC_IOC_MULTI_CMD _IOWR(MMC_BLOCK_MAJOR, 1, struct mmc_ioc_multi_cmd)

#define MMC_RSP_PRESENT (1 << 0)
#define MMC_RSP_CRC (1 << 2)
#define MMC_RSP_OPCODE (1 << 4)
#define MMC_RSP_R1 (MMC_RSP_PRESENT | MMC_RSP_CRC | MMC_RSP_OPCODE)
#define MMC_CMD_ADTC (1 << 5)
#define MMC_DATA_FLAGS (MMC_RSP_R1 | MMC_CMD_ADTC)

#define MMC_WRITE_MULTIPLE_BLOCK 25
#define MMC_READ_MULTIPLE_BLOCK 18

struct rpmb_frame {
	uint8_t stuff[196];
	uint8_t key_mac[32];
	uint8_t data[256];
	uint8_t nonce[16];
	uint32_t write_counter;
	uint16_t addr;
	uint16_t block_count;
	uint16_t result;
	uint16_t req_resp;
} __attribute__((packed));

#define RPMB_GET_WRITE_COUNTER 0x0002
#define RPMB_WRITE_DATA 0x0003
#define RPMB_READ_DATA 0x0004
#define RPMB_RESULT_READ 0x0005

_Static_assert(sizeof(struct mmc_ioc_cmd) == 72, "mmc_ioc_cmd layout");
_Static_assert(sizeof(struct rpmb_frame) == 512, "rpmb_frame layout");

/* ---- SHA-256 / HMAC-SHA256 (FIPS 180-4, RFC 2104) ---- */
static const uint32_t K256[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
	0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
	0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
	0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
	0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
	0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t st[8], const uint8_t *p)
{
	uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) |
		       ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
	for (i = 16; i < 64; i++) {
		uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);

		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	a = st[0]; b = st[1]; c = st[2]; d = st[3];
	e = st[4]; f = st[5]; g = st[6]; h = st[7];
	for (i = 0; i < 64; i++) {
		uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
		uint32_t ch = (e & f) ^ (~e & g);
		uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
		uint32_t maj = (a & b) ^ (a & c) ^ (b & c);

		t1 = h + S1 + ch + K256[i] + w[i];
		t2 = S0 + maj;
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}
	st[0] += a; st[1] += b; st[2] += c; st[3] += d;
	st[4] += e; st[5] += f; st[6] += g; st[7] += h;
}

static void sha256(const void *data, size_t len, uint8_t out[32])
{
	uint32_t st[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
			   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	const uint8_t *p = data;
	uint64_t bits = (uint64_t)len * 8;
	uint8_t tail[128];
	size_t rem = len % 64, tl;
	int i;

	while (len >= 64) {
		sha256_block(st, p);
		p += 64;
		len -= 64;
	}
	memset(tail, 0, sizeof(tail));
	memcpy(tail, p, rem);
	tail[rem] = 0x80;
	tl = (rem < 56) ? 64 : 128;
	for (i = 0; i < 8; i++)
		tail[tl - 1 - i] = (uint8_t)(bits >> (8 * i));
	sha256_block(st, tail);
	if (tl == 128)
		sha256_block(st, tail + 64);
	for (i = 0; i < 8; i++) {
		out[4 * i] = st[i] >> 24;
		out[4 * i + 1] = st[i] >> 16;
		out[4 * i + 2] = st[i] >> 8;
		out[4 * i + 3] = st[i];
	}
}

static void hmac_sha256(const uint8_t *key, size_t klen, const void *msg,
			size_t mlen, uint8_t out[32])
{
	uint8_t k[64], ipad[64], opad[64], inner[32];
	uint8_t *buf;
	size_t i;

	memset(k, 0, sizeof(k));
	if (klen > 64) {
		sha256(key, klen, k);
	} else {
		memcpy(k, key, klen);
	}
	for (i = 0; i < 64; i++) {
		ipad[i] = k[i] ^ 0x36;
		opad[i] = k[i] ^ 0x5c;
	}
	buf = malloc(64 + mlen);
	if (!buf)
		return;
	memcpy(buf, ipad, 64);
	memcpy(buf + 64, msg, mlen);
	sha256(buf, 64 + mlen, inner);
	memcpy(buf, opad, 64);
	memcpy(buf + 64, inner, 32);
	sha256(buf, 96, out);
	free(buf);
}

/* ---- RPMB transport ---- */
static int rpmb_xfer(int fd, struct rpmb_frame *req, struct rpmb_frame *resp)
{
	size_t sz = sizeof(struct mmc_ioc_multi_cmd) + 2 * sizeof(struct mmc_ioc_cmd);
	struct mmc_ioc_multi_cmd *m = calloc(1, sz);
	int ret;

	if (!m)
		return -1;
	m->num_of_cmds = 2;
	m->cmds[0].write_flag = 1;
	m->cmds[0].opcode = MMC_WRITE_MULTIPLE_BLOCK;
	m->cmds[0].flags = MMC_DATA_FLAGS;
	m->cmds[0].blksz = 512;
	m->cmds[0].blocks = 1;
	m->cmds[0].data_ptr = (uint64_t)(uintptr_t)req;
	m->cmds[1].write_flag = 0;
	m->cmds[1].opcode = MMC_READ_MULTIPLE_BLOCK;
	m->cmds[1].flags = MMC_DATA_FLAGS;
	m->cmds[1].blksz = 512;
	m->cmds[1].blocks = 1;
	m->cmds[1].data_ptr = (uint64_t)(uintptr_t)resp;
	ret = ioctl(fd, MMC_IOC_MULTI_CMD, m);
	free(m);
	return ret;
}

/*
 * One RPMB WRITE_DATA transaction: request frame with the reliable-write bit,
 * a RESULT_READ frame, then CMD18 into the response buffer (the kernel's
 * mmc_route_rpmb_frames() order).  `resp` is used for both the result request
 * and the response, as the kernel does.
 */
static int __attribute__((unused))
rpmb_write_xfer(int fd, struct rpmb_frame *req, struct rpmb_frame *resp)
{
	size_t sz = sizeof(struct mmc_ioc_multi_cmd) +
		    3 * sizeof(struct mmc_ioc_cmd);
	struct mmc_ioc_multi_cmd *m = calloc(1, sz);
	int ret;

	if (!m)
		return -1;
	m->num_of_cmds = 3;

	m->cmds[0].write_flag = 1 | (int)(1u << 31);	/* REL_WR */
	m->cmds[0].opcode = MMC_WRITE_MULTIPLE_BLOCK;
	m->cmds[0].flags = MMC_DATA_FLAGS;
	m->cmds[0].blksz = 512;
	m->cmds[0].blocks = 1;
	m->cmds[0].data_ptr = (uint64_t)(uintptr_t)req;

	memset(resp, 0, sizeof(*resp));
	resp->req_resp = htobe16(RPMB_RESULT_READ);
	m->cmds[1].write_flag = 1;
	m->cmds[1].opcode = MMC_WRITE_MULTIPLE_BLOCK;
	m->cmds[1].flags = MMC_DATA_FLAGS;
	m->cmds[1].blksz = 512;
	m->cmds[1].blocks = 1;
	m->cmds[1].data_ptr = (uint64_t)(uintptr_t)resp;

	m->cmds[2].write_flag = 0;
	m->cmds[2].opcode = MMC_READ_MULTIPLE_BLOCK;
	m->cmds[2].flags = MMC_DATA_FLAGS;
	m->cmds[2].blksz = 512;
	m->cmds[2].blocks = 1;
	m->cmds[2].data_ptr = (uint64_t)(uintptr_t)resp;

	ret = ioctl(fd, MMC_IOC_MULTI_CMD, m);
	free(m);
	return ret;
}

static int get_counter(int fd, uint32_t *counter)
{
	int bc;

	/*
	 * QNX's counter request (0x1aa8) memsets the frame and sets only
	 * req_resp=2: block_count stays 0.  Try 0 first, then 1, and print both
	 * results; reads are free, so this costs nothing.
	 */
	for (bc = 0; bc <= 1; bc++) {
		struct rpmb_frame req, resp;
		int ret;

		memset(&req, 0, sizeof(req));
		memset(&resp, 0, sizeof(resp));
		req.req_resp = htobe16(RPMB_GET_WRITE_COUNTER);
		req.block_count = htobe16(bc);
		ret = rpmb_xfer(fd, &req, &resp);
		if (ret < 0) {
			printf("counter: ioctl failed: %s\r\n", strerror(errno));
			return -1;
		}
		printf("counter(bc=%d): result=%d req_resp=0x%04x counter=%u\r\n",
		       bc, be16toh(resp.result), be16toh(resp.req_resp),
		       be32toh(resp.write_counter));
		if (be16toh(resp.result) == 0) {
			*counter = be32toh(resp.write_counter);
			return 0;
		}
	}
	return 1;
}

static int read_block(int fd, unsigned addr, const uint8_t nonce[16],
		      struct rpmb_frame *resp)
{
	int bc;

	for (bc = 0; bc <= 1; bc++) {
		struct rpmb_frame req;
		int ret;

		memset(&req, 0, sizeof(req));
		memset(resp, 0, sizeof(*resp));
		req.req_resp = htobe16(RPMB_READ_DATA);
		req.addr = htobe16(addr);
		req.block_count = htobe16(bc);
		memcpy(req.nonce, nonce, 16);
		ret = rpmb_xfer(fd, &req, resp);
		if (ret < 0) {
			printf("read %u: ioctl failed: %s\r\n", addr,
			       strerror(errno));
			return -1;
		}
		printf("read %u(bc=%d): result=%d req_resp=0x%04x\r\n", addr, bc,
		       be16toh(resp->result), be16toh(resp->req_resp));
		if (be16toh(resp->result) == 0) {
			if (memcmp(resp->nonce, nonce, 16)) {
				printf("read %u: nonce mismatch\r\n", addr);
				return -2;
			}
			return 0;
		}
	}
	return 1;
}

static void hex(const uint8_t *p, size_t n, char *out)
{
	static const char h[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < n; i++) {
		out[2 * i] = h[p[i] >> 4];
		out[2 * i + 1] = h[p[i] & 15];
	}
	out[2 * n] = 0;
}

/* The 32-byte seed that ships in rpmb-omap4 (file 0x71b4). */
static const uint8_t lit_seed[32] =
	"retreating is advancing in the o";

/*
 * Pinned in session 19 from the live TabletOS page (report 143):
 *   rpmb-omap4 page+0x460 -> the 32-byte frame key;
 *   rpmb-omap4 page+0x440 -> the block HMAC key (first 16 bytes of the seed).
 * Both reproduce device MACs exactly; they are f(CID seed) and stable.
 */
static const uint8_t rpmb_key[32] = {
	/* the REFERENCE's frame key (report 156, page+0x460): verified on this
	 * unit's live frame MAC in session 26.  The previous value (dfee...) was
	 * the SPARE's and cannot authenticate reference frames. */
	0x16,0x11,0xb3,0xbb,0x0e,0xed,0xaa,0xcb,
	0xe0,0xc1,0x55,0xb1,0xa5,0x45,0xcc,0xcd,
	0x34,0xb3,0xf1,0xad,0x78,0xf2,0x9a,0x37,
	0x5c,0xf0,0xc4,0x68,0xf7,0x9f,0xd9,0x22
};
static const uint8_t block_key[16] = {
	/* the REFERENCE's seed[0:16] (report 156); the last 4 bytes were the
	 * spare's (46 28 01 b4) — they differ per unit (CID-derived). */
	0x15,0x00,0x00,0x01,0x4d,0x43,0x47,0x41,
	0x46,0x41,0x10,0x00,0x2b,0xe1,0x1d,0xa8
};

#define REC_MAGIC 0x5983BC2D
#define REC_NAME "BOOTCTR_FCTRY"

/* QNX stores the logical 256-byte block reversed in the frame's data field. */
static void rev256(uint8_t out[256], const uint8_t in[256])
{
	int i;

	for (i = 0; i < 256; i++)
		out[i] = in[255 - i];
}

static void block_mac(const uint8_t logical[256], uint8_t out[32])
{
	hmac_sha256(block_key, 16, logical, 0xE0, out);
}

static void mac_ok(const uint8_t *key, size_t klen, const struct rpmb_frame *resp,
		   uint8_t out[32])
{
	hmac_sha256(key, klen, (const uint8_t *)resp + 0xE4, 0x11C, out);
}

static int try_one(const char *label, const char *what, const uint8_t *cand,
		   const struct rpmb_frame *resp, uint8_t key[32])
{
	uint8_t mac[32];

	mac_ok(cand, 32, resp, mac);
	if (!memcmp(mac, resp->key_mac, 32)) {
		printf("  candidate %s/%s: MATCH\r\n", label, what);
		memcpy(key, cand, 32);
		return 0;
	}
	return -1;
}

/* Candidate derivations f(seed) from report 141 §6.2. */
static int try_seed(const char *label, const uint8_t s[32],
		    const struct rpmb_frame *resp, uint8_t key[32])
{
	uint8_t cand[32], zero[16] = {0};

	if (!try_one(label, "identity", s, resp, key))
		return 0;
	hmac_sha256(zero, 16, s, 32, cand);
	if (!try_one(label, "HMAC(0^16,seed)", cand, resp, key))
		return 0;
	sha256(s, 32, cand);
	if (!try_one(label, "SHA256(seed)", cand, resp, key))
		return 0;
	hmac_sha256(s, 16, s + 16, 16, cand);
	if (!try_one(label, "HMAC(seed[0:16],seed[16:32])", cand, resp, key))
		return 0;
	hmac_sha256(s, 32, zero, 16, cand);
	if (!try_one(label, "HMAC(seed,0^16)", cand, resp, key))
		return 0;
	return -1;
}

/*
 * The CID seed, reconstructed from rpmb-omap4 0x2d40-0x2e10.  The CID is
 * read into sp+0x3c; the 20-byte seed at sp+0x20 is (offsets):
 *   [0]=CID15 [2]=CID13 [3]=CID14 [4..9]=CID12,11,10,9,8,7
 *   [10]=CID6 [12]=CID2 [13]=CID3 [14]=CID4 [15]=CID5 [16]=CID1
 * Everything else is zero.  The 0x2F call reads 0x20 bytes from that buffer,
 * so the tail (seed[20..31]) is zero.
 */
static int cid_seed32(uint8_t s[32])
{
	char buf[64];
	uint8_t cid[16];
	int fd, n, i;

	memset(s, 0, 32);
	fd = open("/sys/block/mmcblk1/device/cid", O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n < 32)
		return -1;
	for (i = 0; i < 16; i++) {
		unsigned v;

		if (sscanf(buf + 2 * i, "%2x", &v) != 1)
			return -1;
		cid[i] = (uint8_t)v;
	}
	s[0] = cid[15];
	s[2] = cid[13];
	s[3] = cid[14];
	s[4] = cid[12];
	s[5] = cid[11];
	s[6] = cid[10];
	s[7] = cid[9];
	s[8] = cid[8];
	s[9] = cid[7];
	s[10] = cid[6];
	s[12] = cid[2];
	s[13] = cid[3];
	s[14] = cid[4];
	s[15] = cid[5];
	s[16] = cid[1];
	return 0;
}

static int cmd_selftest(void)
{
	uint8_t out[32];
	char hx[65];
	static const uint8_t abc_sha[32] = {
		0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,
		0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
		0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
	};
	static const uint8_t rfc4231_2[32] = {
		0x5b,0xdc,0xc1,0x46,0xbf,0x60,0x75,0x4e,0x6a,0x04,0x24,0x26,
		0x08,0x95,0x75,0xc7,0x5a,0x00,0x3f,0x08,0x9d,0x27,0x39,0x83,
		0x9d,0xec,0x58,0xb9,0x64,0xec,0x38,0x43
	};
	int fail = 0;

	sha256("abc", 3, out);
	if (memcmp(out, abc_sha, 32)) {
		hex(out, 32, hx); printf("SHA-256(abc) FAIL %s\r\n", hx); fail = 1;
	} else puts("SHA-256(abc) OK\r\n");

	hmac_sha256((const uint8_t *)"Jefe", 4,
		    "what do ya want for nothing?", 28, out);
	if (memcmp(out, rfc4231_2, 32)) {
		hex(out, 32, hx); printf("HMAC RFC4231#2 FAIL %s\r\n", hx); fail = 1;
	} else puts("HMAC RFC4231#2 OK\r\n");

	printf("ioctl: CMD=%#lx MULTI=%#lx sizeof(cmd)=%zu\r\n",
	       (unsigned long)MMC_IOC_CMD, (unsigned long)MMC_IOC_MULTI_CMD,
	       sizeof(struct mmc_ioc_cmd));
	return fail;
}

static int cmd_counter(int fd)
{
	uint32_t c = 0;
	int ret = get_counter(fd, &c);

	if (ret) {
		printf("counter: FAIL %d (%s)\r\n", ret, strerror(-ret));
		return 1;
	}
	printf("counter: %u (0x%08x)\r\n", c, c);
	return 0;
}

static int one_read(int fd, unsigned addr, const uint8_t nonce[16],
		    struct rpmb_frame *resp)
{
	int ret = read_block(fd, addr, nonce, resp);
	char hx[65];

	if (ret) {
		printf("read %u: FAIL %d (%s)\r\n", addr, ret,
		       ret == -2 ? "nonce mismatch" :
		       ret > 0 ? "rpmb result" : strerror(-ret));
		return 1;
	}
	hex(resp->key_mac, 32, hx);
	printf("read %u: result=0 counter=%u nonce=ok\r\n", addr,
	       be32toh(resp->write_counter));
	printf("  mac : %s\r\n", hx);
	hex(resp->data, 32, hx);
	printf("  data: %s ...\r\n", hx);
	return 0;
}

/*
 * The RPMB nonce.  It must be unpredictable to the card: the frame MAC covers
 * it, so it is what makes a response fresh.
 *
 * The OMAP4 hardware RNG is behind the secure firewall on this unit (the board
 * DTS keeps `rng_target` disabled: a non-secure read raises an L3 error and an
 * imprecise external abort), so the CRNG may never be initialised.  Read
 * /dev/random with O_NONBLOCK — it only answers once the CRNG is seeded — and
 * fall back to /dev/urandom, saying so loudly.  A weak nonce is tolerable for
 * the read-only tests; it is NOT tolerable for a write.
 */
static int nonce_seeded = -1;	/* 1 = from a seeded CRNG, 0 = weak */
static int nonce_warned;

static void nonce_report(void)
{
	if (nonce_seeded == 0 && !nonce_warned++) {
		printf("WARNING: the CRNG is not initialised - this nonce is "
		       "predictable; do NOT write with it\r\n");
	}
}

static void fill_nonce(uint8_t nonce[16])
{
	int fd, i;

	fd = open("/dev/random", O_RDONLY | O_NONBLOCK);
	if (fd >= 0) {
		ssize_t n = read(fd, nonce, 16);

		close(fd);
		if (n == 16) {
			nonce_seeded = 1;
			nonce_report();
			return;
		}
	}
	nonce_seeded = 0;
	fd = open("/dev/urandom", O_RDONLY);
	if (fd >= 0 && read(fd, nonce, 16) == 16) {
		close(fd);
		nonce_report();
		return;
	}
	if (fd >= 0)
		close(fd);
	for (i = 0; i < 16; i++)
		nonce[i] = (uint8_t)(0x40 + i);
	nonce_report();
}

static int block_valid(const struct rpmb_frame *resp);

static int cmd_probe(int fd)
{
	uint8_t nonce[16], key[32], cidseed[32];
	struct rpmb_frame resp;
	uint32_t counter = 0;
	char hx[65];
	static char msg[2 * 0x11C + 1];
	int ret;

	ret = get_counter(fd, &counter);
	if (ret) {
		printf("counter: FAIL (rpmb result %d)\r\n", ret);
		return 1;
	}
	printf("counter: %u (0x%08x)\r\n", counter, counter);

	fill_nonce(nonce);
	if (one_read(fd, 0, nonce, &resp))
		return 1;

	hex((const uint8_t *)&resp + 0xE4, 0x11C, msg);
	printf("msg284: %s\r\n", msg);

	if (block_valid(&resp)) {
		hex(rpmb_key, 32, hx);
		printf("KEY PINNED (session 19): %s\r\n", hx);
		return 0;
	}
	printf("pinned key does NOT verify block 0\r\n");
	if (try_seed("literal", lit_seed, &resp, key) == 0 ||
	    (cid_seed32(cidseed) == 0 && try_seed("cid", cidseed, &resp, key) == 0)) {
		hex(key, 32, hx);
		printf("candidate key matched: %s\r\n", hx);
		return 0;
	}
	printf("no candidate matched; msg284 above is the exact MAC input\r\n");
	return 0;
}

static int block_valid(const struct rpmb_frame *resp)
{
	uint8_t mac[32];

	mac_ok(rpmb_key, 32, resp, mac);
	return !memcmp(mac, resp->key_mac, 32);
}

static int record_block_valid(const uint8_t L[256])
{
	uint8_t mac[32];

	block_mac(L, mac);
	return !memcmp(mac, L + 0xE0, 32);
}

/*
 * Find BOOTCTR_FCTRY in the logical (reversed) 256-byte block.  The name is at
 * header+0x04, so the header starts 4 bytes earlier; len is at name+0x12 and
 * the value at name+0x14.  Returns the name offset, or -1.
 */
static int find_record(const uint8_t *L, unsigned n, unsigned *lenp)
{
	unsigned i;

	for (i = 4; i + 0x18 <= n && i + 0x18 <= 0xE0; i++) {
		uint32_t magic;
		unsigned len;

		if (memcmp(L + i, REC_NAME, 13))
			continue;
		magic = (uint32_t)L[i - 4] | ((uint32_t)L[i - 3] << 8) |
			((uint32_t)L[i - 2] << 16) |
			((uint32_t)L[i - 1] << 24);
		if (magic != REC_MAGIC)
			continue;
		len = (unsigned)L[i + 0x12] | ((unsigned)L[i + 0x13] << 8);
		*lenp = len;
		return i;
	}
	return -1;
}

static int read_block_q(int fd, unsigned addr, const uint8_t nonce[16],
			struct rpmb_frame *resp)
{
	int bc;

	for (bc = 0; bc <= 1; bc++) {
		struct rpmb_frame req;

		memset(&req, 0, sizeof(req));
		memset(resp, 0, sizeof(*resp));
		req.req_resp = htobe16(RPMB_READ_DATA);
		req.addr = htobe16(addr);
		req.block_count = htobe16(bc);
		memcpy(req.nonce, nonce, 16);
		if (rpmb_xfer(fd, &req, resp) < 0)
			return -1;
		if (be16toh(resp->result) == 0) {
			if (memcmp(resp->nonce, nonce, 16))
				return -2;
			return 0;
		}
	}
	return 1;
}

static const uint8_t rec_magic[4] = { 0x2d, 0xbc, 0x83, 0x59 };

static int find_bytes(const uint8_t *hay, size_t n, const uint8_t *needle,
		      size_t m)
{
	size_t i;

	if (m > n)
		return -1;
	for (i = 0; i + m <= n; i++)
		if (!memcmp(hay + i, needle, m))
			return (int)i;
	return -1;
}

static int any_nonzero(const uint8_t *p, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (p[i])
			return 1;
	return 0;
}

static void hexline(const uint8_t *p, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		printf("%02x", p[i]);
	printf("\r\n");
}

static int cmd_scan(int fd, unsigned max)
{
	uint8_t nonce[16], L[256];
	struct rpmb_frame resp;
	unsigned addr;
	int hits = 0, valid = 0, badmac = 0, rfail = 0;

	for (addr = 0; addr < max; addr++) {
		unsigned len;
		int off, m1, m2, n1, n2;

		if (!(addr % 64))
			printf("scan: block %u/%u\r\n", addr, max);
		fill_nonce(nonce);
		if (read_block_q(fd, addr, nonce, &resp)) {
			rfail++;
			continue;
		}
		if (!block_valid(&resp)) {
			badmac++;
			continue;
		}
		valid++;
		rev256(L, resp.data);
		off = find_record(L, 256, &len);
		if (off >= 0) {
			printf("hit block %u name@0x%02x len=%u "
			       "value=%02x%02x%02x%02x blockhmac=%s\r\n",
			       addr, off, len, L[off + 0x14], L[off + 0x15],
			       L[off + 0x16], L[off + 0x17],
			       record_block_valid(L) ? "ok" : "BAD");
			hits++;
			continue;
		}
		m1 = find_bytes(L, 0xE0, rec_magic, 4);
		m2 = find_bytes(resp.data, 0xE0, rec_magic, 4);
		n1 = find_bytes(L, 0xE0, (const uint8_t *)REC_NAME, 13);
		n2 = find_bytes(resp.data, 0xE0, (const uint8_t *)REC_NAME, 13);
		if (m1 >= 0 || m2 >= 0 || n1 >= 0 || n2 >= 0) {
			printf("block %u MAGIC log=%d wire=%d name log=%d "
			       "wire=%d\r\n", addr, m1, m2, n1, n2);
			printf("  L[%02x]: ", m1 >= 0 ? m1 : 0);
			hexline(L + (m1 >= 0 ? m1 : 0), 48);
			hits++;
		} else if (addr < 16 && any_nonzero(L, 0xE0)) {
			printf("block %u L[0]: ", addr);
			hexline(L, 16);
		}
	}
	printf("scan: %d marker hit(s); valid=%d badmac=%d readfail=%d\r\n",
	       hits, valid, badmac, rfail);
	return 0;
}

static int cmd_dump(int fd, unsigned addr)
{
	uint8_t nonce[16], L[256];
	struct rpmb_frame resp;
	unsigned i;

	fill_nonce(nonce);
	if (read_block_q(fd, addr, nonce, &resp)) {
		printf("dump %u: read failed\r\n", addr);
		return 1;
	}
	printf("dump %u: frame mac %s\r\n", addr,
	       block_valid(&resp) ? "ok" : "BAD");
	rev256(L, resp.data);
	printf("blocks: logical=%s wire=%s bootctr=%s\r\n",
	       record_block_valid(L) ? "ok" : "BAD",
	       any_nonzero(resp.data, 0xE0) ? "nz" : "zero",
	       find_bytes(L, 0xE0, (const uint8_t *)REC_NAME, 13) >= 0 ? "y"
								       : "n");
	for (i = 0; i < 256; i += 16) {
		printf("%02x: ", i);
		hexline(L + i, 16);
	}
	return 0;
}

/* the transform helpers and the key matrix live further down */
static int pb_seal(const uint8_t P[224], uint8_t C[256]);
static int pb_unseal(const uint8_t C[256], uint8_t P[224]);
static int tag_ok_offline(const uint8_t C[256]);
static void wire_to_logical(uint8_t L[256], const uint8_t W[256]);
static int pb_use_measured(void);

/*
 * Build the one legal RPMB write: BOOTCTR_FCTRY = 0x32, sealed by the monitor.
 *
 * The record block is encrypted on media by the secure monitor, so the patched
 * plaintext must be RE-SEALED, not patched in ciphertext (report 144/175):
 * unseal the block, patch the 4 value bytes, seal, and check that the seal is
 * an exact inverse of the unseal before anything is built.  The frame carries
 * the sealed 256 bytes in the wire (reversed) order and a fresh write counter.
 *
 * Returns 1 and fills F, 0 if the value is already 0x32, or a negative code.
 */
static int build_topup(int fd, unsigned addr, struct rpmb_frame *F,
		       uint32_t *counter, uint8_t old[4], unsigned *recoff,
		       uint8_t N[256], uint8_t S[256])
{
	uint8_t nonce[16], L[256], W[256], P[224], chk[224], mac[32];
	struct rpmb_frame rd;
	unsigned len;
	int off, i;

	/* the seal needs the monitor state the read path uses */
	if (pb_use_measured())
		return -8;
	if (get_counter(fd, counter))
		return -10;
	fill_nonce(nonce);
	if (read_block_q(fd, addr, nonce, &rd))
		return -11;
	if (!block_valid(&rd))
		return -12;
	memcpy(W, rd.data, 256);
	wire_to_logical(L, W);
	if (!tag_ok_offline(L))
		return -13;
	/*
	 * The store is sealed: the records live in the MONITOR's plaintext, not
	 * in the bytes on media.  Unseal first (this also checks the tag), then
	 * find the record there.
	 */
	if (pb_unseal(L, P))
		return -20;
	off = find_record(P, 224, &len);
	if (off < 0)
		return -14;
	if (len != 4)
		return -15;
	for (i = 0; i < 4; i++)
		old[i] = P[off + 0x14 + i];
	if (old[1] || old[2] || old[3])
		return -16;
	if (old[0] == 0x32)
		return 0;
	if (old[0] == 0 || old[0] > 0x32)
		return -17;

	/* patch the plaintext, then seal it with the device's own transform */
	memset(N, 0, 256);
	memcpy(N, P, 224);
	N[off + 0x14] = 0x32;
	N[off + 0x15] = 0;
	N[off + 0x16] = 0;
	N[off + 0x17] = 0;
	if (pb_seal(N, S))
		return -18;
	if (pb_unseal(S, chk) || memcmp(chk, N, 224))
		return -19;		/* the seal does not round-trip: refuse */
	if (S[0xE0] == 0)
		return -19;

	memset(F, 0, sizeof(*F));
	rev256(F->data, S);		/* the frame carries the wire order */
	F->write_counter = htobe32(*counter);
	F->addr = htobe16((uint16_t)addr);
	F->block_count = htobe16(1);
	F->result = 0;
	F->req_resp = htobe16(RPMB_WRITE_DATA);
	hmac_sha256(rpmb_key, 32, (const uint8_t *)F + 0xE4, 0x11C, mac);
	memcpy(F->key_mac, mac, 32);
	*recoff = off;
	return 1;
}

static void dump_frame(const struct rpmb_frame *F)
{
	char hx[65];

	hex(F->key_mac, 32, hx);
	printf("  mac : %s\r\n", hx);
	printf("  ct  : %u addr=%u bc=%u req=0x%04x\r\n",
	       be32toh(F->write_counter), be16toh(F->addr),
	       be16toh(F->block_count), be16toh(F->req_resp));
}

/*
 * The replay block: the genuine 256-byte data field of RPMB block 2 as the
 * device returned it in session 20, when its plaintext was BOOTCTR_FCTRY =
 * 32 00 00 00 (docs/reports/data/rpmb-block2-replay-2026-10-31.txt).
 *
 * Why this exists: the 224-byte record block is sealed by the secure monitor
 * and we cannot yet reproduce that transform, but we do not have to — the
 * device already made this exact block for value 0x32.  Re-sending these bytes
 * in a fresh frame restores that state; the block HMAC inside the field is
 * untouched, so the monitor's unseal yields the device's own 0x32 record.
 */
static const uint8_t replay_block[256] = {
	0x3e, 0x4f, 0x84, 0x8e, 0xe5, 0xc8, 0x06, 0x7d, 0x08, 0xd8, 0xbe, 0xda,
	0xd7, 0x7a, 0x87, 0x34, 0xcf, 0x56, 0xaf, 0xba, 0xf2, 0x47, 0x8e, 0xc4,
	0x9c, 0xe6, 0x0e, 0x23, 0x67, 0x58, 0x3e, 0xad, 0xa0, 0xbc, 0x02, 0x1a,
	0xb2, 0xdf, 0x5d, 0x81, 0x52, 0x49, 0x99, 0x21, 0x7e, 0x57, 0x4f, 0x46,
	0xfa, 0x5a, 0x64, 0x1d, 0x1b, 0xb2, 0x21, 0x3c, 0x8a, 0x7e, 0x46, 0xec,
	0xb2, 0x39, 0x35, 0xa0, 0x60, 0x8c, 0x40, 0xa6, 0x49, 0xbb, 0x6c, 0x8b,
	0x3d, 0x1d, 0x4b, 0xb4, 0x0e, 0x69, 0xce, 0x42, 0x60, 0x8c, 0x40, 0xa6,
	0x49, 0xbb, 0x6c, 0x8b, 0x3d, 0x1d, 0x4b, 0xb4, 0x0e, 0x69, 0xce, 0x42,
	0x60, 0x8c, 0x40, 0xa6, 0x49, 0xbb, 0x6c, 0x8b, 0x3d, 0x1d, 0x4b, 0xb4,
	0x0e, 0x69, 0xce, 0x42, 0x60, 0x8c, 0x40, 0xa6, 0x49, 0xbb, 0x6c, 0x8b,
	0x3d, 0x1d, 0x4b, 0xb4, 0x0e, 0x69, 0xce, 0x42, 0x60, 0x8c, 0x40, 0xa6,
	0x49, 0xbb, 0x6c, 0x8b, 0x3d, 0x1d, 0x4b, 0xb4, 0x0e, 0x69, 0xce, 0x42,
	0x60, 0x8c, 0x40, 0xa6, 0x49, 0xbb, 0x6c, 0x8b, 0x3d, 0x1d, 0x4b, 0xb4,
	0x0e, 0x69, 0xce, 0x42, 0x60, 0x8c, 0x40, 0xa6, 0x49, 0xbb, 0x6c, 0x8b,
	0x3d, 0x1d, 0x4b, 0xb4, 0x0e, 0x69, 0xce, 0x42, 0x60, 0x8c, 0x40, 0xa6,
	0x49, 0xbb, 0x6c, 0x8b, 0x3d, 0x1d, 0x4b, 0xb4, 0x0e, 0x69, 0xce, 0x42,
	0x60, 0x8c, 0x40, 0xa6, 0x49, 0xbb, 0x6c, 0x8b, 0x3d, 0x1d, 0x4b, 0xb4,
	0x0e, 0x69, 0xce, 0x42, 0x60, 0x8c, 0x40, 0xa6, 0x49, 0xbb, 0x6c, 0x8b,
	0x3d, 0x1d, 0x4b, 0xb4, 0x0e, 0x69, 0xce, 0x42, 0xab, 0x0e, 0x9d, 0x2a,
	0x2b, 0x10, 0x80, 0x2f, 0x11, 0xf6, 0x5e, 0xe1, 0x0e, 0x6a, 0xb3, 0xd2,
	0x55, 0xee, 0xef, 0x66, 0x9f, 0xf2, 0x95, 0x99, 0x99, 0x8e, 0x48, 0x1b,
	0xb3, 0x93, 0x51, 0x99,
};

/*
 * Build the write frame for a replay.  The only fresh parts are the RPMB write
 * counter and the frame MAC (HMAC-SHA256 with the pinned frame key over
 * frame[0xE4..0x200), the same construction pbrpmb verifies on responses).
 */
static int build_replay(int fd, unsigned addr, struct rpmb_frame *F,
			uint32_t *counter)
{
	uint8_t mac[32];

	if (addr != 2) {
		printf("replay: only block 2 (BOOTCTR_FCTRY) has a captured "
		       "block\r\n");
		return -1;
	}
	if (get_counter(fd, counter))
		return -10;
	memset(F, 0, sizeof(*F));
	memcpy(F->data, replay_block, 256);
	F->write_counter = htobe32(*counter);
	F->addr = htobe16((uint16_t)addr);
	F->block_count = htobe16(1);
	F->result = 0;
	F->req_resp = htobe16(RPMB_WRITE_DATA);
	hmac_sha256(rpmb_key, 32, (const uint8_t *)F + 0xE4, 0x11C, mac);
	memcpy(F->key_mac, mac, 32);
	return 1;
}


/* Read a block and re-send its own bytes: a full frame-path rehearsal that
 * cannot change any content.  This is the write test that cannot hurt. */
static int cmd_replay_ident(int fd, unsigned addr, int write_it)
{
	struct rpmb_frame F, resp, rb;
	uint32_t counter = 0;
	uint8_t nonce[16], mac[32];
	int r;

	fill_nonce(nonce);
	if (read_block_q(fd, addr, nonce, &rb)) {
		printf("replay-ident: read block %u failed\r\n", addr);
		return 1;
	}
	if (!block_valid(&rb)) {
		printf("replay-ident: frame MAC BAD\r\n");
		return 1;
	}
	if (get_counter(fd, &counter)) {
		printf("replay-ident: counter read failed\r\n");
		return 1;
	}
	memset(&F, 0, sizeof(F));
	memcpy(F.data, rb.data, 256);
	F.write_counter = htobe32(counter);
	F.addr = htobe16((uint16_t)addr);
	F.block_count = htobe16(1);
	F.result = 0;
	F.req_resp = htobe16(RPMB_WRITE_DATA);
	hmac_sha256(rpmb_key, 32, (const uint8_t *)&F + 0xE4, 0x11C, mac);
	memcpy(F.key_mac, mac, 32);
	printf("replay-ident: block %u counter %u, data = the block's own bytes\r\n",
	       addr, counter);
	dump_frame(&F);
	if (!write_it) {
		printf("replay-ident: built, NOT written (add 'yes')\r\n");
		return 0;
	}
	printf("replay-ident: writing ...\r\n");
	r = rpmb_write_xfer(fd, &F, &resp);
	printf("replay-ident: write ioctl %d result=%u\r\n", r,
	       be16toh(resp.result));
	if (r || be16toh(resp.result)) {
		printf("replay-ident: REJECTED - nothing changed\r\n");
		return 1;
	}
	fill_nonce(nonce);
	if (read_block_q(fd, addr, nonce, &rb)) {
		printf("replay-ident: read-back failed\r\n");
		return 1;
	}
	printf("replay-ident: read-back mac %s data %s\r\n",
	       block_valid(&rb) ? "ok" : "BAD",
	       memcmp(rb.data, F.data, 256) ? "DIFFERS" : "matches");
	return memcmp(rb.data, F.data, 256) ? 1 : 0;
}

static int cmd_replay(int fd, unsigned addr, int write_it)
{
	struct rpmb_frame F, resp, rb;
	uint32_t counter = 0;
	uint8_t nonce[16];
	int i, r;

	fill_nonce(nonce);
	if (read_block_q(fd, addr, nonce, &rb)) {
		printf("replay: read block %u failed\r\n", addr);
		return 1;
	}
	if (!block_valid(&rb)) {
		printf("replay: current block %u frame MAC BAD\r\n", addr);
		return 1;
	}
	if (!memcmp(rb.data, replay_block, 256)) {
		printf("replay: block %u already holds the captured 0x32 block; "
		       "nothing to write\r\n", addr);
		return 0;
	}
	printf("replay: block %u differs from the captured 0x32 block\r\n",
	       addr);

	r = build_replay(fd, addr, &F, &counter);
	if (r <= 0) {
		printf("replay: REFUSED %d\r\n", r);
		return 1;
	}
	printf("replay: frame for block %u, write counter %u\r\n",
	       addr, counter);
	dump_frame(&F);
	{
		const uint8_t *p = (const uint8_t *)&F;

		printf("replay: frame bytes:");
		for (i = 0; i < 512; i++) {
			if (i % 32 == 0)
				printf("\r\n  %03x:", i);
			printf("%02x", p[i]);
		}
		printf("\r\n");
	}
	if (!write_it) {
		printf("replay: built, NOT written (add 'yes' to write)\r\n");
		return 0;
	}

	printf("replay: writing ...\r\n");
	r = rpmb_write_xfer(fd, &F, &resp);
	printf("replay: write ioctl %d, result=%u\r\n", r,
	       be16toh(resp.result));
	if (r || be16toh(resp.result)) {
		printf("replay: write REJECTED - the record is unchanged\r\n");
		return 1;
	}
	fill_nonce(nonce);
	if (read_block_q(fd, addr, nonce, &rb)) {
		printf("replay: read-back failed\r\n");
		return 1;
	}
	printf("replay: read-back frame mac %s, data %s\r\n",
	       block_valid(&rb) ? "ok" : "BAD",
	       memcmp(rb.data, replay_block, 256) ? "DIFFERS" : "matches");
	if (!memcmp(rb.data, replay_block, 256)) {
		printf("replay: PASS - media holds the captured 0x32 block again\r\n");
		return 0;
	}
	return 1;
}

static int cmd_prep(int fd, unsigned addr)
{
	struct rpmb_frame F;
	uint32_t counter = 0;
	uint8_t old[4], N[256], S[256];
	unsigned off = 0;
	int r, i;

	r = build_topup(fd, addr, &F, &counter, old, &off, N, S);
	if (r <= 0) {
		if (r == 0)
			printf("prep: value is already 32 00 00 00; nothing to "
			       "do\r\n");
		else
			printf("prep: REFUSED (%d) - see the codes in the "
			       "source\r\n", r);
		return 1;
	}
	printf("prep: block 0x%x record@%u value %02x%02x%02x%02x -> 32 00 00 00\r\n",
	       addr, off, old[0], old[1], old[2], old[3]);
	printf("prep: counter %u; seal round-trip OK\r\n", counter);
	printf("prep: sealed(256) =");
	for (i = 0; i < 256; i++)
		printf("%s%02x", (i % 32) ? "" : "\r\n  ", S[i]);
	printf("\r\n");
	dump_frame(&F);
	printf("prep: ONE frame of type 3 to block 0x%x, %d bytes; NOT written\r\n",
	       addr, (int)sizeof(F));
	printf("prep: writing is the operator's act: 'run /pbrpmb write 0x%x yes'\r\n",
	       addr);
	return 0;
}

/*
 * The write itself.  This is the operator's step, not an agent's:
 * SAFETY.md §2/§6.  It builds the same frame as `prep`, sends exactly one
 * type-3 frame plus its result read, then does an authenticated read-back and
 * prints the value it finds.  A failed write is as fatal as a wrong value, so
 * never loop it.
 */
static int cmd_write(int fd, unsigned addr)
{
	struct rpmb_frame F, resp, rb;
	uint32_t counter = 0;
	uint8_t nonce[16], old[4], N[256], S[256], P[224];
	unsigned off = 0;
	int r, i;

	r = build_topup(fd, addr, &F, &counter, old, &off, N, S);
	if (r <= 0) {
		if (r == 0)
			printf("write: value is already 32 00 00 00; nothing "
			       "written\r\n");
		else
			printf("write: REFUSED (%d); nothing written\r\n", r);
		return 1;
	}
	printf("write: block 0x%x value %02x%02x%02x%02x -> 32 00 00 00, "
	       "counter %u\r\n", addr, old[0], old[1], old[2], old[3],
	       counter);
	dump_frame(&F);
	printf("write: sending ONE frame ...\r\n");
	r = rpmb_write_xfer(fd, &F, &resp);
	printf("write: ioctl %d result=%u\r\n", r, be16toh(resp.result));
	if (r || be16toh(resp.result)) {
		printf("write: REJECTED - read the block back before anything "
		       "else\r\n");
	} else {
		printf("write: accepted\r\n");
	}

	/* read it back: the only evidence that matters */
	fill_nonce(nonce);
	if (read_block_q(fd, addr, nonce, &rb)) {
		printf("write: READ-BACK FAILED\r\n");
		return 1;
	}
	printf("write: read-back frame MAC %s\r\n",
	       block_valid(&rb) ? "ok" : "BAD");
	{
		uint8_t W[256], L[256];

		memcpy(W, rb.data, 256);
		wire_to_logical(L, W);
		printf("write: read-back block tag %s\r\n",
		       tag_ok_offline(L) ? "ok" : "BAD");
		if (pb_unseal(L, P)) {
			printf("write: read-back unseal FAILED\r\n");
			return 1;
		}
		printf("write: read-back plaintext[%d..%d] =", off,
		       off + 0x17);
		for (i = 0; i < 4; i++)
			printf(" %02x", P[off + 0x14 + i]);
		printf("\r\n");
		printf("write: %s\r\n",
		       (P[off + 0x14] == 0x32 && P[off + 0x15] == 0 &&
			P[off + 0x16] == 0 && P[off + 0x17] == 0) ?
		       "PASS - BOOTCTR_FCTRY reads 0x32" :
		       "FAIL - the value did not take");
	}
	return 0;
}

/* ---- the boot-time top-up --------------------------------------------- */

/*
 * klog — one line into the kernel log.  The boot-time top-up runs from /init
 * with stdio on /dev/null, so this is how its outcome reaches `kmsg`.
 */
static void klog(const char *fmt, ...)
{
	char b[256];
	va_list ap;
	int fd, n;

	va_start(ap, fmt);
	n = vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	fd = open("/dev/kmsg", O_WRONLY);
	if (fd >= 0) {
		write(fd, b, n);
		close(fd);
	}
}

/* Read BOOTCTR_FCTRY's four value bytes out of a block's monitor plaintext. */
static int read_value(int fd, unsigned addr, uint8_t v[4], unsigned *offp,
		      unsigned *lenp)
{
	uint8_t nonce[16], W[256], L[256], P[224];
	struct rpmb_frame rd;
	unsigned len;
	int off;

	fill_nonce(nonce);
	if (read_block_q(fd, addr, nonce, &rd))
		return -1;
	if (!block_valid(&rd))
		return -2;
	memcpy(W, rd.data, 256);
	wire_to_logical(L, W);
	if (pb_unseal(L, P))
		return -3;
	off = find_record(P, 224, &len);
	if (off < 0)
		return -4;
	if (offp)
		*offp = off;
	if (lenp)
		*lenp = len;
	memcpy(v, P + off + 0x14, 4);
	return 0;
}

/* Where the record lives: the known block first, then a bounded scan. */
static int find_counter_block(int fd)
{
	static const unsigned firsts[] = { 0x3a };
	uint8_t v[4];
	unsigned b;
	int i;

	for (i = 0; i < (int)(sizeof(firsts) / sizeof(firsts[0])); i++)
		if (read_value(fd, firsts[i], v, NULL, NULL) == 0)
			return (int)firsts[i];
	for (b = 0; b < 0x80; b++)
		if (read_value(fd, b, v, NULL, NULL) == 0)
			return (int)b;
	return -1;
}

/*
 * topup [threshold] — the boot-time top-up.
 *
 * Reads the record and writes 0x32 ONLY when the value is inside the legal
 * 1..0x32 range and below <threshold> (default 0x1E = 30, so a reset loop has
 * room to be noticed before the budget runs out).  The write is the same
 * single-frame, sealed, read-back write as `write` (report 175): unseal, patch
 * four bytes, re-seal with the monitor, verify the seal round-trips, send one
 * type-3 frame, read the result, unseal the read-back.
 *
 * One attempt, never a loop: a failed RPMB write does not return
 * (SAFETY.md §2), and the only legal target value is 0x32.
 */
static int cmd_topup(int fd, unsigned thresh)
{
	struct rpmb_frame F, rb;
	uint32_t counter = 0;
	uint8_t old[4], N[256], S[256];
	unsigned off = 0, len = 0;
	int blk, r;

	if (pb_use_measured()) {
		klog("pbrpmb: topup: the monitor transform is unavailable\n");
		return 1;
	}
	blk = find_counter_block(fd);
	if (blk < 0) {
		klog("pbrpmb: topup: BOOTCTR_FCTRY not found in blocks 0..0x7f\n");
		return 1;
	}
	r = read_value(fd, (unsigned)blk, old, &off, &len);
	if (r) {
		klog("pbrpmb: topup: block 0x%x unreadable (%d)\n", blk, r);
		return 1;
	}
	klog("pbrpmb: topup: BOOTCTR_FCTRY at block 0x%x record@%u = %02x%02x%02x%02x\n",
	     blk, off, old[0], old[1], old[2], old[3]);

	if (len != 4 || old[1] || old[2] || old[3]) {
		klog("pbrpmb: topup: record is not a 4-byte value, REFUSING\n");
		return 1;
	}
	if (old[0] == 0x32) {
		klog("pbrpmb: topup: already 0x32, nothing to do\n");
		return 0;
	}
	if (old[0] == 0 || old[0] > 0x32) {
		klog("pbrpmb: topup: value 0x%02x is outside 1..0x32, REFUSING\n",
		     old[0]);
		return 1;
	}
	if (old[0] >= thresh) {
		klog("pbrpmb: topup: 0x%02x >= threshold 0x%02x, no write\n",
		     old[0], thresh);
		return 0;
	}

	r = build_topup(fd, (unsigned)blk, &F, &counter, old, &off, N, S);
	if (r != 1) {
		klog("pbrpmb: topup: build refused (%d), nothing written\n", r);
		return 1;
	}
	klog("pbrpmb: topup: writing 0x%02x -> 0x32 (RPMB counter %u)\n",
	     old[0], counter);
	r = rpmb_write_xfer(fd, &F, &rb);
	if (r || be16toh(rb.result)) {
		klog("pbrpmb: topup: WRITE FAILED (ioctl %d, result %u) - read back before anything else\n",
		     r, be16toh(rb.result));
		return 1;
	}
	if (read_value(fd, (unsigned)blk, old, NULL, NULL) == 0 && old[0] == 0x32 &&
	    !old[1] && !old[2] && !old[3]) {
		klog("pbrpmb: topup: PASS - BOOTCTR_FCTRY = 0x32\n");
		return 0;
	}
	klog("pbrpmb: topup: READ-BACK MISMATCH - do not reboot blindly\n");
	return 1;
}

/*
 * The two-stage monitor transform helpers live with the key matrix further
 * down; the tests below use them.
 */
static int pb_seal(const uint8_t P[224], uint8_t C[256]);
static int pb_unseal(const uint8_t C[256], uint8_t P[224]);
static int tag_ok_offline(const uint8_t C[256]);
static void wire_to_logical(uint8_t L[256], const uint8_t W[256]);

/*
 * Validate the monitor's two-stage transform via the pbmon misc devices.
 *
 * The input is the 256-byte data field as a read returns it — [ciphertext 224]
 * [tag 32] — which is exactly the interface /dev/pbunseal now carries
 * (session 27; report 174 §3/§5).  The driver digests the ciphertext with the
 * 0x2E stage and compares it with the block's own tag before decrypting, so
 * "unseal returns EBADMSG" means the digest stage did not reproduce the tag,
 * and "unseal returns a plaintext that is not the store" means the 0x2F cipher
 * key is still wrong.  Both are reported separately from the offline HMAC.
 */
static int cmd_sealtest(int fd)
{
	uint8_t nonce[16], W[256], C[256], P[224], C2[256];
	struct rpmb_frame resp;
	int u, s, ret = 1;

	fill_nonce(nonce);
	if (read_block_q(fd, 0x43, nonce, &resp)) {
		printf("sealtest: read block 0x43 failed\r\n");
		return 1;
	}
	if (!block_valid(&resp)) {
		printf("sealtest: block 0x43 frame MAC invalid\r\n");
		return 1;
	}
	memcpy(W, resp.data, 256);
	wire_to_logical(C, W);

	printf("sealtest: tag offline=%s\r\n", tag_ok_offline(C) ? "ok" : "BAD");
	u = pb_unseal(C, P);
	printf("sealtest: unseal -> %s\r\n",
	       u == 0 ? "ok" : u == -EBADMSG ? "TAG MISMATCH" : strerror(-u));
	if (u) {
		printf("sealtest: the 0x2E stage did not reproduce the tag\r\n");
		return 1;
	}
	printf("sealtest: P[0:16]=");
	{
		int i;

		for (i = 0; i < 16; i++)
			printf("%02x", P[i]);
		printf(" magic fwd@%d rev@%d\r\n",
		       find_bytes(P, 224, (const uint8_t *)"\x2d\xbc\x83\x59", 4),
		       find_bytes(P, 224, (const uint8_t *)"\x59\x83\xbc\x2d", 4));
	}

	s = pb_seal(P, C2);
	printf("sealtest: seal -> %s\r\n",
	       s == 0 ? "ok" : strerror(-s));
	if (s)
		return 1;
	if (!memcmp(C2, C, 256)) {
		printf("sealtest: PASS - unseal and seal both match block 0x43\r\n");
		ret = 0;
	} else {
		int k = 0;

		while (k < 256 && C2[k] == C[k])
			k++;
		printf("sealtest: seal(P) differs from the media block "
		       "(first diff at %d)\r\n", k);
	}
	return ret;
}

#define NKEY 6

/*
 * Candidate 16-byte halves.  The pair is (blob[0:16], blob[16:32]): the first
 * half is the 0x2E digest key (index 2, the reference's seed[0:16], proven)
 * and the second is only read when the 0x33 preload length is 32.  Index 5 is
 * the seed's real second half, so (2, 5) is the device's whole 32-byte seed.
 */
static const uint8_t keytest_keys[NKEY][16] = {
	/* page+0x20 (the bootloader page slot / session-19 default) */
	{ 0x86, 0x78, 0x7c, 0x2a, 0x63, 0x2f, 0xbe, 0x16,
	  0x48, 0xec, 0xab, 0xa6, 0xd8, 0x84, 0x01, 0x09 },
	/* page+0x40, the second key in the page */
	{ 0x44, 0x3a, 0xc2, 0x02, 0xeb, 0x50, 0x05, 0x1f,
	  0x0c, 0x2c, 0x12, 0xb3, 0x4a, 0xc9, 0x3b, 0x4d },
	/* seed[0:16] — the block-HMAC key (reference, proven offline and live) */
	{ 0x15, 0x00, 0x00, 0x01, 0x4d, 0x43, 0x47, 0x41,
	  0x46, 0x41, 0x10, 0x00, 0x2b, 0xe1, 0x1d, 0xa8 },
	/* the reference's frame key, first 16 */
	{ 0x16, 0x11, 0xb3, 0xbb, 0x0e, 0xed, 0xaa, 0xcb,
	  0xe0, 0xc1, 0x55, 0xb1, 0xa5, 0x45, 0xcc, 0xcd },
	/* the reference's frame key, second 16 */
	{ 0x34, 0xb3, 0xf1, 0xad, 0x78, 0xf2, 0x9a, 0x37,
	  0x5c, 0xf0, 0xc4, 0x68, 0xf7, 0x9f, 0xd9, 0x22 },
	/* seed[16:32] — the passphrase fragment (page+0x450) */
	{ 0x3e, 0x00, 0x00, 0x00, 0x69, 0x6e, 0x67, 0x20,
	  0x69, 0x6e, 0x20, 0x74, 0x68, 0x65, 0x20, 0x6f },
};

static const uint8_t seed_first[16] = {
	0x15, 0x00, 0x00, 0x01, 0x4d, 0x43, 0x47, 0x41,
	0x46, 0x41, 0x10, 0x00, 0x2b, 0xe1, 0x1d, 0xa8
};

/* {op, kek, klen_sel (0 = 16-byte preload, 1 = 32), key_mode} */
static int pb_set_ctl(uint8_t op, uint8_t kek, uint8_t klen_sel)
{
	uint8_t v[4] = { op, kek, klen_sel, 0 };
	int fd, ok;

	fd = open("/dev/pbctl", O_WRONLY);
	if (fd < 0)
		return -1;
	ok = write(fd, v, 4) == 4 ? 0 : -1;
	close(fd);
	return ok;
}

/*
 * The winning transform parameters, measured in session 27:
 *
 *   digest key (0x2E descriptor) = seed[0:16]      (page+0x440, proven)
 *   cipher key (0x33 preload)    = page+0x20       (86 78 7c 2a…)
 *   NO 0x31 select_kek            — issuing it changes the plaintext and
 *                                   breaks the transform
 *
 * `wsweep` found it: with the digest key held at the seed and no 0x31, the
 * preload window at page+0x20 unseals the captured block-0x3B bytes into the
 * captured plaintext exactly.
 */
static const uint8_t cipher_key[16] = {
	0x86, 0x78, 0x7c, 0x2a, 0x63, 0x2f, 0xbe, 0x16,
	0x48, 0xec, 0xab, 0xa6, 0xd8, 0x84, 0x01, 0x09
};

/* set the two transform key slots (/dev/pbkeys: keyA || keyB) */
static int pb_set_keys2(const uint8_t a[16], const uint8_t b[16]);
static int pb_set_keys2(const uint8_t a[16], const uint8_t b[16])
{
	uint8_t v[32];
	int fd, ok;

	memcpy(v, a, 16);
	memcpy(v + 16, b, 16);
	fd = open("/dev/pbkeys", O_WRONLY);
	if (fd < 0)
		return -1;
	ok = write(fd, v, 32) == 32 ? 0 : -1;
	close(fd);
	return ok;
}

/* pin the driver to the measured combination (see the block comment above) */
static int pb_use_measured(void)
{
	return pb_set_ctl(0, 0, 0) || pb_set_keys2(seed_first, cipher_key);
}

static const char *xf_status(int r)
{
	return r == 0 ? "ok" : r == -EBADMSG ? "tag" : "err";
}

/*
 * The monitor works on the LOGICAL view; /dev/mmcblk1rpmb hands us the wire
 * frame, in which the 256-byte data field is byte-reversed (trap 60).  This is
 * proven, not assumed: `dump 0x3b` shows the captured block-0x3B bytes rotated
 * so that its tag lands at +0xE0, and HMAC(seed16, L[0:0xE0]) == L[0xE0:] on
 * that view (report 156's pair).
 */
static void wire_to_logical(uint8_t L[256], const uint8_t W[256])
{
	rev256(L, W);
}

/*
 * One transform through /dev/pbseal (224 plaintext -> 256 sealed) or
 * /dev/pbunseal (256 sealed -> 224 plaintext).  The misc nodes are
 * non-seekable report nodes, so every call needs a fresh file description:
 * reusing one would leave the read offset advanced and the next read would
 * return nothing (trap 63).
 *
 * Returns 0 on success, -EBADMSG when the 0x2E tag does not match (a block
 * that does not authenticate), and another negative errno for an smc error.
 */
static int pb_xf(const char *path, const uint8_t *in, size_t inlen,
		 uint8_t *out, size_t outlen)
{
	int fd;
	ssize_t n;

	fd = open(path, O_RDWR);
	if (fd < 0)
		return -errno;
	if (write(fd, in, inlen) != (ssize_t)inlen) {
		int err = errno;

		close(fd);
		return err ? -err : -EIO;
	}
	n = read(fd, out, outlen);
	close(fd);
	if (n != (ssize_t)outlen)
		return n < 0 ? -errno : -EIO;
	return 0;
}

static int pb_seal(const uint8_t P[224], uint8_t C[256])
{
	return pb_xf("/dev/pbseal", P, 224, C, 256);
}

static int pb_unseal(const uint8_t C[256], uint8_t P[224])
{
	return pb_xf("/dev/pbunseal", C, 256, P, 224);
}

/* the block tag the device stores: HMAC-SHA256(seed[0:16], C[0:0xE0]) */
static int tag_ok_offline(const uint8_t C[256])
{
	uint8_t mac[32];

	hmac_sha256(block_key, 16, C, 0xE0, mac);
	return !memcmp(mac, C + 0xE0, 32);
}

/* one /dev/pbpair query (fresh description each time) */
static int pb_pair_query(char *rep, size_t sz)
{
	int fd = open("/dev/pbpair", O_RDONLY);
	ssize_t n;

	if (fd < 0)
		return -1;
	n = read(fd, rep, sz - 1);
	close(fd);
	if (n <= 0)
		return -1;
	rep[n] = '\0';
	return 0;
}

/*
 * wsweep — try every 16-byte window of the captured QNX page as the transform
 * key (driver side, /dev/pbwsweep).  Answers "is the key in the page at all"
 * against the fresh, same-boot pair.  No media access.
 */
static int cmd_wsweep(void)
{
	char rep[256];
	int fd = open("/dev/pbwsweep", O_RDONLY);
	ssize_t n;

	if (fd < 0) {
		printf("wsweep: open: %s\r\n", strerror(errno));
		return 1;
	}
	n = read(fd, rep, sizeof(rep) - 1);
	close(fd);
	if (n <= 0) {
		printf("wsweep: read: %s\r\n", strerror(errno));
		return 1;
	}
	rep[n] = '\0';
	printf("%s", rep);
	return strstr(rep, "HIT") ? 0 : 1;
}

/*
 * pairtest — the known-pair gate (report 144 §5), one boot, both stages.
 *
 * The digest key is proven (seed[0:16]), so it is held fixed and only the 0x33
 * preload — the engine's key input for the 0x2F cipher — is varied.  Every row
 * therefore returned a plaintext: `p=` answers whether the cipher key is being
 * used at all (identical p across rows = the engine holds its own key) and
 * `ct=OK` is the cipher matching the captured bytes.
 *
 * No media access: the pair is in the driver.
 */
static int cmd_pairtest(void)
{
	char rep[160];
	int b, hits = 0;

	if (pb_set_ctl(0, 2, 0)) {
		printf("pairtest: pbctl: %s\r\n", strerror(errno));
		return 1;
	}
	for (b = 0; b < NKEY; b++) {
		if (pb_set_keys2(seed_first, keytest_keys[b]))
			goto io;
		if (pb_pair_query(rep, sizeof(rep)))
			goto io;
		printf("pairtest: preload B=%d %s", b, rep);
		if (strstr(rep, "ct=OK") || strstr(rep, "unseal=0/OK"))
			hits++;
	}

	/* the measured preload (page+0x20) under all three kek settings: the
	 * no-kek row is the one wsweep found, and the other two are the control
	 * that shows the 0x31 call changes the result */
	{
		static const uint8_t keks[] = { 0, 1, 2 };
		int v;

		for (v = 0; v < 3; v++) {
			if (pb_set_ctl(0, keks[v], 0) ||
			    pb_set_keys2(seed_first, cipher_key))
				goto io;
			if (pb_pair_query(rep, sizeof(rep)))
				goto io;
			printf("pairtest: preload=0x20 kek=%u %s", keks[v], rep);
			if (strstr(rep, "ct=OK") && strstr(rep, "tag=OK"))
				hits++;
		}
	}

	printf("pairtest: %d line(s) with a stage match\r\n", hits);
	return 0;
io:
	printf("pairtest: io: %s\r\n", strerror(errno));
	return 1;
}

/*
 * keytest — the live-media check, digest key held at the proven seed and the
 * 0x33 preload varied.  Block 0x43's tag verifies offline first; then each row
 * must return a plaintext (the tag gate is satisfied by keyA) and shows the
 * first 32 bytes plus the record-magic offset.  Zero media writes: one read.
 */
static int cmd_keytest(int fd)
{
	static const uint8_t magic[4] = { 0x2d, 0xbc, 0x83, 0x59 };
	uint8_t nonce[16], W[256], D[256], P[224];
	struct rpmb_frame resp;
	int hit = 0;

	fill_nonce(nonce);
	if (read_block_q(fd, 0x43, nonce, &resp)) {
		printf("keytest: read block 0x43 failed\r\n");
		return 1;
	}
	if (!block_valid(&resp)) {
		printf("keytest: block 0x43 frame MAC invalid\r\n");
		return 1;
	}
	memcpy(W, resp.data, 256);
	wire_to_logical(D, W);
	printf("keytest: block 0x43 tag offline=%s\r\n",
	       tag_ok_offline(D) ? "ok" : "BAD");

	if (pb_use_measured()) {
		printf("keytest: setup: %s\r\n", strerror(errno));
		return 1;
	}
	{
		int i, off, u = pb_unseal(D, P);

		off = u == 0 ? find_bytes(P, 224, magic, 4) : -1;
		printf("keytest: unseal=%s p=", xf_status(u));
		if (u == 0) {
			for (i = 0; i < 8; i++)
				printf("%02x", P[i]);
		} else {
			printf("--------");
		}
		printf(" magic@%d\r\n", off);
		if (off >= 0) {
			uint8_t C2[256];

			hit = 1;
			if (pb_seal(P, C2)) {
				printf("keytest: seal failed\r\n");
			} else {
				printf("keytest: seal(P) vs the block: %s\r\n",
				       memcmp(C2, D, 256) ? "differs" : "equal");
			}
		}
	}
	if (!hit)
		printf("keytest: the measured pair did not produce the record "
		       "magic\r\n");
	return hit ? 0 : 1;
}

/*
 * blockscan — unseal every record block on media and look for the record store.
 *
 * The input is the 256-byte data field exactly as the read returned it: the
 * orientation is settled (report 156 §5 — a read leaves the logical view in
 * frame.data and the tag sits at +0xE0), so there is no framing sweep.  Three
 * counters separate the stages: `tag` counts blocks whose 0x2E digest did not
 * reproduce the stored tag, `ok` counts blocks the monitor unsealed, `offline`
 * counts blocks whose tag the same HMAC verifies host-side.  If offline and
 * `ok`+`tag` disagree, the driver and the monitor do not share a key.
 *
 * Read-only.
 */
static int cmd_blockscan(int fd, unsigned first, unsigned last)
{
	static const uint8_t magic[4] = { 0x2d, 0xbc, 0x83, 0x59 };
	static const char *names[4] = { REC_NAME, "bgroupcrt_dat", "fsecsyskey",
					"drmclock.cfg" };
	uint8_t nonce[16], W[256], D[256], P[224];
	struct rpmb_frame resp;
	unsigned b;
	int i, hits = 0, rdok = 0, ok = 0, tag = 0, err = 0, offline = 0;

	if (last > 511)
		last = 511;
	/*
	 * Pin the state to the measured pair rather than inheriting whatever a
	 * previous command left: digest key seed[0:16], 0x33 preload
	 * page+0x20, no select_kek.
	 */
	if (pb_use_measured()) {
		printf("blockscan: setup: %s\r\n", strerror(errno));
		return 1;
	}
	printf("blockscan: digest=seed[0:16] preload=page+0x20 kek=none\r\n");

	for (b = first; b <= last; b++) {
		int off, u;

		fill_nonce(nonce);
		if (read_block_q(fd, b, nonce, &resp))
			continue;
		if (!block_valid(&resp))
			continue;
		rdok++;
		memcpy(W, resp.data, 256);
		wire_to_logical(D, W);
		if (tag_ok_offline(D))
			offline++;
		u = pb_unseal(D, P);
		if (u == -EBADMSG) {
			tag++;
			continue;
		}
		if (u) {
			err++;
			continue;
		}
		ok++;
		/*
		 * Names first: every store block carries the record magic, so a
		 * magic test alone would mask the records.  A record is
		 * {u32 magic, char name[14], u16 slot, u16 slot, u16 len} and the
		 * payload follows the header, i.e. at name+0x14.
		 */
		for (i = 0; i < 4; i++) {
			off = find_bytes(P, 224, (const uint8_t *)names[i],
					 strlen(names[i]));
			if (off < 0)
				continue;
			printf("blockscan: HIT block 0x%x '%s'@%d ", b, names[i],
			       off);
			if (off >= 0x14 && off + 0x16 <= 224) {
				unsigned len = P[off + 0x12] |
					       (P[off + 0x13] << 8);
				int k;

				printf("len=%u data=", len);
				for (k = 0; k < 8; k++)
					printf("%02x", P[off + 0x14 + k]);
			}
			printf("\r\n");
			hits++;
			break;
		}
		if (i < 4)
			continue;
		off = find_bytes(P, 224, magic, 4);
		if (off >= 0) {
			printf("blockscan: block 0x%x magic@%d (no known name)\r\n",
			       b, off);
			hits++;
		}
	}
	printf("blockscan: %d blocks read/MAC-ok, tag offline %d, unsealed %d, "
	       "tag-mismatch %d, smc-error %d, %d hits\r\n",
	       rdok, offline, ok, tag, err, hits);
	return hits ? 0 : 1;
}

/*
 * The 0x12 address oracle (report 146 "what to try next"; session 20).  One
 * probe per write to /dev/pboracle, because a wrong command-page address hangs
 * the whole unit: each report line is printed before the next probe is asked
 * for, and the driver printk's it before the smc as well.  No media access and
 * no RPMB call happens here.
 *
 * Stops at the first probe that returns 0 and filled the buffer ("OK").
 */
static int cmd_oracle(unsigned only)
{
	int fd, first = only ? (int)only : 1, last = only ? (int)only : 7;
	int i, ret = 1;

	for (i = first; i <= last; i++) {
		char sel[8], rep[256];
		int n = snprintf(sel, sizeof(sel), "%d", i);
		ssize_t got;

		fd = open("/dev/pboracle", O_RDWR);
		if (fd < 0) {
			printf("oracle: open /dev/pboracle: %s\r\n",
			       strerror(errno));
			break;
		}
		if (write(fd, sel, n) != n) {
			printf("oracle %d: write: %s\r\n", i, strerror(errno));
			close(fd);
			break;
		}
		got = read(fd, rep, sizeof(rep) - 1);
		close(fd);
		if (got <= 0) {
			printf("oracle %d: no report (%s)\r\n", i,
			       strerror(errno));
			break;
		}
		rep[got] = '\0';
		printf("%s", rep);
		if (strstr(rep, " OK")) {
			ret = 0;
			break;
		}
	}
	return ret;
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/mmcblk1rpmb";
	const char *cmd = argc > 1 ? argv[1] : "probe";
	int fd, ret;

	if (!strcmp(cmd, "selftest"))
		return cmd_selftest();
	if (!strcmp(cmd, "oracle"))
		return cmd_oracle(argc > 2 ? (unsigned)strtoul(argv[2], NULL, 0)
					  : 0);
	if (!strcmp(cmd, "pairtest"))
		return cmd_pairtest();
	if (!strcmp(cmd, "wsweep"))
		return cmd_wsweep();

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		printf("open %s: %s\r\n", dev, strerror(errno));
		return 1;
	}
	if (!strcmp(cmd, "counter")) {
		ret = cmd_counter(fd);
	} else if (!strcmp(cmd, "read")) {
		uint8_t nonce[16];
		struct rpmb_frame resp;

		fill_nonce(nonce);
		ret = one_read(fd, argc > 2 ? strtoul(argv[2], NULL, 0) : 0,
			       nonce, &resp);
	} else if (!strcmp(cmd, "probe")) {
		ret = cmd_probe(fd);
	} else if (!strcmp(cmd, "scan")) {
		ret = cmd_scan(fd, argc > 2 ? (unsigned)strtoul(argv[2], NULL, 0)
					    : 512);
	} else if (!strcmp(cmd, "sealtest")) {
		ret = cmd_sealtest(fd);
	} else if (!strcmp(cmd, "replay")) {
		if (argc < 3) {
			printf("replay <block> [yes]\r\n");
			ret = 1;
		} else {
			ret = cmd_replay(fd,
					 (unsigned)strtoul(argv[2], NULL, 0),
					 argc > 3 && !strcmp(argv[3], "yes"));
		}
	} else if (!strcmp(cmd, "replay-ident")) {
		if (argc < 3) {
			printf("replay-ident <block> [yes]\r\n");
			ret = 1;
		} else {
			ret = cmd_replay_ident(fd,
					       (unsigned)strtoul(argv[2], NULL, 0),
					       argc > 3 && !strcmp(argv[3], "yes"));
		}
	} else if (!strcmp(cmd, "keytest")) {
		ret = cmd_keytest(fd);
	} else if (!strcmp(cmd, "blockscan")) {
		ret = cmd_blockscan(fd,
				    argc > 2 ? (unsigned)strtoul(argv[2], NULL, 0)
					     : 0,
				    argc > 3 ? (unsigned)strtoul(argv[3], NULL, 0)
					     : 511);
	} else if (!strcmp(cmd, "topup")) {
		ret = cmd_topup(fd, argc > 2 ?
				(unsigned)strtoul(argv[2], NULL, 0) : 0x1E);
	} else if (!strcmp(cmd, "dump")) {
		if (argc < 3) {
			printf("dump <block>\r\n");
			ret = 1;
		} else {
			ret = cmd_dump(fd, (unsigned)strtoul(argv[2], NULL, 0));
		}
	} else if (!strcmp(cmd, "prep")) {
		if (argc < 3) {
			printf("prep needs a block address from scan\r\n");
			ret = 1;
		} else {
			ret = cmd_prep(fd, (unsigned)strtoul(argv[2], NULL, 0));
		}
	} else if (!strcmp(cmd, "write")) {
		/* the explicit "yes" is the interlock: no accidental writes */
		if (argc < 4 || strcmp(argv[3], "yes")) {
			printf("write <block> yes\r\n");
			ret = 1;
		} else {
			ret = cmd_write(fd, (unsigned)strtoul(argv[2], NULL, 0));
		}
	} else {
		printf("usage: pbrpmb [selftest|counter|read <addr>|probe|scan|"
		       "prep <addr>|write <addr> yes|topup [thresh]|oracle [n]|keytest|"
		       "pairtest|blockscan]\r\n");
		ret = 1;
	}
	close(fd);
	return ret;
}
