#!/usr/bin/env python3
"""Build a CRC-path image: patch a payload into a sealed IFS and re-seal the CRC."""
import argparse, struct, zlib

ap = argparse.ArgumentParser()
ap.add_argument('--base', required=True, help='sealed IFS image (0x2BFE10, os_type=6)')
ap.add_argument('--payload', required=True, help='flat ARM binary linked at 0x80100000')
ap.add_argument('--offset', type=lambda s: int(s, 0), default=0x800)
ap.add_argument('--out', required=True)
a = ap.parse_args()

img = bytearray(open(a.base, 'rb').read())
code = open(a.payload, 'rb').read()
end = len(img)
if len(img) != 0x2BFE10:
    raise SystemExit('base is 0x%X, expected 0x2BFE10' % len(img))
if a.offset + len(code) > 0x2BFE0C:
    raise SystemExit('payload overruns the CRC trailer')
img[a.offset:a.offset + len(code)] = code
crc = zlib.crc32(bytes(img[:end - 4])) & 0xffffffff
struct.pack_into('<I', img, end - 4, crc)
open(a.out, 'wb').write(img)
print('wrote %s: size 0x%X payload 0x%X at 0x%X crc 0x%08X'
      % (a.out, end, len(code), a.offset, crc))
