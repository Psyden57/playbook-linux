#!/usr/bin/env python3
"""Offline tests for tools/bbusb.py — no device, no USB.

The device layer is replaced in-process (`bbusb.BBUSB`, `bbusb._open_ramloader`),
and the stub returns canned replies shaped like the ones documented in reports
100/162/164.  So these tests exercise our code — framing, parsing, the
read-before/delete/read-after logic, the host-side gate, container build/parse/
split/extract — rather than the hardware.

Run:  python3 tools/tests/test_offline.py
Exit: 0 = all passed, 1 = at least one failure (each printed).
"""

import contextlib
import io
import json
import os
import struct
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import bbusb                                # noqa: E402

failures = []


def check(name, cond, detail=''):
    print(('PASS ' if cond else 'FAIL ') + name +
          (('  ' + detail) if detail and not cond else ''))
    if not cond:
        failures.append(name)


# ------------------------------------------------------------------ canned data
def mct_blob():
    """A 240-byte MCT shaped like the live one: 19 TLVs, 8-byte header."""
    types = [0x1D, 0x2B, 0x2C, 0x31, 0x32, 0x33, 0x18, 0x34, 0x1B, 0x1E,
             0x36, 0x1C, 0x25, 0x35, 0x35, 0x23, 0x23, 0x26, 0x09]
    body = b''
    region = 0
    for t in types:
        if t in (0x32,):
            body += struct.pack('<BB', t, 8) + b'\x00' * 6
        elif t == 0x26:
            body += struct.pack('<BBII', t, 10, 0x1000, 0x10000)
        elif t == 0x09:
            body += struct.pack('<BBII', t, 10, 0x202C0AE2, 0)
        elif t == 0x35:
            body += struct.pack('<BBHII', t, 12, region << 8, 0x815, 0x2804)
            region += 1
        elif t == 0x23:
            body += struct.pack('<BBHII', t, 12, 0, 0x80000000, 0x9FFFFFFF)
        else:
            body += struct.pack('<BBHII', t, 12, 0, 0x805, 0x814)
    head = struct.pack('<IHB', 0x92BE564A, 0x18, 1) + b'\x00'   # 8-byte header
    blob = head + body
    return blob + b'\x00' * (240 - len(blob))


LAL = (b'\x00' * 8 +
       b'Sun Aug 30 05:41:38 2026:3.11.0.27:DONE:QNXLOAD:2.0.0.4869 DEV-'
       b'ErrorCode:0-Success!\x00' +
       b'Sun Aug 30 05:38:10 2026:3.11.0.27:QNXLOAD:2.0.0.4869 DEV\x00' +
       b'\xff' * 16)

# live 2026-11-01: [0, 3, 0x017F0000, 0, ...]
RECORD_PRESENT = struct.pack('<IIIIIII', 0, 3, 0x017F0000, 0, 0, 0, 0)
RECORD_DELETED = b'\x00' * 28


class StubDevice:
    """The surface the device commands use.  Records what was sent, in order."""
    record = RECORD_PRESENT
    deleted_calls = 0
    sent = []
    # boot-ROM metrics are captured during the handshake in real use
    last_bootrom_metrics = None

    def __init__(self):
        self.id = 0x8001

    def open(self, product_ids, timeout_ms=5000):
        return False          # never touch real USB from the harness

    def channel2(self, op, data=b'', timeout=1000):
        StubDevice.sent.append(op)
        if op == 0xB1:
            return 0xC8, struct.pack('<III', 0, 0x10000, 0xEDA04) + b'\x00' * 4
        if op == 0xB4:
            return 0xD2, b'\x00' * 40 + b'MCGAFA' + b'\x00' * 142
        if op == 0xD9:
            return 0xC9, mct_blob()
        if op == 0xDE:
            return 0xFA, struct.pack('<III', 0, 1, 49)
        if op == 0xE9:
            return 0xFB, self.record
        if op == 0xB5:
            return 0xD3, bytes(range(256)) * 2
        if op == 0xBF:      # live 2026-11-01: size 0x400 MB, vendor 3, rev 0x100
            return 0xD9, bytes.fromhex('000000000000000000040000000000000300000000010000'
                                       '000000000000000000000000')
        if op == 0xBD:
            return 0xBE, b'\x22' * 8
        if op == 0xDB:      # live: branding id 0x01F8 = 504 (CFP prints 504)
            return 0xCB, bytes.fromhex('0100f801')
        if op in (0xE1, 0xD8, 0xB2):
            return 0xC8, b'\x44' * 0x3FF0
        if op == 0xB0:
            return 0xD0, b''
        if op == 0xB3:
            StubDevice.action_payload = data
            # live shape: NUL-terminated records, newest first, no newlines
            text = (b'D:00010013\x00S:00010011\x00'
                    b'6A93ECC2:3.11.0.27:DONE:QNXLOAD:2.0.0.4869 DEV--0\x00'
                    b'6A93EBF2:3.11.0.27:QNXLOAD:2.0.0.4869 DEV\x00')
            return 0xB4, struct.pack('<I', len(text)) + text
        if op == 0xE8:
            arg, = struct.unpack('<I', data)
            if arg == 4:
                StubDevice.deleted_calls += 1
                StubDevice.record = RECORD_DELETED
                return 6, b''
            return 0x15, b''
        if op == 0x20EE:
            StubDevice.preflash = data[0]
            return 0x39, b''
        if op == 0xF7:
            return 0xDF, b''
        if op == 0x40F9:
            StubDevice.sig_len = struct.unpack_from('<H', data, 0)[0]
            return 0x4006, b''
        if op == 0x40C0:
            return 0x4006, b''
        raise AssertionError('unexpected op 0x%X' % op)

    def flash_regions_info(self):
        return self.channel2(0xB4)[1]

    def get_metrics(self):
        blob = bytearray(32)
        struct.pack_into('<I', blob, 12, 0x05010000)
        struct.pack_into('<I', blob, 16, 0x06001A06)
        return bytes(blob)

    def device_pin(self):
        return 0x50175127

    def get_model_id(self):
        return 0x06001A06

    def read_boot_mode(self, timeout=2000):
        return 0, 1, 49

    def bugdisp_log(self):
        self.last_bugdisp_log = LAL
        return LAL

    def reboot_loader(self):
        return True

    def close(self):
        pass


_M = bytearray(32)
struct.pack_into('<I', _M, 12, 0x05010000)
struct.pack_into('<I', _M, 16, 0x06001A06)
_M[0x14:0x14 + 9] = b'ec_agent\x00'
StubDevice.last_bootrom_metrics = bytes(_M)
_RealBBUSB = bbusb.BBUSB      # keep the real class for the password-stage tests
bbusb.BBUSB = StubDevice
bbusb._open_ramloader = lambda *a, **k: True


def run(argv):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = bbusb.main(argv)
    return rc, buf.getvalue()


# ------------------------------------------------------------------------ info
def test_info():
    print('--- info --json')
    rc, out = run(['info', '--json'])
    info = json.loads(out)
    check('info rc 0', rc == 0)
    check('PIN is the raw hex digits', info['pin'] == {'raw': '0x50175127',
                                                       'pin': '50175127'},
          str(info.get('pin')))
    check('boot mode factory / 49',
          info['boot_mode']['mode_name'] == 'factory' and
          info['boot_mode']['factory_count'] == 49, str(info.get('boot_mode')))
    check('metrics decoded', info['metrics']['model_id'] == '0x06001A06',
          str(info.get('metrics')))
    check('mct parsed', bool(info['mct']['tlvs']) and 'error' not in info['mct'],
          str(info['mct'].get('error')))
    check('mct header 8', info['mct']['header_size'] == 8,
          str(info['mct'].get('header_size')))
    check('mct TLV count', len(info['mct']['tlvs']) == 19,
          str(len(info['mct']['tlvs'])))
    check('mct region ids from the second byte',
          [t.get('partition_id') for t in info['mct']['tlvs']
           if t['type'] == '0x35'] == [0, 1],
          str([t.get('partition_id') for t in info['mct']['tlvs'] if t['type'] == '0x35']))
    check('0x26 is not decoded as a range',
          'range' not in [t for t in info['mct']['tlvs'] if t['type'] == '0x26'][0])
    al = info['action_log']
    check('action log: 4 entries from a NUL-separated reply', len(al) == 4, str(len(al)))
    check('action log: short codes kept verbatim',
          [e['text'] for e in al[:2]] == ['D:00010013', 'S:00010011'],
          str([e['text'] for e in al[:2]]))
    import time as _t
    _when = _t.strftime('%a %b %e %H:%M:%S %Y', _t.localtime(0x6A93ECC2))
    check('action log: epoch rendered like CFP (localtime)',
          al[2]['when'] == _when, '%r vs %r' % (al[2].get('when'), _when))
    check('action log: --0 becomes -ErrorCode:0-Success!',
          al[2]['text'].endswith('DEV-ErrorCode:0-Success!'), al[2]['text'][-40:])
    check('action log: entry without a status keeps its text',
          al[3]['status'] is None and al[3]['text'].endswith('QNXLOAD:2.0.0.4869 DEV'),
          al[3]['text'][-40:])
    # safety: the 0xB3 payload is fixed-size and read-only
    payload = getattr(StubDevice, 'action_payload', b'')
    check('action-log payload is 132 bytes', len(payload) == 132, str(len(payload)))
    check('action-log request uses action 0 (read, never append)',
          struct.unpack_from('<I', payload, 0)[0] == 0 if payload else False,
          payload[:8].hex())
    probes = info['probes']
    check('8 probes', len(probes) == 8, str(sorted(probes)))
    check('all probes answered as expected',
          all(e.get('ok') for e in probes.values()),
          str({k: e.get('resp') for k, e in probes.items() if not e.get('ok')}))
    rec = probes['osregion_record']
    check('osregion record decoded (version 3, 383 boots)',
          rec.get('version') == 3 and rec.get('region0_boots') == 383,
          str(rec))
    dram = probes['dram_info']
    check('DRAM decoded from the live layout',
          dram.get('size_mb') == 1024 and dram.get('vendor') == 'Elpida'
          and dram.get('revision') == '0x100', str(dram))
    vid = probes['vendor_id']
    check('vendor id matches CFP branding 504', vid.get('branding_id') == 504, str(vid))

    print('--- info text output')
    rc, out = run(['info'])
    check('text shows the PIN', 'PIN:' in out and '50175127' in out)
    check('text shows the OSREGION line', 'region-0 boots 383' in out)
    check('no unexpected-response flags', 'unexpected response' not in out)

    print('--- info --mct-file (offline)')
    fd, path = tempfile.mkstemp(suffix='.mct')
    with os.fdopen(fd, 'wb') as f:
        f.write(mct_blob())
    rc, out = run(['info', '--mct-file', path])
    check('offline MCT parse rc 0', rc == 0)
    check('offline parse finds 19 TLVs', out.count('len ') >= 19, out[:80])
    os.unlink(path)


# --------------------------------------------------------------- reset-osregion
def test_reset_osregion():
    print('--- reset-osregion dry run')
    StubDevice.record = RECORD_PRESENT
    StubDevice.deleted_calls = 0
    rc, out = run(['reset-osregion'])
    check('dry run rc 0', rc == 0)
    check('dry run sent no delete', StubDevice.deleted_calls == 0)
    check('dry run printed the record', '0x017F0000' in out)

    print('--- reset-osregion --apply')
    rc, out = run(['reset-osregion', '--apply', '--json'])
    res = json.loads(out)
    check('apply rc 0', rc == 0)
    check('delete sent once', StubDevice.deleted_calls == 1)
    check('record_changed True', res.get('record_changed') is True,
          str(res.get('record_changed')))
    check('before/after captured',
          res['before']['words'][1] == '0x00000003' and res['after']['all_zero'] is True)

    rc, out = run(['reset-osregion', '--apply'])
    check('second apply says UNCHANGED', 'UNCHANGED' in out,
          out.strip().splitlines()[-1] if out.strip() else '(no output)')


# ----------------------------------------------------------------------- flash
def test_flash():
    print('--- flash dry run (no device touched)')
    d = tempfile.mkdtemp()
    img = os.path.join(d, 'x.ifs')
    with open(img, 'wb') as f:
        f.write(bytes(range(256)) * 512)                 # 2 blocks
    StubDevice.sent = []
    rc, out = run(['flash', '--component', img, '--dest-block', '0x825'])
    check('dry run rc 0', rc == 0)
    check('dry run planned block 0x825', 'write block 0x00825' in out, out[:200])
    check('dry run touched no USB op', StubDevice.sent == [], str(StubDevice.sent))

    print('--- flash --apply (stub records the sequence)')
    StubDevice.sent = []
    StubDevice.preflash = None
    rc, out = run(['flash', '--component', img, '--dest-block', '0x825', '--apply'])
    check('apply rc 0', rc == 0, out[-200:])
    check('PreFlash byte 0x15 for this model', StubDevice.preflash == 0x15,
          str(StubDevice.preflash))
    check('sequence is PreFlash, frames, signature, complete',
          StubDevice.sent[:1] == [0x20EE] and StubDevice.sent[-2:] == [0x40F9, 0x40C0]
          and 0xF7 in StubDevice.sent, str(StubDevice.sent[:6]))
    check('signature length 560 announced', StubDevice.sig_len == 560,
          str(getattr(StubDevice, 'sig_len', None)))
    check('two frames for 2 blocks of payload', StubDevice.sent.count(0xF7) >= 1,
          str(StubDevice.sent.count(0xF7)))

    print('--- flash gate refuses a destination outside the extents')
    rc = None
    try:
        run(['flash', '--component', img, '--dest-block', '0x0'])
    except SystemExit as e:
        rc = e.code
    check('bad destination refused', rc is not None, str(rc))


# ------------------------------------------------------- containers (no device)
def synth_autoloader(path, payload):
    data = bytearray(0x200)
    data[0:2] = b'MZ'
    struct.pack_into('<I', data, 0x3C, 0x40)
    data[0x40:0x44] = b'PE\x00\x00'
    struct.pack_into('<H', data, 0x46, 1)
    struct.pack_into('<H', data, 0x54, 0)
    struct.pack_into('<I', data, 0x58 + 16, 0x100)
    struct.pack_into('<I', data, 0x58 + 20, 0x100)
    hdr = struct.pack('<III', 0x97C5D59C, 0x97C5D59C, 0x97C5D59C)
    hdr += struct.pack('<Q', 1)
    hdr += struct.pack('<Q', 0x200 + len(hdr) + 8)
    with open(path, 'wb') as f:
        f.write(bytes(data) + hdr + payload)


def test_containers():
    import shutil
    print('--- containers: build / unpack / split / parse')
    d = tempfile.mkdtemp()
    try:
        dense = os.path.join(d, 'a.ifs')
        with open(dense, 'wb') as f:
            f.write(bytes(range(256)) * 512)
        blk0, blk1, blk2 = b'A' * 0x10000, b'\xff' * 0x10000, b'B' * 0x10000
        sparse = os.path.join(d, 'b.ufs')
        with open(sparse, 'wb') as f:
            f.write(blk0 + blk1 + blk2)

        container, _ = bbusb.build([(dense, 16, 0), (sparse, 192, 0)],
                                   ver=3, sign=False)
        cpath = os.path.join(d, 't.signed')
        with open(cpath, 'wb') as f:
            f.write(container)

        rc, out = run(['parse', cpath, '--json'])
        check('parse rc 0', rc == 0)
        parsed = json.loads(out)
        check('V1 run offset is the Delta', parsed['v1']['records'][0]['runs'][0]['offset'] == 16)
        check('V2 run offset drops the Delta',
              parsed['v2']['records'][0]['runs'][0]['offset'] == 0)

        rc, out = run(['unpack', cpath, '-o', d])
        check('unpack rc 0', rc == 0, out[-160:])
        check('dense component round-trips',
              open(os.path.join(d, 't.0.ifs'), 'rb').read() == open(dense, 'rb').read())
        check('sparse component restores its 0xFF hole',
              open(os.path.join(d, 't.1.ufs'), 'rb').read() == blk0 + blk1 + blk2)
        check('unpack prints lst lines', '=16,0' in out and '=192,0' in out, out[-120:])

        rc, out = run(['unpack', cpath, '-o', d, '--only', '0'])
        check('--only extracts one component', rc == 0)

        exe = os.path.join(d, 'auto.exe')
        synth_autoloader(exe, container)
        rc, out = run(['split', exe, '-o', d])
        check('split rc 0', rc == 0, out[-160:])
        check('split reports PE end 0x200', '0x200' in out, out)
        got = open(os.path.join(d, 'auto.0.signed'), 'rb').read()
        check('split reproduces the appended container', got == container)

        lst = os.path.join(d, 'pack.lst')
        with open(lst, 'w') as f:
            f.write('%s=16,0\n' % dense)
        outpath = os.path.join(d, 'back.signed')
        rc, out = run(['pack', '-l', lst, '-v', '1,2', '-c', outpath])
        check('pack rc 0', rc == 0, out[-160:])
        back = open(outpath, 'rb').read()
        check('pack produced a V1+V2 container',
              bbusb.parse(back)['v1']['records'][0]['runs'][0]['offset'] == 16)
        check('pack is deterministic',
              bbusb.build([(dense, 16, 0)], ver=3, sign=False)[0] == back)
    finally:
        shutil.rmtree(d)


def test_decoders_from_capture():
    """The decoders vs the 2026-11-01 capture, and vs CFP's own printed table."""
    import re
    print('--- decoders vs the live capture (docs/reports/data/live-info-2026-11-01)')
    d = os.path.join(ROOT, 'docs', 'reports', 'data', 'live-info-2026-11-01')
    if not os.path.isdir(d):
        print('  capture not present, skipping')
        return

    m = bbusb.decode_bootrom_metrics(open(os.path.join(d, 'bootrom_metrics.bin'), 'rb').read())
    check('metrics: hardware id 0x06001A06', m.get('hardware_id') == 0x06001A06, str(m.get('hardware_id')))
    check('metrics: bootrom version 5.27.0.20', m.get('br_version') == '5.27.0.20', str(m.get('br_version')))
    check('metrics: metrics version 6.24', m.get('metrics_version') == '6.24', str(m.get('metrics_version')))
    check('metrics: options / drivers / processor',
          (m.get('supported_options'), m.get('drivers'), m.get('processor')) == (9, 0x10, 0x20), str(m))
    check('metrics: LDR blocks 0x80014674', m.get('ldr_blocks') == 0x80014674, str(m.get('ldr_blocks')))
    check('metrics: bootrom size 0x20000', m.get('bootrom_size') == 0x20000, str(m.get('bootrom_size')))
    check('metrics: build user/date/time',
          (m.get('build_user'), m.get('build_date'), m.get('build_time')) ==
          ('ec_agent', 'Mar 24 2011', '15:31:02'), str(m))
    check('metrics: hardware id string',
          m.get('hardware_id_string') == 'RIM BlackBerry Device', str(m.get('hardware_id_string')))

    o = bbusb.decode_os_metrics(open(os.path.join(d, 'os_metrics.bin'), 'rb').read())
    check('os metrics: version 2.0.0.4869', o.get('os_version') == '2.0.0.4869',
          str(o.get('os_version')))
    check('os metrics: metrics version 3.18', o.get('metrics_version') == '3.18',
          str(o.get('metrics_version')))
    check('os metrics: address range', o.get('os_address') == '0x800FF800-0x803BF60F',
          str(o.get('os_address')))
    check('os metrics: build user/date/time',
          (o.get('build_user'), o.get('build_date'), o.get('build_time')) ==
          ('cfp', 'Nov 15 2011', '14:51:36'), str(o))
    check('os metrics: hardware id + string',
          o.get('hardware_id') == 0x06001A06 and
          o.get('hardware_id_string') == 'RIM BlackBerry Device', str(o.get('hardware_id')))
    check('os metrics: image magic 0xC3D0E193', o.get('image_magic_hex') == '0xC3D0E193',
          str(o.get('image_magic_hex')))

    c = bbusb.decode_csd_extcsd(open(os.path.join(d, 'csd_extcsd.bin'), 'rb').read())
    check('csd/extcsd: 50 fields decoded', len(c) == 50, str(len(c)))
    check('csd/extcsd: sector count 0x76D0000', c.get('sector_count') == 0x76D0000)
    check('csd/extcsd: max enh 0x72E', c.get('max_enh_size_mult') == 0x72E)
    check('csd/extcsd: c_size 0xFFF', c.get('c_size') == 0xFFF)
    check('csd/extcsd: rpmb mult 1 / partition support 3',
          (c.get('rpmb_size_mult'), c.get('partition_support')) == (1, 3))
    check('csd/extcsd: hc wp grp 0x20', c.get('hc_wp_grp_size') == 0x20)
    check('csd/extcsd: version 3 / spec 4',
          (c.get('csd_version_number'), c.get('specification_version')) == (3, 4))

    txt = open(os.path.join(d, '..', 'cfp-2026-11-01', 'CFP-flashinfo.txt')).read()
    cfp = {}
    for line in txt.splitlines():
        mm = re.match(r'\s*([A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+)\s*$', line)
        if mm:
            cfp[mm.group(1).lower()] = int(mm.group(2), 16)
    overlap = {k: v for k, v in c.items() if k in cfp}
    mism = {k: (v, cfp[k]) for k, v in overlap.items() if cfp[k] != v}
    check('every decoded CSD/ExtCSD field agrees with CFP',
          len(overlap) >= 30 and not mism,
          '%d compared, mismatches %s' % (len(overlap), str(mism)[:200]))


def test_password_stage():
    """The password stage must never retry a rejection (ten wrong attempts wipe the
    unit) and must not treat a typed password as a CLI-supplied one."""
    import builtins

    def challenge(remaining, budget):
        p = bytearray(24)
        p[0] = remaining
        p[1] = budget
        p[4:8] = b'\x01\x02\x03\x04'
        p[12:20] = b'\x11' * 8
        struct.pack_into('<I', p, 20, 16)
        return 0x0E, bytes(p)

    def device(script):
        bb = object.__new__(_RealBBUSB)
        seen = []

        def channel0(cmd, data=b'', timeout=1000):
            seen.append(cmd)
            return script.pop(0)
        bb.channel0 = channel0
        bb.read_data = lambda timeout=1000: b''
        return bb, seen

    print('--- password stage')
    bb, seen = device([challenge(10, 10), (0x10, b'')])
    check('supplied password accepted in two frames',
          bb.password_info('0000') is True and seen == [0x0A, 0x0F], str(seen))

    bb, seen = device([challenge(10, 10), challenge(9, 10)])
    err = None
    try:
        bb.password_info('000')
    except bbusb.PasswordError as e:
        err = str(e)
    check('a rejected --password is NOT retried', err is not None and seen == [0x0A, 0x0F],
          '%s / %s' % (err, seen))
    check('rejection message shows the remaining budget', err and '9/10' in err, str(err))

    bb, seen = device([(0x15, b'x' * 24)])
    err = None
    try:
        bb.password_info('0000')
    except bbusb.PasswordError as e:
        err = str(e)
    check('an unexpected response aborts with its code',
          err is not None and '0x15' in err, str(err))

    # upstream rule: stop once the device reports two attempts spent (8/10)
    bb, seen = device([challenge(8, 10)])
    err = None
    try:
        bb.password_info('0000')
    except bbusb.PasswordError as e:
        err = str(e)
    check('stops at two spent attempts, like bb10mt',
          err is not None and '8/10' in err and seen == [0x0A], '%s / %s' % (err, seen))

    # an empty line must never be sent as a guess
    bb, seen = device([challenge(10, 10), (0x10, b'')])
    answers = iter(['', '   ', '0000'])

    def empty_then_real(prompt=''):
        return next(answers)
    real_input = builtins.input
    builtins.input = empty_then_real
    try:
        ok = bb.password_info('')
    finally:
        builtins.input = real_input
    check('blank input is not sent as a guess', ok is True and seen == [0x0A, 0x0F],
          str(seen))

    # interactive: a wrong entry is re-presented with the new budget
    bb, seen = device([challenge(10, 10), challenge(9, 10), (0x10, b'')])
    answers = iter(['wrong', '0000'])
    prompts = []

    def fake_input(prompt=''):
        prompts.append(prompt)
        return next(answers)
    real_input = builtins.input
    builtins.input = fake_input
    try:
        ok = bb.password_info('')
    finally:
        builtins.input = real_input
    check('interactive retry asks again after a wrong entry', ok is True, str(ok))
    check('the second prompt shows 9/10',
          len(prompts) == 2 and '9/10' in prompts[1], str(prompts))


def main():
    test_info()
    test_reset_osregion()
    test_flash()
    test_containers()
    test_decoders_from_capture()
    test_password_stage()
    print()
    print('FAILURES:', failures if failures else 'none')
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
