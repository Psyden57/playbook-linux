#!/usr/bin/env python3
"""Read-only loader probe for the spare PlayBook.

Waits for the bootrom (0fca:0001), uploads LDR_01, then issues only read-only
channel-2 queries: 0xB1 (block size), 0xB4 (flash regions), 0xD9 (MCT),
0xDC (range hash of the region-0 IFS), 0xDE (boot mode).  Nothing is written
to eMMC, NVRAM or RPMB.

Run it just before rebooting the unit into the bootrom, e.g.:
    python3 probe_loader.py &
    ssh ... '/proc/boot/shutdown -S reboot'
"""
import os, sys, time, struct, zlib, json

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usb.core
import bbusb as B

OUT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + '/local/research/loader-probe'
os.makedirs(OUT, exist_ok=True)


def log(msg):
    print(time.strftime('%H:%M:%S'), msg, flush=True)


def wait_bootrom(timeout=120):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            if usb.core.find(idVendor=0x0fca, idProduct=0x0001) is not None:
                return True
        except Exception:
            pass
        time.sleep(0.1)
    return False


def parse(raw):
    """raw is the payload returned by read_data(): [u16 size][u16 cmd][data][u32 crc]."""
    if len(raw) < 8:
        return None
    size = struct.unpack_from('<H', raw, 0)[0]
    cmd = struct.unpack_from('<H', raw, 2)[0]
    dlen = max(0, size - 8)
    data = raw[4:4 + dlen]
    return cmd, data, size


def collect(bb, op, data=b'', settle=0.5, total=3.0, timeout=200):
    size = len(data) + 8
    pkt = bytearray(size)
    struct.pack_into('<H', pkt, 0, size)
    struct.pack_into('<H', pkt, 2, op)
    if data:
        pkt[4:4 + len(data)] = data
    crc = zlib.crc32(bytes(pkt[:len(data) + 4])) & 0xffffffff
    struct.pack_into('<I', pkt, len(data) + 4, crc)
    bb.send_data(2, bytes(pkt), timeout=2000)

    frames = []
    t0 = time.time()
    last = time.time()
    while time.time() - t0 < total:
        try:
            ch, raw = bb.read_data(timeout=timeout)
        except Exception:
            raw = b''
        if raw:
            frames.append(raw)
            last = time.time()
        elif time.time() - last > settle:
            break
    return frames


def save(name, op, frames):
    blob = b''.join(frames)
    open(os.path.join(OUT, name + '.bin'), 'wb').write(blob)
    with open(os.path.join(OUT, name + '.txt'), 'w') as f:
        f.write('op=0x%02X frames=%d bytes=%d\n' % (op, len(frames), len(blob)))
        for i, raw in enumerate(frames):
            p = parse(raw)
            if p:
                cmd, data, size = p
                f.write('frame %d: size=0x%X cmd=0x%X data=%d\n' % (i, size, cmd, len(data)))
                f.write('  hex: %s\n' % data[:256].hex())
            else:
                f.write('frame %d: short raw=%s\n' % (i, raw.hex()))
    log('%s: %d frame(s), %d bytes' % (name, len(frames), len(blob)))


def main():
    log('waiting for bootrom 0fca:0001 ...')
    if not wait_bootrom():
        log('bootrom not found (timeout). Aborting; nothing was sent.')
        return 2
    log('bootrom present; opening/uploading LDR_01')

    bb = B.BBUSB()
    ok = B._open_ramloader(bb, B.DEFAULT_LOADERS, True, None,
                           password='0000', debug=True)
    if not ok:
        log('loader init failed. Power-cycle the unit and retry.')
        bb.close()
        return 3
    log('loader ready; running read-only probes')

    plan = [
        ('b1_blocksize', 0xB1, b''),
        ('b4_flashregions', 0xB4, b''),
        ('d9_mct', 0xD9, b''),
        ('dc_ifs_crc', 0xDC, struct.pack('<II', 0x825, 0x2C)),
        ('de_bootmode', 0xDE, struct.pack('<II', 0, 0)),
    ]
    summary = {}
    for name, op, data in plan:
        frames = collect(bb, op, data)
        save(name, op, frames)
        if frames:
            parsed = [parse(f) for f in frames]
            parsed = [p for p in parsed if p]
            if parsed:
                cmd, pdata, size = parsed[0]
                summary[name] = {'op': op, 'cmd': cmd, 'len': len(pdata),
                                 'hex': pdata[:64].hex()}
                log('  -> cmd=0x%X len=%d %s' % (cmd, len(pdata), pdata[:32].hex()))
            else:
                summary[name] = {'op': op, 'cmd': None}
        else:
            summary[name] = {'op': op, 'cmd': None, 'error': 'no response'}

    with open(os.path.join(OUT, 'summary.json'), 'w') as f:
        json.dump(summary, f, indent=2)
    log('summary: ' + json.dumps(summary))

    log('rebooting the unit out of the loader')
    try:
        bb.reboot_loader()
    except Exception as e:
        log('reboot_loader: %s' % e)
    try:
        bb.close()
    except Exception:
        pass
    return 0


if __name__ == '__main__':
    sys.exit(main())
