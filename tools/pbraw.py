#!/usr/bin/env python3
"""Raw diagnostic for /dev/ttyACM0: dump every byte received, with timestamps.

No marker matching, no drain, no timeout heuristics -- it just shows what the
device actually transmits so a "wedged" console can be told apart from
(a) a shell stuck in a foreground command, (b) a dead shell with the tty line
discipline still echoing, (c) a broken host-side read path.

Usage: pbraw.py [seconds] [hexbytes-to-send-first]
  pbraw.py 8            just listen
  pbraw.py 8 0a         send one newline, then listen
  pbraw.py 8 03         send Ctrl-C, then listen
"""
import os
import select
import sys
import termios
import time

DEV = os.environ.get("PBCON_DEV", "/dev/ttyACM0")


def open_port(dev):
    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0
    a[1] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[3] = 0
    a[4] = termios.B115200
    a[5] = termios.B115200
    a[6][termios.VMIN] = 0
    a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd


def main(argv):
    secs = float(argv[0]) if argv else 8.0
    send = bytes.fromhex(argv[1]) if len(argv) > 1 else b""

    fd = open_port(DEV)
    t0 = time.time()

    if send:
        n = os.write(fd, send)
        print("[%6.2f] SENT %s (%d bytes)" % (time.time() - t0, send.hex(), n))

    total = 0
    while time.time() - t0 < secs:
        r, _, _ = select.select([fd], [], [], 0.2)
        if not r:
            continue
        try:
            b = os.read(fd, 4096)
        except OSError as e:
            print("[%6.2f] READ ERROR %s" % (time.time() - t0, e))
            break
        if not b:
            print("[%6.2f] EOF" % (time.time() - t0))
            break
        total += len(b)
        print("[%6.2f] RECV %5d %r" % (time.time() - t0, len(b), b))

    print("total %d bytes in %.1fs" % (total, secs))
    os.close(fd)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
