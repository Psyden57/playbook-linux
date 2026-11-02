#!/usr/bin/env python3
"""Pre-OS IFS flash tool for the PlayBook, using the signed ramloader's 0xF7 path.

Path 1 (the live MCT has no 0x39 entries):
    dest_block = MCT 0x34 (MBR) start + rrcq.offset
For region 0 IFS: block 0x825 -> offset 0x20, Count 0x2C (44 x 64 KiB).

Modes:
  --build-test      build a 3-block pattern at block 0x8D0 (IFS padding, safe)
  --template-ifs IMG  build the sealed IFS image at block 0x825 (44 blocks)
  --payload IMG --dest-raw HEX   write an arbitrary block-aligned payload to a
                    raw device offset (region 1 only unless --allow-region0)
  --dry-run         build + print the plan, do not touch USB
  --keep-loader     do not reset the loader after the write (chain a second run
                    with --reuse-loader)
  (default)         wait for bootrom, upload LDR_01, flash the built container

The destination is a *loader* 64 KiB block; raw mmcblk1 byte = (block-0x15)*0x10000
(report 134 §5).  --dest-raw does that conversion in Python, because the
project rule is that no address is computed mentally.

Container shape (learned in session 18, see report 139): the loader on this
unit uses the **V1 `qcfp`** run record, whose `offset` is *region-relative*
(the vendor `.lst` uses the same units: `ifs=16` = 0x100000, `rcfs=192` =
0xC00000; the loader adds the region start 0x815).  A V2-only container from
`bb10mt pack -v 2` is rejected with status `0x11`; vendor autoloaders are built
with `-v 1,2`.  This tool clones the vendor hybrid template and patches both
records.

Nothing is written until the container is built and the gate is checked host-side.
"""
import os, sys, re, struct, zlib, time, subprocess, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usb.core
import bbusb as B

BB10MT = ROOT + '/local/vendor/bb10mt/bb10mt'
QCFM_PAS = ROOT + '/local/vendor/bb10mt-main/qcfm.pas'
TEMPLATE = os.environ.get('PB_IFS_TEMPLATE',
                          ROOT + '/local/images/ifs_template.signed')
BASE_IMG = ROOT + '/local/images/ifs0.sealed.bin'
BS = 0x10000
MBR_START = 0x805
IFS_BLOCK = 0x825
SAFE_BLOCK = 0x8D0
SIG_LEN = 560

# Measured MCT extents (report 98 §4, 64 GB unit) in loader 64 KiB blocks.
# The loader's only address gate is "inside one of these"; see report 101.
OS_LO, OS_HI = 0x815, 0x4804      # 0x1B OS Fixed -- both QNX regions
R1_LO, R1_HI = 0x2815, 0x4804     # 0x35 id 1 -- region 1 = /dev/hd2
REGION0_START = 0x815            # base the V1 qcfp offsets are relative to
RAW_SHIFT = 0x15                 # raw byte = (block - 0x15) * 0x10000


def raw_to_block(raw):
    if raw % BS:
        raise SystemExit('raw offset 0x%X is not 64 KiB aligned' % raw)
    return raw // BS + RAW_SHIFT


def block_to_raw(block):
    return (block - RAW_SHIFT) * BS


def log(msg):
    print(time.strftime('%H:%M:%S'), msg, flush=True)


def load_signature():
    txt = open(QCFM_PAS, 'r', errors='ignore').read()
    m = re.search(r'signature_data:\s*array\[0\.\.559\]\s*of\s*byte\s*=\s*\((.*?)\);', txt, re.S)
    if not m:
        raise SystemExit('signature_data not found in qcfm.pas')
    vals = [int(x, 16) for x in re.findall(r'\$([0-9A-Fa-f]{2})', m.group(1))]
    if len(vals) != SIG_LEN:
        raise SystemExit('signature_data has %d bytes, expected %d' % (len(vals), SIG_LEN))
    return bytes(vals)


def build_container(payload, block_offset):
    """Compatibility wrapper used by --build-test.

    Session 18: a container built with `bb10mt pack -v 2` has a V1 `mfcq`
    header but no V1 `qcfp` block, and this loader rejects its first frame with
    status `0x11`.  Vendor autoloaders are built with **`-v 1,2`** (the
    operator's own recipe); the template below is exactly that hybrid shape.
    Delegate so every payload goes through one proven path.
    """
    with tempfile.NamedTemporaryFile(suffix='.bin') as f:
        f.write(payload)
        f.flush()
        return build_from_template(f.name, block_offset + MBR_START)


def build_from_template(image_path, dest_block):
    """Wrap an arbitrary payload in the vendor IFS template's container.

    Two things were learned the hard way (session 18) and are encoded here:

    * `bb10mt pack -v 2` emits a V1 `mfcq` header with **no `qcfp` block**, and
      the loader rejects that first frame with status `0x11`.
    * on this unit the loader uses the **V1 `qcfp` run record**, not the V2
      `rrcq`.  Its `offset` is *region-relative*: the vendor `.lst` uses the
      same units (`ifs=16` -> region offset 0x100000, `rcfs=192` -> 0xC00000),
      and the loader adds the region start 0x815.  Patching only the V2 record
      left this one at 0x10, so the write landed at region-0 block 0x825.

    Both records are patched, so it is correct whether a loader prefers V1 or
    V2.  The loader only parses the container, never hashes it (report 98 §6),
    so the payload CRC fields are not consulted.
    """
    if not os.path.exists(TEMPLATE):
        raise SystemExit(
            'IFS template not found: %s\n'
            'Set PB_IFS_TEMPLATE to the vendor signed IFS container '
            '(2.0.0.4869-IFS.0.signed) and re-run.' % TEMPLATE)
    d = open(image_path, 'rb').read()
    tpl = open(TEMPLATE, 'rb').read()
    hdr = struct.unpack_from('<I', tpl, 0x10)[0]
    count = (len(d) + BS - 1) // BS
    payload = d + b'\x00' * (count * BS - len(d))
    t = bytearray(tpl[:hdr] + payload + tpl[-8:])
    i = t.find(b'rrcq')
    q = t.find(b'qcfp')
    if i < 0 or q < 0:
        raise SystemExit('template is missing qcfp/rrcq')
    v1_off = dest_block - REGION0_START       # TControlFileV1 run record lives here
    v2_off = dest_block - MBR_START           # TControlFileV2 rrcq (report 98)
    struct.pack_into('<i', t, q + 0x28, v1_off)   # V1 run0.offset
    struct.pack_into('<I', t, q + 0x2C, count)    # V1 run0.Count
    dc = zlib.crc32(bytes(t[q + 0x28:q + 0x40])) & 0xffffffff
    struct.pack_into('<I', t, q + 0x1C, dc)       # datachecksum
    cs = zlib.crc32(bytes(t[q + 0x08:q + 0x40])) & 0xffffffff
    struct.pack_into('<I', t, q + 0x04, cs)       # checksum
    struct.pack_into('<I', t, i + 8, v2_off)      # V2 rrcq.offset
    struct.pack_into('<I', t, i + 12, count)      # V2 rrcq.Count
    log('template container: %d bytes, dest block 0x%X, V1 offset 0x%X, '
        'V2 offset 0x%X, count %d'
        % (len(t), dest_block, v1_off, v2_off, count))
    return bytes(t), count


def gate_check(block, count, region1_only=False):
    """Refuse anything the loader's allow-list would reject, before USB.

    The loader tests the destination against the MCT extents 0x34/0x1B/0x1C
    (report 98 §4); region 1 lives inside 0x1B.  A generic payload is further
    restricted to region 1, so a mistyped offset cannot land in region 0's IFS,
    the MBR or the NVRAM block.
    """
    last = block + count - 1
    inside_os = OS_LO <= block and last <= OS_HI
    inside_r1 = R1_LO <= block and last <= R1_HI
    ok = inside_os and (inside_r1 if region1_only else True)
    log('gate: block 0x%X..0x%X (raw 0x%X..0x%X) vs 0x1B 0x%X..0x%X%s -> %s'
        % (block, last, block_to_raw(block), block_to_raw(last) + BS - 1,
           OS_LO, OS_HI, ' (region 1 only)' if region1_only else '',
           'OK' if ok else 'REJECT'))
    if not ok:
        raise SystemExit('host-side gate check failed; refusing')


def preflash(bb, val=0x15):
    """TBBLoader.PreFlash: opcode 0x20EE, 36-byte descriptor, response 0x39."""
    data = bytearray(36)
    data[0] = val
    data[1] = 0x28
    data[6] = 0x02
    data[28] = 0x01
    data[32] = 0x02
    cmd, resp = bb.channel2(0x20EE, bytes(data), timeout=15000)
    log('PreFlash 0x20EE -> cmd 0x%X len %d' % (cmd, len(resp)))
    if cmd not in (0x39, 0x20DE):
        raise SystemExit('PreFlash failed (cmd 0x%X)' % cmd)


def send_frames(bb, container):
    seq = 0
    off = 0
    total = len(container)
    while off < total:
        chunk = container[off:off + 0x3FEC]
        payload = struct.pack('<II', seq, len(chunk)) + chunk
        cmd, data = bb.channel2(0xF7, payload, timeout=5000)
        if cmd != 0xDF:
            raise SystemExit('0xF7 seq %d -> cmd 0x%X data %s' % (seq, cmd, data[:8].hex()))
        off += len(chunk)
        seq += 1
        if seq % 8 == 0 or off == total:
            log('  %d/%d bytes, seq %d' % (off, total, seq - 1))
    return seq


def wait_bootrom(timeout=300):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            if usb.core.find(idVendor=0x0fca, idProduct=0x0001) is not None:
                return True
        except Exception:
            pass
        time.sleep(0.1)
    return False


def _loader_present(pid=0x8001):
    try:
        return usb.core.find(idVendor=0x0fca, idProduct=pid) is not None
    except Exception:
        return False


def leave_loader(bb, attempts=3):
    """Reset the unit out of the ramloader after the write.

    The single 0x80EF on the flash handle is not reliable after the
    COMPLETE/0x4015 exchange: it has often returned without resetting, leaving
    the unit in 0fca:8001 so the operator had to run force_reset_loader.py by
    hand.  Retry on a fresh handle with the same 0x80EF/0xEF/0x80EF sequence,
    and report the outcome instead of discarding it.
    """
    try:
        ok = bb.reboot_loader()
        log('reboot_loader on the flash handle: %s' % ('ok' if ok else 'rejected'))
    except Exception as e:
        log('reboot_loader on the flash handle: %s' % e)
    try:
        bb.close()
    except Exception:
        pass

    if not _loader_present():
        return
    for attempt in range(1, attempts + 1):
        b2 = B.BBUSB()
        if not b2.open([0x8001], timeout_ms=2000):
            log('ramloader not enumerated (attempt %d)' % attempt)
            if not _loader_present():
                return
            continue
        log('opened ramloader id=0x%x (attempt %d)' % (b2.id, attempt))
        try:
            for op in (0x80EF, 0xEF, 0x80EF):
                try:
                    cmd, data = b2.channel2(op, b'', timeout=800)
                    log('  loader reset 0x%X -> 0x%X' % (op, cmd))
                except Exception as e:
                    log('  loader reset 0x%X -> %s' % (op, e))
                time.sleep(0.4)
        finally:
            b2.close()
        time.sleep(1.0)
        if not _loader_present():
            log('loader reset accepted')
            return
    log('loader still enumerated; run tools/force_reset_loader.py manually')


def main():
    args = sys.argv[1:]
    dry = '--dry-run' in args
    template_mode = False
    payload_mode = False
    region1_only = False
    delay = 0
    if '--delay' in args:
        delay = int(args[args.index('--delay') + 1])
    if '--build-test' in args:
        payload = bytes([0x4B, 0x42, 0x4C, 0x42]) * (3 * BS // 4)  # 'KBLB' repeated
        block = SAFE_BLOCK
    elif '--payload' in args:
        idx = args.index('--payload')
        img = args[idx + 1]
        payload_img = img
        d = open(img, 'rb').read()
        if '--dest-block' in args:
            block = int(args[args.index('--dest-block') + 1], 16)
        elif '--dest-raw' in args:
            block = raw_to_block(int(args[args.index('--dest-raw') + 1], 16))
        else:
            raise SystemExit('--payload needs --dest-raw <hex> or --dest-block <hex>')
        count = (len(d) + BS - 1) // BS
        if not count:
            raise SystemExit('empty payload')
        region1_only = '--allow-region0' not in args
        payload_mode = True
        log('payload %s: %d bytes -> %d blocks, dest block 0x%X (raw 0x%X)'
            % (img, len(d), count, block, block_to_raw(block)))
    elif '--template-ifs' in args:
        idx = args.index('--template-ifs')
        img = args[idx + 1]
        d = open(img, 'rb').read()
        if not os.path.exists(TEMPLATE):
            raise SystemExit(
                'IFS template not found: %s\n'
                'Set PB_IFS_TEMPLATE to the vendor signed IFS container '
                '(2.0.0.4869-IFS.0.signed) and re-run.' % TEMPLATE)
        tpl = open(TEMPLATE, 'rb').read()
        hdr = struct.unpack_from('<I', tpl, 0x10)[0]
        if len(d) == 0x2BFE10:
            t = bytearray(tpl)
            t[hdr:hdr + 0x2C0000] = d + b'\x00' * (0x2C0000 - len(d))
            i = t.find(b'rrcq')
            struct.pack_into('<I', t, i + 8, IFS_BLOCK - MBR_START)
            container = bytes(t)
            count = 44
        else:
            count = (len(d) + BS - 1) // BS
            payload = d + b'\x00' * (count * BS - len(d))
            t = bytearray(tpl[:hdr] + payload + tpl[-8:])
            i = t.find(b'rrcq')
            struct.pack_into('<I', t, i + 8, IFS_BLOCK - MBR_START)
            struct.pack_into('<I', t, i + 12, count)
            q = t.find(b'qcfp')
            if q >= 0:
                struct.pack_into('<I', t, q + 0x2C, count)          # run0.Count
                dc = zlib.crc32(bytes(t[q + 0x28:q + 0x40])) & 0xffffffff
                struct.pack_into('<I', t, q + 0x1C, dc)             # datachecksum
                cs = zlib.crc32(bytes(t[q + 0x08:q + 0x40])) & 0xffffffff
                struct.pack_into('<I', t, q + 0x04, cs)             # checksum
            container = bytes(t)
        block = IFS_BLOCK
        log('template container: %d bytes, rrcq at 0x%X, offset 0x%X, count %d'
            % (len(container), i, IFS_BLOCK - MBR_START, count))
        template_mode = True
    else:
        raise SystemExit('use --build-test, --payload IMG --dest-raw HEX, '
                         'or --template-ifs IMG [--dry-run]')

    if payload_mode:
        if block < MBR_START:
            raise SystemExit('destination block 0x%X is before the MBR extent '
                             '(0x%X); refusing' % (block, MBR_START))
    elif not template_mode:
        count = len(payload) // BS
        if block < MBR_START:
            raise SystemExit('destination block 0x%X is before the MBR extent '
                             '(0x%X); refusing' % (block, MBR_START))
    gate_check(block, count, region1_only=region1_only)
    if payload_mode:
        container, count = build_from_template(payload_img, block)
    elif not template_mode:
        container, count = build_container(payload, block - MBR_START)
    where = ('region 1 (hd2)' if block >= R1_LO else 'region 0 (hd1)')
    log('plan: %s, block 0x%X (raw 0x%X), %d blocks, container %d bytes'
        % (where, block, block_to_raw(block), count, len(container)))
    if dry:
        log('dry run: stopping before USB')
        return 0

    if template_mode:
        sig = open(BASE_IMG, 'rb').read()[-SIG_LEN:]
    else:
        sig = load_signature()
    reuse = '--reuse-loader' in args
    if delay:
        log('delaying %ds before the bootrom wait' % delay)
        time.sleep(delay)
    if not reuse:
        log('waiting for bootrom ...')
        if not wait_bootrom():
            raise SystemExit('bootrom not found; nothing sent')
    else:
        log('reusing an already-running loader')
    bb = B.BBUSB()
    if not B._open_ramloader(bb, B.DEFAULT_LOADERS, True, None, password='0000', debug=True):
        raise SystemExit('loader init failed')
    log('loader ready; sending PreFlash')
    preflash(bb, 0x15)
    keep = '--keep-loader' in args
    log('sending container')
    frames = send_frames(bb, container)
    if '--no-sig' not in args:
        log('sending 0x40F9 signature (%d bytes)' % SIG_LEN)
        try:
            cmd, data = bb.channel2(0x40F9, struct.pack('<H', 0x230) + sig, timeout=15000)
            log('0x40F9 -> cmd 0x%X len %d' % (cmd, len(data)))
        except Exception as e:
            log('0x40F9 closed the loader (%s); the write is on media' % e)
            if not keep:
                leave_loader(bb)
            log('done: verify by booting the unit')
            return 0
    log('sending COMPLETE')
    for op in (0x40C0, 0xC0):
        try:
            cmd, data = bb.channel2(op, b'', timeout=60000)
            log('COMPLETE 0x%X -> cmd 0x%X len %d' % (op, cmd, len(data)))
            if cmd in (0x4006, 0x402E, 6):
                break
            if cmd == 0x4015:
                log('COMPLETE rejected (0x4015); the write is on media, boot will verify')
                break
        except Exception as e:
            log('COMPLETE 0x%X failed: %s' % (op, e))
    if keep:
        log('--keep-loader: leaving the loader running for a --reuse-loader run')
        try:
            bb.close()
        except Exception:
            pass
    else:
        log('rebooting out of loader')
        leave_loader(bb)
    log('done (%d frames)' % frames)
    return 0


if __name__ == '__main__':
    sys.exit(main())
