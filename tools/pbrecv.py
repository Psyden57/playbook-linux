#!/usr/bin/env python3
"""Stream a file to the PlayBook console's `recv` command (raw media write).

Usage:
    tools/pbrecv.py <byteoff-hex> <file> [--dev /dev/ttyACM0]

The console (initramfs/console.c, `recv`) prints "recv: ready", reads exactly
the announced number of bytes from /dev/ttyGS0 and writes them to
/dev/mmcblk1 at the given byte offset, then prints the FNV-1a64 it computed.
This tool sends the file, prints the device's hash and compares it with the
hash of the bytes it sent.  Used by the hd3-shrink and rootfs work (report
135); the offset/len arithmetic belongs in Python, never in your head.
"""
import os
import select
import sys
import termios
import time
import tty

MASK = (1 << 64) - 1
CHUNK = 1 << 16


def fnv1a64(buf, h=0xCBF29CE484222325):
    for b in buf:
        h ^= b
        h = (h * 0x100000001B3) & MASK
    return h


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
    """Blocking writes: the tty/USB layer applies backpressure by itself, and
    select() on this ACM node stalls when the device briefly stops reading
    (observed with a 16 MiB payload)."""
    view = memoryview(data)
    while view:
        n = os.write(fd, view)
        view = view[n:]


def main():
    args = sys.argv[1:]
    dev = "/dev/ttyACM0"
    if "--dev" in args:
        i = args.index("--dev")
        dev = args[i + 1]
        del args[i:i + 2]
    if len(args) != 2:
        raise SystemExit(__doc__)
    off = int(args[0], 16)
    path = args[1]
    size = os.path.getsize(path)

    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[3] &= ~(termios.ECHO | termios.ICANON | termios.ISIG)
    termios.tcsetattr(fd, termios.TCSANOW, attrs)

    print("recv: off 0x%x len %d (%s)" % (off, size, path))
    write_all(fd, ("recv %x %d\n" % (off, size)).encode())
    read_until(fd, b"recv: ready", 15)
    print("device ready; sending ...")

    h = 0xCBF29CE484222325
    sent = 0
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            h = fnv1a64(b, h)
            write_all(fd, b)
            sent += len(b)
            if sent % (256 << 20) < (1 << 20):
                print("  sent %d/%d MiB" % (sent >> 20, size >> 20))

    out = read_until(fd, b"recv: done", 300)
    tail = out[out.find(b"recv: done"):].decode("latin1").splitlines()
    for ln in tail[:4]:
        print("  device:", ln.strip())
    print("host fnv1a64 %016x  sent %d bytes" % (h, sent))
    return 0


if __name__ == "__main__":
    sys.exit(main())
