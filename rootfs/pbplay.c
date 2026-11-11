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

#define CHUNK_FRAMES 1024

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
	int fmt = AFMT_S16_LE, ch = 2, rate = 48000, frag = 0x00040008;
	int fd, i, frames, nframes;
	uint32_t phase = 0, inc;
	int16_t buf[CHUNK_FRAMES * 2];
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

	inc = (uint32_t)(((uint64_t)freq << 16) / (rate > 0 ? rate : 48000));
	nframes = rate * secs;

	while (total / (unsigned)(ch * 2) < (unsigned)nframes) {
		frames = CHUNK_FRAMES;
		for (i = 0; i < frames; i++) {
			int16_t s = 0;

			if (freq > 0) {
				phase += inc;
				s = (phase & 0x8000) ? 8000 : -8000;
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
