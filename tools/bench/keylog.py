#!/usr/bin/env python3
"""Log raw bytes delivered on stdin (run inside the terminal under test).

Usage: keylog.py [seconds] [outfile]
Sets the tty to raw mode (no echo, no line editing) and timestamps every
read, so key repeat is visible as repeated bytes.
"""
import os
import select
import sys
import termios
import time
import tty

SECS = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
OUT = sys.argv[2] if len(sys.argv) > 2 else "/tmp/keys.log"

fd = 0
old = termios.tcgetattr(fd)
out = open(OUT, "w", buffering=1)
tty.setraw(fd)
t0 = time.time()
out.write("# t0=%.6f secs=%.1f\n" % (t0, SECS))
try:
    while time.time() - t0 < SECS:
        r, _, _ = select.select([fd], [], [], 0.2)
        if not r:
            continue
        b = os.read(fd, 64)
        if not b:
            break
        out.write("%.4f %s\n" % (time.time() - t0, b.hex()))
finally:
    termios.tcsetattr(fd, termios.TCSANOW, old)
    out.close()
