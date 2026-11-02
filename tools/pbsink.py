#!/usr/bin/env python3
"""Measure the PlayBook console link: stream N bytes to the `sink` command.

Usage:
    tools/pbsink.py <len> [--dev /dev/ttyACM0] [--chunk 65536]

The console (initramfs/console.c, `sink`) reads exactly N bytes from
/dev/ttyGS0 and discards them, so this measures the USB/CDC-ACM path with no
media and no kernel log in the way.  Compare with the `blkwr` command, which
times a raw write to /dev/mmcblk1 with the same clock (report 136).
"""
import os
import select
import sys
import termios
import time
import tty

PAT = bytes((i * 7 + 0x5A) & 0xFF for i in range(65536))


def read_until(fd, needle, timeout):
    buf = b""
    end = time.time() + timeout
    while needle not in buf:
        left = end - time.time()
        if left <= 0:
            raise SystemExit("timeout waiting for %r; got %r" % (needle, buf[-400:]))
        r, _, _ = select.select([fd], [], [], left)
        if not r:
            continue
        b = os.read(fd, 4096)
        if not b:
            raise SystemExit("device closed the link")
        buf += b
    return buf


def write_all(fd, data):
    view = memoryview(data)
    while view:
        n = os.write(fd, view)
        view = view[n:]


def main():
    args = sys.argv[1:]
    dev = "/dev/ttyACM0"
    chunk = 65536
    if "--dev" in args:
        i = args.index("--dev")
        dev = args[i + 1]
        del args[i:i + 2]
    if "--chunk" in args:
        i = args.index("--chunk")
        chunk = int(args[i + 1], 0)
        del args[i:i + 2]
    if len(args) != 1:
        raise SystemExit(__doc__)
    size = int(args[0], 0)

    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[3] &= ~(termios.ECHO | termios.ICANON | termios.ISIG)
    termios.tcsetattr(fd, termios.TCSANOW, attrs)

    print("sink: len %d (%s)" % (size, dev))
    write_all(fd, ("sink %d\n" % size).encode())
    read_until(fd, b"sink: ready", 15)

    sent = 0
    t0 = time.time()
    while sent < size:
        n = min(chunk, size - sent)
        if n <= len(PAT):
            data = PAT[:n]
        else:
            data = (PAT * (n // len(PAT) + 1))[:n]
        write_all(fd, data)
        sent += n
    out = read_until(fd, b"sink: done", 60)
    dt = time.time() - t0
    for ln in out[out.find(b"sink: done"):].decode("latin1").splitlines()[:2]:
        print("  device:", ln.strip())
    print("host: sent %d bytes in %.3f s (%.0f KB/s)" % (sent, dt, sent / dt / 1000 if dt else 0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
