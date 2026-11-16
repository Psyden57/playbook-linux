#!/usr/bin/env python3
"""Capture /dev/input/event0 (16-byte input_event, 32-bit ARM timeval).

Usage: evcap.py [seconds] [outfile]
Prints every event with an absolute monotonic timestamp; also returns SYN
frames so contact boundaries are visible.
"""
import select
import struct
import sys
import time

SECS = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0
OUT = sys.argv[2] if len(sys.argv) > 2 else "/tmp/ev.txt"
DEV = "/dev/input/event0"

TYPES = {0: "SYN", 1: "KEY", 2: "REL", 3: "ABS", 4: "MSC"}
ABS = {
    0x00: "ABS_X",
    0x01: "ABS_Y",
    0x2f: "MT_SLOT",
    0x30: "MT_TOUCH_MAJOR",
    0x31: "MT_TOUCH_MINOR",
    0x35: "MT_POSITION_X",
    0x36: "MT_POSITION_Y",
    0x39: "MT_TRACKING_ID",
    0x37: "MT_TOOL_Y",
}
KEY = {0x14a: "BTN_TOUCH", 0x145: "BTN_TOOL_FINGER"}


def name(t, c):
    if t == 3:
        return ABS.get(c, "ABS_0x%x" % c)
    if t == 1:
        return KEY.get(c, "KEY_0x%x" % c)
    if t == 0:
        return {0: "SYN_REPORT", 2: "SYN_MT_REPORT"}.get(c, "SYN_%d" % c)
    return "%s_0x%x" % (TYPES.get(t, str(t)), c)


def main():
    t0 = time.monotonic()
    out = open(OUT, "w", buffering=1)
    fd = open(DEV, "rb")
    print("t0=%.6f dev=%s secs=%.1f" % (t0, DEV, SECS))
    out.write("# t0=%.6f dev=%s\n" % (t0, DEV))
    n = 0
    while time.monotonic() - t0 < SECS:
        r, _, _ = select.select([fd], [], [], 0.2)
        if not r:
            continue
        b = fd.read(16)
        if len(b) < 16:
            break
        sec, usec, typ, code, val = struct.unpack("<iiHHi", b)
        t = time.monotonic() - t0
        line = "t=%9.4f %-16s %6d" % (t, name(typ, code), val)
        out.write(line + "\n")
        print(line)
        n += 1
    out.close()
    print("done %d events" % n)


if __name__ == "__main__":
    main()
