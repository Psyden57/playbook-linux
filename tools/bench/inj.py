#!/usr/bin/env python3
"""Inject a command line into the foot shell's pty via TIOCSTI.

Usage: inj.py "<command>"
"""
import fcntl
import os
import sys

if len(sys.argv) < 2:
    sys.exit("usage: inj.py <command>")

s = (sys.argv[1] + "\r").encode()
fd = os.open("/dev/pts/0", os.O_RDWR)
try:
    for ch in s:
        fcntl.ioctl(fd, 0x5412, bytes([ch]))
finally:
    os.close(fd)
print("injected %d bytes" % len(s))
