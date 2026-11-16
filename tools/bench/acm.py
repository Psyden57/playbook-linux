#!/usr/bin/env python3
"""Raw ACM console driver for the Nura root (trap 274: prompt is playbook:~#).

Paced write + marker read.  One command per invocation; never run two at once.
Usage: acm.py '<shell command>' [timeout]
"""
import os
import select
import sys
import termios
import time

DEV = os.environ.get("PBCON_DEV", "/dev/ttyACM0")


def open_port():
    fd = os.open(DEV, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
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


def pump(fd, until, timeout):
    buf = b""
    t0 = time.time()
    while time.time() - t0 < timeout:
        r, _, _ = select.select([fd], [], [], 0.2)
        if not r:
            continue
        try:
            b = os.read(fd, 65536)
        except OSError:
            break
        if not b:
            break
        buf += b
        if until and until in buf:
            break
    return buf


def main(argv):
    if not argv:
        sys.stderr.write(__doc__)
        return 2
    cmd = argv[0]
    timeout = float(argv[1]) if len(argv) > 1 else 20.0

    fd = open_port()
    pump(fd, None, 0.3)  # drain whatever is pending

    marker = "ZZ%d" % (int(time.time() * 1000) % 1000000)
    payload = ("%s\recho %s:$?\r" % (cmd, marker)).encode()
    for ch in payload:
        try:
            os.write(fd, bytes([ch]))
        except OSError:
            break
        time.sleep(0.003)

    out = pump(fd, marker.encode(), timeout)
    out += pump(fd, None, 0.5)
    sys.stdout.write(out.decode("utf-8", "replace"))
    os.close(fd)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
