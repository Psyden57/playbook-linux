#!/usr/bin/env python3
"""Send 0x80EF then 0xEF to an enumerated ramloader to force a reset."""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bbusb as B

for attempt in range(3):
    bb = B.BBUSB()
    if not bb.open([0x8001], timeout_ms=2000):
        print('no ramloader enumerated')
        sys.exit(0)
    print('opened ramloader id=0x%x (attempt %d)' % (bb.id, attempt + 1))
    for op in (0x80EF, 0xEF, 0x80EF):
        try:
            cmd, data = bb.channel2(op, b'', timeout=800)
            print('  0x%X -> 0x%X %s' % (op, cmd, data.hex()))
        except Exception as e:
            print('  0x%X -> %s' % (op, e))
        time.sleep(0.4)
    bb.close()
    time.sleep(1.0)
