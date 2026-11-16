#!/usr/bin/env python3
"""pb-touchwake — restore catacomb's DPMS-off panel on a touch.

The Munin touch still delivers evdev events with the panel blanked, but
swayidle's `resume` action does not fire for touch on this unit (trap 298).
Watch /dev/input/event0 for BTN_TOUCH down and run `catacomb msg dpms on`
whenever the backlight is off.  Runs as the session user from catacomb's
initrc; multiple readers on an evdev node are fine.
"""
import os
import struct
import subprocess
import sys
import time

DEV = "/dev/input/event0"
BL = "/sys/class/backlight/backlight/bl_power"
EV_KEY = 1
BTN_TOUCH = 0x14A
EVENT = struct.Struct("<IiHHi")  # 32-bit ARM input_event: 16 bytes

last = 0.0

def panel_off():
    try:
        with open(BL) as f:
            return f.read().strip() != "0"
    except OSError:
        return False

def wake():
    global last
    now = time.monotonic()
    if now - last < 1.0:
        return
    last = now
    subprocess.run(["catacomb", "msg", "dpms", "on"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def main():
    f = open(DEV, "rb")
    while True:
        b = f.read(EVENT.size)
        if len(b) < EVENT.size:
            break
        _sec, _usec, etype, code, value = EVENT.unpack(b)
        if etype == EV_KEY and code == BTN_TOUCH and value == 1 and panel_off():
            wake()

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
