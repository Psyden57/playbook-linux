#!/usr/bin/env python3
"""Drive the PlayBook USB bring-up console (/dev/ttyACM0 <-> /dev/ttyGS0).

The console stays silent until the first host byte and prints "pb> " after
every command, which is the sync marker.  Usage:

    ./pbcon.py 'help' 'disp stat' 'bl init'

Every argument is sent as one line, in order; the output of each is printed.
Exit status is non-zero if the port could not be opened.
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
    a[0] = 0                                          # iflag: no translation
    a[1] = 0                                          # oflag: no translation
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[3] = 0                                          # lflag: raw, no echo
    a[4] = termios.B115200
    a[5] = termios.B115200
    a[6][termios.VMIN] = 0
    a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd


def read_until(fd, marker, timeout):
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.2)
        if not r:
            continue
        try:
            b = os.read(fd, 4096)
        except OSError:
            break
        if not b:
            break
        buf += b
        if marker and marker in buf:
            break
    return buf


def drain(fd):
    """Discard anything already pending (e.g. a prompt printed before we spoke)."""
    while True:
        r, _, _ = select.select([fd], [], [], 0.05)
        if not r:
            return
        try:
            if not os.read(fd, 4096):
                return
        except OSError:
            return


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    try:
        fd = open_port(DEV)
    except OSError as e:
        print("pbcon: open %s: %s" % (DEV, e), file=sys.stderr)
        return 1

    # Wake the console: it says nothing until it sees a host byte.
    os.write(fd, b"\n")
    read_until(fd, b"pb> ", 5)

    rc = 0
    for cmd in argv:
        drain(fd)
        os.write(fd, cmd.encode() + b"\n")
        out = read_until(fd, b"pb> ", float(os.environ.get("PBCON_TMO", "20")))
        txt = out.decode("utf-8", "replace").replace("\r", "")
        lines = [l for l in txt.split("\n") if l.strip() and l.strip() != "pb>"]
        print("$ " + cmd)
        for l in lines:
            print("  " + l)
        if not out:
            print("  !! no response")
            rc = 1
            break
    os.close(fd)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
