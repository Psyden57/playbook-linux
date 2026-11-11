/*
 * pbplay -- a minimal OSS playback probe for the PlayBook bring-up.
 *
 * The WM8994 card is up as `0 [WM8994]: simple-card - PlayBook WM8994`, but
 * nothing has opened a stream.  The first write()/SNDCTL_DSP_SPEED is what
 * calls the DAI's hw_params, so this program exercises the whole path with a
 * format we choose and prints every step.
 *
 * Needs snd-mixer-oss.ko and snd-pcm-oss.ko; /dev/dsp appears once they load
 * and a card exists.
 *
 * Usage: pbplay [seconds] [freq]
 *        freq 0 = silence, otherwise a square wave at `freq` Hz.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

/* Full-scale-ish square wave: loud enough to judge by ear on a speaker. */
#define TONE_AMP 12000

static int step(const char *what, int fd, unsigned long req, int *val)
{
	int r = ioctl(fd, req, val);

	if (r < 0) {
		fprintf(stderr, "pbplay: ioctl %s: %s\n", what, strerror(errno));
		return -1;
	}
	printf("pbplay: %-10s -> %d\n", what, *val);
	return 0;
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/dsp";
	int secs = 2, freq = 440;
	/*
	 * Ask for 4 fragments of 2^14 bytes: 16384 frames, i.e. ~341 ms at
	 * 48 kHz stereo S16.  The OSS default (256-byte fragments) gives a
	 * 64-frame period, and at 48 kHz the period interrupt never keeps up --
	 * the stream reports one period and then sits in XRUN with hw_ptr
	 * pinned.  The 8 kHz path, which works, gets 4096-frame periods.
	 */
	int fmt = AFMT_S16_LE, ch = 2, rate = 48000, frag = 0x0004000E;
	int fd, i, frames, nframes, blksz;
	uint32_t phase = 0, inc;
	int16_t *buf;
	unsigned long total = 0;

	if (argc > 1)
		secs = atoi(argv[1]);
	if (argc > 2)
		freq = atoi(argv[2]);
	if (secs <= 0)
		secs = 1;

	fd = open(dev, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "pbplay: open %s: %s\n", dev, strerror(errno));
		return 1;
	}
	printf("pbplay: opened %s (S16_LE %d ch %d Hz, %d s, %d Hz tone)\n",
	       dev, ch, rate, secs, freq);

	if (step("SETFMT", fd, SNDCTL_DSP_SETFMT, &fmt))
		return 1;
	if (step("CHANNELS", fd, SNDCTL_DSP_CHANNELS, &ch))
		return 1;
	if (step("SPEED", fd, SNDCTL_DSP_SPEED, &rate))
		return 1;
	step("SETFRAGMENT", fd, SNDCTL_DSP_SETFRAGMENT, &frag);

	/*
	 * Write exactly one hardware period per write(), and take the period
	 * from the driver instead of assuming 1024 frames fits.  The original
	 * code wrote 1024 frames into the 64-frame ring that
	 * SETFRAGMENT 0x00040008 asks for, so the first write overran and the
	 * stream sat in XRUN with hw_ptr pinned at one period.
	 */
	blksz = 0;
	if (ioctl(fd, SNDCTL_DSP_GETBLKSIZE, &blksz) < 0 || blksz <= 0) {
		fprintf(stderr, "pbplay: GETBLKSIZE: %s\n", strerror(errno));
		return 1;
	}
	frames = blksz / (ch * 2);
	printf("pbplay: block size %d bytes (%d frames)\n", blksz, frames);
	if (frames <= 0)
		return 1;

	buf = calloc(frames * 2, sizeof(int16_t));
	if (!buf)
		return 1;

	inc = (uint32_t)(((uint64_t)freq << 16) / (rate > 0 ? rate : 48000));
	nframes = rate * secs;

	while (total / (unsigned)(ch * 2) < (unsigned)nframes) {
		for (i = 0; i < frames; i++) {
			int16_t s = 0;

			if (freq > 0) {
				phase += inc;
				s = (phase & 0x8000) ? TONE_AMP : -TONE_AMP;
			}
			buf[i * 2] = s;
			buf[i * 2 + 1] = s;
		}
		{
			ssize_t n = write(fd, buf, frames * ch * 2);

			if (n < 0) {
				fprintf(stderr, "pbplay: write: %s (after %lu bytes)\n",
					strerror(errno), total);
				return 1;
			}
			total += (unsigned long)n;
		}
	}

	printf("pbplay: wrote %lu bytes, done\n", total);
	close(fd);
	return 0;
}
