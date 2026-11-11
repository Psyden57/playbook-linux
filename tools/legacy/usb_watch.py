#!/usr/bin/env python3
"""USB timeline watcher for the PlayBook.

Logs every 0fca product-ID transition with a timestamp. Useful because the
USB re-enumeration cadence is a boot-stage signal:

  0fca:0001  bootrom (full-speed)
  0fca:8001  ramloader (high-speed)
  0fca:8010  OS (Connect-to-Windows mode seen earlier)
  0fca:8012  OS / RNDIS
  0fca:8020  OS / mass storage (RIM Disk Device)

Past events are also in the kernel journal:
  journalctl -k --no-pager -n 300 | grep -iE 'usb 3-1|0fca'
Live: this script (no root needed), or `udevadm monitor --kernel --subsystem-match=usb`.
"""
import sys, time
import usb.core

VID = 0x0FCA
NAMES = {
    0x0001: 'bootrom',
    0x8001: 'ramloader',
    0x8010: 'OS/8010',
    0x8012: 'OS/RNDIS',
    0x8020: 'OS/mass-storage',
}


def snapshot():
    try:
        return tuple(sorted({d.idProduct for d in usb.core.find(find_all=True)
                             if d.idVendor == VID}))
    except Exception:
        return ()


def main():
    last = None
    print('watching 0fca devices (Ctrl-C to stop)', flush=True)
    while True:
        cur = snapshot()
        if cur != last:
            now = time.strftime('%H:%M:%S')
            desc = ', '.join('%04x:%s' % (p, NAMES.get(p, '?')) for p in cur) or 'none'
            print('%s  %s' % (now, desc), flush=True)
            last = cur
        time.sleep(0.1)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
