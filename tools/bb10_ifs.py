#!/usr/bin/env python3
"""Build a compressed BB10 IFS container by splicing a new imagefs payload.

WHY THIS EXISTS
  The BB10 IFS stores its whole imagefs as a chain of independently compressed
  NRV2B blocks guarded by RIM's ARM decompressor stub.  Rebuilding the imagefs
  changes that payload, but RIM's stub, the 0x100 startup_header and the
  QNXH-OS-1 signature block must be preserved.  This tool compresses a new
  imagefs (see tools/ucl_nrv2b.py) and splices it in, patching exactly the
  fields that describe the payload.

FORMAT (reports 171/172, measured on 10.0.9.1103 and 10.3.03.2049)
  H = 0x808 (startup_header)
  payload      = [H + startup_size, H + stored_size - 8)
                 = [u16 BE compressed length][NRV2B block] *, zero-padded to 4
  trailer8     = 00000000 + LE32(-sum of the payload's LE32 words)
  after it     = RIM's "QNXH-OS-1" signature block (kept verbatim)
  patch        : stored_size (H+0x24) = startup_size + C + 8
                 imagefs_size (H+0x2c) = uncompressed imagefs length
                 ram_size    (H+0x1c) = startup_size + imagefs_size
                 startup trailer (H+startup_size-4) =
                     -(sum of LE32 words over [H, H+startup_size-4))

USAGE
  tools/bb10_ifs.py splice --stock STOCK.ifs --imagefs NEW.ifs --out OUT.ifs
                           [--imagefs-offset HEX] [--header-offset HEX]
  tools/bb10_ifs.py verify OUT.ifs

`--imagefs` is usually the container emitted by tools/ifs_build.py; its imagefs
starts at the stock payload offset by default (0x86910 for these images).
READ-ONLY with respect to its inputs.
"""

import argparse
import struct
import sys

import ucl_nrv2b


def parse(d, H):
    if d[H:H + 4] != b"\xeb\x7e\xff\x00":
        raise SystemExit("no startup header signature at 0x%x" % H)
    ss = struct.unpack_from("<I", d, H + 0x20)[0]
    st = struct.unpack_from("<I", d, H + 0x24)[0]
    ifp = struct.unpack_from("<I", d, H + 0x28)[0]
    ifs = struct.unpack_from("<I", d, H + 0x2c)[0]
    rs = struct.unpack_from("<I", d, H + 0x1c)[0]
    flags1 = d[H + 6]
    return dict(ss=ss, st=st, ifp=ifp, ifs=ifs, rs=rs, flags1=flags1)


def sum32(buf):
    n = len(buf) // 4
    return sum(struct.unpack_from("<%dI" % n, buf, 0)) & 0xFFFFFFFF


def splice(stock_path, imagefs_path, out_path, H=0x808, imagefs_offset=None):
    d = bytearray(open(stock_path, "rb").read())
    h = parse(d, H)
    start = H + h["ss"]
    if imagefs_offset is None:
        imagefs_offset = start
    src = open(imagefs_path, "rb").read()
    imagefs = src[imagefs_offset:]
    if not imagefs.startswith(b"imagefs"):
        raise SystemExit("imagefs at 0x%x does not start with 'imagefs'" % imagefs_offset)
    declared = struct.unpack_from("<I", imagefs, 8)[0]
    if declared != len(imagefs):
        print("warning: imagefs header image_size 0x%x != length 0x%x"
              % (declared, len(imagefs)), file=sys.stderr)

    stream = ucl_nrv2b.compress_stream(imagefs)
    pad = (-len(stream)) % 4
    payload = stream + b"\x00" * pad
    trailer = (-sum32(payload)) & 0xFFFFFFFF

    end = H + h["st"]
    out = bytearray()
    out += d[:start]
    out += payload
    out += struct.pack("<II", 0, trailer)
    out += d[end:]

    # patch the startup header
    struct.pack_into("<I", out, H + 0x1c, h["ss"] + len(imagefs))       # ram_size
    struct.pack_into("<I", out, H + 0x24, h["ss"] + len(payload) + 8)   # stored_size
    struct.pack_into("<I", out, H + 0x2c, len(imagefs))                 # imagefs_size

    # startup trailer over [H, H+ss-4)
    tpos = H + h["ss"] - 4
    s = sum32(out[H:tpos])
    struct.pack_into("<I", out, tpos, (-s) & 0xFFFFFFFF)

    if out[H + 6] & 0x0C != 0x0C:
        raise SystemExit("stock flags1 0x%02x does not select UCL" % out[H + 6])

    with open(out_path, "wb") as f:
        f.write(out)
    print("splice: imagefs %d -> payload %d (%.3f), IFS %d -> %d"
          % (len(imagefs), len(payload), len(payload) / max(1, len(imagefs)),
             len(d), len(out)))
    return len(out)


def verify(path, H=0x808):
    d = open(path, "rb").read()
    h = parse(d, H)
    start = H + h["ss"]
    end = H + h["st"]
    pos = start
    out = bytearray()
    blocks = 0
    while pos < end - 9:
        n = struct.unpack_from(">H", d, pos)[0]
        if n == 0:
            break
        pos += 2
        out += ucl_nrv2b.decode(d[pos:pos + n])
        pos += n
        blocks += 1
    payload = d[start:end - 8]
    trailer = struct.unpack_from("<I", d, end - 4)[0]
    ok_sum = ((-sum32(payload)) & 0xFFFFFFFF) == trailer
    ok_len = len(out) == h["ifs"]
    ok_sig = out.startswith(b"imagefs")
    ok_qnxh = d[end:end + 8] == b"QNXH-OS-"
    ss_ok = sum32(d[H:H + h["ss"]]) == 0
    print("verify: blocks=%d decomp=%d imagefs_size=%d imagefs_ok=%s trailer_ok=%s "
          "qnxh=%s startup_sum0=%s" % (blocks, len(out), h["ifs"], ok_len and ok_sig,
                                       ok_sum, ok_qnxh, ss_ok))
    return 0 if (ok_len and ok_sig and ok_sum and ok_qnxh and ss_ok) else 1


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("splice")
    s.add_argument("--stock", required=True)
    s.add_argument("--imagefs", required=True)
    s.add_argument("--out", required=True)
    s.add_argument("--header-offset", default="0x808")
    s.add_argument("--imagefs-offset", default=None)
    v = sub.add_parser("verify")
    v.add_argument("image")
    v.add_argument("--header-offset", default="0x808")
    a = ap.parse_args()
    if a.cmd == "splice":
        off = int(a.imagefs_offset, 0) if a.imagefs_offset else None
        splice(a.stock, a.imagefs, a.out, int(a.header_offset, 0), off)
        return 0
    return verify(a.image, int(a.header_offset, 0))


if __name__ == "__main__":
    sys.exit(main())
