#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ifs_build.py -- build a QNX image filesystem (imagefs) and an IFS component.

Milestone 1 of docs/project/IFS-REBUILD.md: emit `image_header + dirents +
file data + image_trailer`, append it to RIM's unchanged boot prefix, and let
QNX's own `dumpifs` read it back with the same entries.

The output is UNCOMPRESSED.  The stock image's startup header sets
`flags1 & STARTUP_HDR_FLAGS1_COMPRESS_MASK` to UCL (`0x0c`); an uncompressed
stream must say COMPRESS_NONE (`0x00`) or `dumpifs` tries to decompress it and
segfaults.  Unless `--keep-compress-flag` is given, that one byte is cleared
(`flags1 = 0x01`, VIRTUAL).  Everything else in the prefix is copied verbatim.

Format facts (all verified against the 2.1.0.1917 image and a `mkifs` image):

  image_header (ARM32, little-endian):
    signature[7]="imagefs", flags u8, image_size u32, hdr_dir_size u32,
    dir_offset u32, boot_ino[4], script_ino, chain_paddr, spare[10],
    mountflags, mountpoint[] (NUL-terminated, 4-byte aligned)
    size = 0x58 + align4(strlen(mountpoint) + 1)   (0x5c for "/")

  union image_dirent:
    attr      = size u16, extattr_offset u16, ino, mode, gid, uid, mtime
                (24 bytes; `size` is the whole dirent, 4-byte aligned)
    file      = attr, offset u32 (from image header), size u32, path
    dir       = attr, path
    symlink   = attr, sym_offset u16 (= strlen(path)+1), sym_size u16,
                path, link text
    The dirent list is terminated by a 4-byte zero word; `hdr_dir_size`
    includes that word.

  image_trailer: u32 cksum at image_size-4 such that
    (sum of all u32 LE words in [0, image_size-4), with the tail zero-padded)
    + cksum == 0 mod 2^32.

The file data layout is reproduced from the reference listing when no file
changes size; otherwise (`--repack`, or an automatic fallback) data is packed
with 0x800-byte alignment.

Usage
-----
  # capture the reference listing (requires the QNX SDP)
  ifs_build.py listing STOCK.ifs > stock.lst

  # rebuild from the dumpifs -x tree; the reference listing supplies symlinks,
  # dirent order, modes, inodes and mtimes
  ifs_build.py build --tree TREE --listing stock.lst --stock STOCK.ifs \
                     --out OUT.ifs

  # swap one file (milestone 2/3); size change triggers a repack
  ifs_build.py build ... --replace proc/boot/rfs_validator=./shim

  # acceptance: dumpifs must list the same entries and print a Checksums line
  ifs_build.py check --image OUT.ifs --listing stock.lst
  ifs_build.py selftest

Exit status: 0 = OK, 1 = verification failure, 2 = usage/parse error.
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

IMAGE_SIGNATURE = b'imagefs'
IMAGE_FLAGS_INO_BITS = 0x04
IMAGE_TRAILER = 4
HEADER_FIELDS = 0x58            # up to and including mountflags
STARTUP_FLAGS1_OFF = 0x06       # struct startup_header: sig 4, ver 2, flags1
STARTUP_COMPRESS_MASK = 0x1c
STARTUP_COMPRESS_NONE = 0x00
DEFAULT_DATA_ALIGN = 0x800

S_IFMT = 0o170000
S_IFREG = 0o100000
S_IFDIR = 0o040000
S_IFLNK = 0o120000
S_IFCHR = 0o020000
S_IFBLK = 0o060000
S_IFIFO = 0o010000

ENTRY_RE = re.compile(r'^\s*(----|[0-9a-fA-F]+)\s+(----|[0-9a-fA-F]+)\s+(\S.*)$')
META_RE = re.compile(r'\bgid=(-?\d+)\s+uid=(-?\d+)\s+mode=0?([0-7]+)'
                     r'\s+ino=(-?\d+)\s+mtime=([0-9a-fA-F]+)')
HDR_META_RE = re.compile(r'\bflags=0x([0-9a-fA-F]+)\s+script=(-?\d+)'
                         r'\s+boot=(-?\d+)\s+mntflg=(-?\d+)')


def align(n, a):
    return (n + a - 1) // a * a


def u32le(buf, off):
    return struct.unpack_from('<I', buf, off)[0]


class Entry:
    """One imagefs dirent."""

    def __init__(self, name, kind, size=0, offset=None, ino=None, mode=None,
                 gid=0, uid=0, mtime=0, target=None, data=None):
        self.name = name
        self.kind = kind            # 'file' | 'dir' | 'symlink' | 'device'
        self.size = size            # file bytes / symlink target bytes
        self.offset = offset        # relative to the image header, files only
        self.ino = ino
        self.mode = mode            # full mode incl. S_IFMT
        self.gid = gid
        self.uid = uid
        self.mtime = mtime
        self.target = target        # symlink text
        self.data = data            # file bytes
        self.dev = 0
        self.rdev = 0
        self.ref_size = None        # size in the reference listing
        self.has_meta = False       # metadata came from a dumpifs -vv listing

    @property
    def type_bits(self):
        return {'file': S_IFREG, 'dir': S_IFDIR, 'symlink': S_IFLNK,
                'device': S_IFCHR}[self.kind]

    def full_mode(self):
        if self.mode is None:
            return self.type_bits | (0o755 if self.kind != 'file' else 0o644)
        return self.type_bits | (self.mode & 0o7777)

    def __repr__(self):
        return '<%s %r size=%d off=%r ino=%r mode=%o>' % (
            self.kind, self.name, self.size, self.offset, self.ino,
            self.full_mode())


class Header:
    def __init__(self):
        self.mountpoint = ''
        self.flags = IMAGE_FLAGS_INO_BITS
        self.boot_ino = [0, 0, 0, 0]
        self.script_ino = 0
        self.chain_paddr = 0
        self.mountflags = 0
        self.image_off = 0          # imagefs offset inside the container
        self.startup_off = None     # startup header offset (for flags1)
        self.startup_size = None


# ---------------------------------------------------------------------------
# dumpifs listing parsing
# ---------------------------------------------------------------------------
def parse_listing(text):
    """Parse `dumpifs [-vv]` output into (Header, [Entry]).

    Both the plain and the -vv forms are accepted.  Without -vv, modes come
    from the extracted tree at build time and inodes are assigned in order.
    """
    hdr = Header()
    entries = []
    lines = text.splitlines()
    i = 0
    seen_image_header = False

    def take_meta(start):
        """Return (metadata dict, next index) for the lines after an entry."""
        meta = {}
        j = start
        while j < len(lines):
            ln = lines[j]
            if not ln.strip():
                j += 1
                continue
            if ENTRY_RE.match(ln) or ln.startswith('Checksums:'):
                break
            m = META_RE.search(ln)
            if m and not meta:
                meta = {'gid': int(m.group(1)), 'uid': int(m.group(2)),
                        'mode': int(m.group(3), 8), 'ino': int(m.group(4)),
                        'mtime': int(m.group(5), 16)}
            j += 1
        return meta, j

    while i < len(lines):
        line = lines[i]
        if line.startswith('Checksums:'):
            break
        m = ENTRY_RE.match(line)
        if not m:
            i += 1
            continue
        off_s, size_s, rest = m.group(1), m.group(2), m.group(3)
        rest = rest.rstrip()
        i += 1

        if not seen_image_header:
            # *.boot and startup.* precede the image filesystem; only the
            # startup header offset is useful to us (for the compress flag).
            if rest.startswith('Startup-header'):
                if off_s != '----':
                    hdr.startup_off = int(off_s, 16)
                if size_s != '----':
                    hdr.startup_size = int(size_s, 16)
                continue
            if not rest.startswith('Image-header'):
                continue
            seen_image_header = True

        if rest.startswith('Image-header'):
            hdr.image_off = int(off_s, 16)
            mp = re.search(r'mountpoint=(.*)$', rest)
            if mp:
                hdr.mountpoint = mp.group(1)
            meta, i = take_meta(i)
            continue
        if rest == 'Image-directory':
            continue
        if rest == 'Root-dirent':
            meta, i = take_meta(i)
            e = Entry('', 'dir', ino=meta.get('ino', 1),
                      mode=meta.get('mode'), gid=meta.get('gid', 0),
                      uid=meta.get('uid', 0), mtime=meta.get('mtime', 0))
            e.has_meta = bool(meta)
            entries.append(e)
            continue

        # Image-header's metadata line is parsed by take_meta via HDR_META_RE
        # below; regular entries use META_RE.
        if ' -> ' in rest and off_s == '----':
            name, target = rest.split(' -> ', 1)
            e = Entry(name, 'symlink', size=int(size_s, 16) if size_s != '----' else len(target),
                      target=target)
        else:
            e = Entry(rest, 'file',
                      size=int(size_s, 16) if size_s != '----' else 0,
                      offset=(int(off_s, 16) - hdr.image_off)
                      if off_s != '----' else None)
        meta, i = take_meta(i)
        if meta:
            e.ino = meta['ino']
            e.mode = meta['mode']
            e.gid = meta['gid']
            e.uid = meta['uid']
            e.mtime = meta['mtime']
            e.has_meta = True
        if e.kind == 'file':
            e.ref_size = e.size
        entries.append(e)

    # The Image-header metadata line is consumed by take_meta, but take_meta
    # only looks for META_RE; parse the header fields separately.
    for ln in lines:
        if ln.lstrip().startswith('flags='):
            hm = HDR_META_RE.search(ln)
            if hm:
                hdr.flags = int(hm.group(1), 16)
                hdr.script_ino = int(hm.group(2)) & 0xFFFFFFFF
                hdr.boot_ino[0] = int(hm.group(3)) & 0xFFFFFFFF
                hdr.mountflags = int(hm.group(4)) & 0xFFFFFFFF
                break

    return hdr, entries


# ---------------------------------------------------------------------------
# dirent / header encoding
# ---------------------------------------------------------------------------
def _attr(e, dirent_size):
    return struct.pack('<HHIIIII', dirent_size, 0, e.ino & 0xFFFFFFFF,
                       e.full_mode(), e.gid, e.uid, e.mtime & 0xFFFFFFFF)


def dirent_len(e):
    """Encoded dirent length (4-byte aligned); independent of offset/size."""
    n = 24
    if e.kind == 'file':
        n += 8
    elif e.kind == 'symlink':
        n += 4
    elif e.kind == 'device':
        n += 8
    n += len(e.name.encode('latin-1')) + 1
    if e.kind == 'symlink':
        n += len(e.target.encode('latin-1')) + 1
    return align(n, 4)


def encode_dirent(e):
    path = e.name.encode('latin-1') + b'\0'
    if e.kind == 'file':
        body = _attr(e, 0) + struct.pack('<II', e.offset, e.size) + path
    elif e.kind == 'dir':
        body = _attr(e, 0) + path
    elif e.kind == 'symlink':
        target = e.target.encode('latin-1')
        body = _attr(e, 0) + struct.pack('<HH', len(path), len(target)) \
            + path + target + b'\0'
    elif e.kind == 'device':
        body = _attr(e, 0) + struct.pack('<II', e.dev, e.rdev) + path
    else:
        raise ValueError('unknown entry kind %r' % e.kind)
    size = align(len(body), 4)
    body = body + b'\0' * (size - len(body))
    return struct.pack('<H', size) + body[2:]


def imagefs_cksum(payload):
    """Two's complement of the 32-bit LE word sum.

    mkifs folds only whole words; a trailing 1-3 byte remainder is dropped
    (not zero-padded).  Our emitter always produces a multiple of 4.
    """
    data = bytes(payload)[:len(payload) - (len(payload) % 4)]
    total = 0
    for (w,) in struct.iter_unpack('<I', data):
        total = (total + w) & 0xFFFFFFFF
    return (-total) & 0xFFFFFFFF


def encode_header(hdr, image_size, hdr_dir_size, dir_offset):
    buf = bytearray()
    buf += IMAGE_SIGNATURE
    buf += bytes([hdr.flags & 0xFF])
    buf += struct.pack('<III', image_size, hdr_dir_size, dir_offset)
    for ino in hdr.boot_ino:
        buf += struct.pack('<I', ino & 0xFFFFFFFF)
    buf += struct.pack('<II', hdr.script_ino & 0xFFFFFFFF,
                       hdr.chain_paddr & 0xFFFFFFFF)
    buf += b'\0' * 40                                   # spare[10]
    buf += struct.pack('<I', hdr.mountflags & 0xFFFFFFFF)
    mp = hdr.mountpoint.encode('latin-1') + b'\0'
    mp += b'\0' * ((-len(mp)) % 4)
    buf += mp
    return bytes(buf)


# ---------------------------------------------------------------------------
# build
# ---------------------------------------------------------------------------
def find_dumpifs():
    cand = os.environ.get('DUMPIFS')
    if cand and os.path.exists(cand):
        return cand
    p = shutil.which('dumpifs')
    if p:
        return p
    p = os.path.join(ROOT, 'local', 'toolchains', 'qnx660-master', 'host',
                     'linux', 'x86', 'usr', 'bin', 'dumpifs')
    return p if os.path.exists(p) else None


def _load_entry_data(entries, tree, replacements):
    """Fill .data for files; verify that every tree file exists."""
    for e in entries:
        if e.kind != 'file':
            continue
        if e.name in replacements:
            with open(replacements[e.name], 'rb') as f:
                e.data = f.read()
            continue
        path = os.path.join(tree, e.name)
        if not os.path.isfile(path):
            raise SystemExit('ifs_build: missing file in tree: %s' % path)
        with open(path, 'rb') as f:
            e.data = f.read()


def _assign_inodes(entries):
    """Give entries without a reference ino sequential inodes (root=1)."""
    used = {e.ino for e in entries if e.ino is not None}
    nxt = 2
    for e in entries:
        if e.ino is None:
            while nxt in used:
                nxt += 1
            e.ino = nxt
            used.add(nxt)
            nxt += 1


def _fill_default_metadata(entries, tree):
    for e in entries:
        if e.mode is None:
            if e.kind == 'file':
                st = os.stat(os.path.join(tree, e.name))
                e.mode = st.st_mode & 0o7777
                if not e.mtime:
                    e.mtime = int(st.st_mtime) & 0xFFFFFFFF
            elif e.kind == 'dir':
                e.mode = 0o755
            else:
                e.mode = 0o777


def _layout_reference(entries):
    """True if the reference offsets can be kept.

    A file may shrink into its slot (the freed bytes stay zero); it may not
    grow, because that would move every later file and change the image size.
    """
    for e in entries:
        if e.kind != 'file':
            continue
        if e.offset is None:
            return False
        cap = e.ref_size if e.ref_size is not None else e.size
        if len(e.data) > cap:
            return False
    return True


def _layout_pack(entries, dir_end, va_base):
    """Place file data on page boundaries in the image's address space.

    mkifs aligns (imagefs VA + running offset) to max(page_align, 0x1000);
    the stock imagefs VA is LoadStart + imagefs offset (0x80544110), so
    relative offsets are congruent to 0xEF0 mod 0x1000.  Zeros in `va_base`
    reproduce plain file-space alignment for a bare imagefs.
    """
    cursor = dir_end
    for e in entries:
        if e.kind != 'file':
            continue
        start = align(cursor + va_base, 0x1000) - va_base
        e.offset = start
        cursor = start + len(e.data)
    return cursor


def emit(header, entries, image_off, repack=False, va_base=None):
    """Return the imagefs bytes (header + dirents + data + trailer)."""
    dir_offset = HEADER_FIELDS + align(len(header.mountpoint.encode('latin-1')) + 1, 4)
    if va_base is None:
        va_base = image_off

    # dirent blobs must know their offsets before they are encoded, so place
    # data first (reference offsets are already in the entries).
    if repack or not _layout_reference(entries):
        for e in entries:
            if e.kind == 'file':
                e.size = len(e.data)
        dir_end = dir_offset + 4 + sum(dirent_len(e) for e in entries)
        _layout_pack(entries, dir_end, va_base)
        repacked = True
    else:
        for e in entries:
            if e.kind == 'file':
                e.size = len(e.data)
        repacked = False

    dir_blobs = [encode_dirent(e) for e in entries]
    hdr_dir_size = dir_offset + sum(len(b) for b in dir_blobs) + 4

    data_end = max([hdr_dir_size] +
                   [e.offset + len(e.data) for e in entries if e.kind == 'file'])
    image_size = align(data_end, 4) + IMAGE_TRAILER

    buf = bytearray(image_size)
    head = encode_header(header, image_size, hdr_dir_size, dir_offset)
    if len(head) != dir_offset:
        raise AssertionError('header size %d != dir_offset %d' % (len(head), dir_offset))
    buf[0:dir_offset] = head
    o = dir_offset
    for b in dir_blobs:
        buf[o:o + len(b)] = b
        o += len(b)
    # 4-byte zero terminator at o..o+3 (already zero)

    for e in entries:
        if e.kind == 'file':
            buf[e.offset:e.offset + len(e.data)] = e.data

    cks = imagefs_cksum(bytes(buf[:image_size - IMAGE_TRAILER]))
    struct.pack_into('<I', buf, image_size - IMAGE_TRAILER, cks)
    return bytes(buf), repacked


def load_prefix(stock, image_off, keep_compress_flag=False, startup_off=None,
                startup_size=None):
    """Copy 0x0..image_off, clear flags1, and refresh the startup checksum.

    `startup_size` must be the startup BLOCK size, not the 0x100 listing size
    of the "Startup-header" line: it is the field at startup_off+0x20, and the
    stored checksum is the word at startup_off+startup_size-4.
    """
    if image_off == 0:
        return bytearray()
    if not stock:
        raise SystemExit('ifs_build: --stock is required when the imagefs '
                         'offset is nonzero')
    with open(stock, 'rb') as f:
        data = f.read()
    if len(data) < image_off:
        raise SystemExit('ifs_build: %s is shorter than the imagefs offset '
                         '0x%X' % (stock, image_off))
    prefix = bytearray(data[:image_off])
    if startup_off is not None and startup_off + 0x24 <= len(prefix):
        stored_ss = u32le(prefix, startup_off + 0x20)
        if stored_ss:
            startup_size = stored_ss
    if startup_off is not None and startup_size:
        if not keep_compress_flag:
            at = startup_off + STARTUP_FLAGS1_OFF
            if at < len(prefix):
                prefix[at] = ((prefix[at] & ~STARTUP_COMPRESS_MASK)
                              | STARTUP_COMPRESS_NONE)
        # The startup block's checksum is a stored word at
        # startup_off+startup_size-4 (mkifs writes it; nothing recomputes it).
        # Clearing flags1 invalidates it, so rewrite it with the same rule.
        end = startup_off + startup_size - 4
        if (end > startup_off and end + 4 <= len(prefix)
                and (end - startup_off) % 4 == 0):
            cks = imagefs_cksum(prefix[startup_off:end])
            struct.pack_into('<I', prefix, end, cks)
    return prefix


def build(args):
    text = open(args.listing, 'r', encoding='latin-1').read()
    header, entries = parse_listing(text)
    if args.image_off is not None:
        header.image_off = args.image_off

    replacements = {}
    for r in args.replace or []:
        if '=' not in r:
            raise SystemExit('ifs_build: --replace expects NAME=PATH')
        name, path = r.split('=', 1)
        replacements[name] = path

    _load_entry_data(entries, args.tree, replacements)
    _assign_inodes(entries)
    _fill_default_metadata(entries, args.tree)

    if header.script_ino == 0:
        for e in entries:
            if e.name == 'proc/boot/.script':
                header.script_ino = e.ino
            if e.name == 'proc/boot/procnto-smp-instr':
                header.boot_ino[0] = (e.ino | 0xE0000000) & 0xFFFFFFFF

    prefix = load_prefix(args.stock, header.image_off,
                         keep_compress_flag=args.keep_compress_flag,
                         startup_off=header.startup_off,
                         startup_size=header.startup_size)
    # mkifs aligns data in the image's address space: imagefs VA = LoadStart +
    # the imagefs' offset in the container.
    load_start = u32le(prefix, 0x04) if len(prefix) >= 8 else 0
    va_base = load_start + header.image_off if prefix else header.image_off
    imagefs, repacked = emit(header, entries, header.image_off,
                             repack=args.repack, va_base=va_base)
    out = bytes(prefix) + imagefs
    with open(args.out, 'wb') as f:
        f.write(out)

    if not args.quiet:
        print('ifs_build: imagefs 0x%X bytes (%s layout), prefix 0x%X, '
              'output 0x%X' % (len(imagefs),
                               'repacked' if repacked else 'reference',
                               len(prefix), len(out)))
        print('ifs_build: %d dirents (root + %d), trailer 0x%08X'
              % (len(entries), len(entries) - 1,
                 u32le(imagefs, len(imagefs) - 4)))
        print('ifs_build: wrote %s' % args.out)
    return 0


# ---------------------------------------------------------------------------
# verification
# ---------------------------------------------------------------------------
def _dump_listing(dumpifs, image):
    p = subprocess.run([dumpifs, '-vv', image], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    return p.returncode, p.stdout.decode('latin-1'), p.stderr.decode('latin-1')


def check(args):
    dumpifs = args.dumpifs or find_dumpifs()
    if not dumpifs:
        print('ifs_build: dumpifs not found (use --dumpifs or . tools/env.sh)')
        return 2
    rc, out, err = _dump_listing(dumpifs, args.image)
    if rc != 0:
        print('ifs_build: dumpifs failed (rc=%d): %s' % (rc, err.strip()))
        return 1
    ref_hdr, ref_entries = parse_listing(
        open(args.listing, 'r', encoding='latin-1').read())
    got_hdr, got_entries = parse_listing(out)

    failures = []

    def bad(msg):
        failures.append(msg)

    if len(got_entries) != len(ref_entries):
        bad('entry count %d != reference %d'
            % (len(got_entries), len(ref_entries)))
    for i, (a, b) in enumerate(zip(ref_entries, got_entries)):
        if a.name != b.name:
            bad('#%d name %r != %r' % (i, a.name, b.name))
            continue
        if a.size != b.size:
            bad('%s size 0x%X != 0x%X' % (a.name, b.size, a.size))
        if a.mode is not None and a.full_mode() != b.full_mode():
            bad('%s mode 0%o != 0%o' % (a.name, b.full_mode(), a.full_mode()))
        if a.kind != b.kind:
            bad('%s kind %s != %s' % (a.name, a.kind, b.kind))
        if a.target != b.target:
            bad('%s target %r != %r' % (a.name, a.target, b.target))
        for f in ('ino', 'uid', 'gid', 'mtime'):
            if a.has_meta and getattr(a, f) != getattr(b, f):
                bad('%s %s %r != %r' % (a.name, f, getattr(a, f), getattr(b, f)))
        if args.compare_offsets and a.offset != b.offset:
            bad('%s offset 0x%X != 0x%X' % (a.name, a.offset or 0, b.offset or 0))

    if 'Checksums:' not in out:
        bad('no Checksums: line')
    else:
        m = re.search(r'image=0x([0-9a-fA-F]+)', out)
        data = open(args.image, 'rb').read()
        image_size = u32le(data, ref_hdr.image_off + 8)
        stored = u32le(data, ref_hdr.image_off + image_size - 4)
        if not m:
            bad('Checksums line has no image= value')
        elif int(m.group(1), 16) != stored:
            bad('dumpifs image checksum 0x%s != trailer 0x%08X'
                % (m.group(1), stored))
        calc = imagefs_cksum(
            data[ref_hdr.image_off:ref_hdr.image_off + image_size - 4])
        if calc != stored:
            bad('trailer 0x%08X does not satisfy the sum rule (calc 0x%08X)'
                % (stored, calc))

    if failures:
        print('ifs_build: CHECK FAILED (%d)' % len(failures))
        for f in failures:
            print('  - ' + f)
        return 1
    print('ifs_build: CHECK PASSED -- %d entries, names/sizes/modes match, '
          'checksum line present' % len(got_entries))
    return 0


# ---------------------------------------------------------------------------
# listing / selftest
# ---------------------------------------------------------------------------
def cmd_listing(args):
    dumpifs = args.dumpifs or find_dumpifs()
    if not dumpifs:
        print('ifs_build: dumpifs not found')
        return 2
    rc, out, err = _dump_listing(dumpifs, args.image)
    if rc != 0:
        sys.stderr.write(err)
        return rc
    if args.out:
        with open(args.out, 'w', encoding='latin-1') as f:
            f.write(out)
    else:
        sys.stdout.write(out)
    return 0


SELFTEST_LISTING = """\
   Offset     Size  Name
        0       5c  Image-header mountpoint=/
                       flags=0x4 script=0 boot=0 mntflg=0
       5c       10  Image-directory
     ----     ----  Root-dirent
                       gid=0 uid=0 mode=0755 ino=1 mtime=0
     ----       0  hello
                       gid=0 uid=0 mode=0644 ino=2 mtime=0
     ----       0  sub/data
                       gid=0 uid=0 mode=0600 ino=3 mtime=0
     ----       9  sub/link -> ../hello
                       gid=0 uid=0 mode=0777 ino=4 mtime=0
Checksums: image=0x0
"""


def cmd_selftest(_args):
    failures = []

    def check(name, cond, detail=''):
        print(('PASS ' if cond else 'FAIL ') + name +
              (('  ' + detail) if detail and not cond else ''))
        if not cond:
            failures.append(name)

    # checksum: sum(whole words) + cksum == 0 (mkifs drops a 1-3 byte tail)
    for payload in (b'', b'hello\n', bytes(range(256)) * 3 + b'abc'):
        c = imagefs_cksum(payload)
        whole = payload[:len(payload) - (len(payload) % 4)]
        s = sum(w for (w,) in struct.iter_unpack('<I', whole))
        check('cksum zero-sum len=%d' % len(payload),
              (s + c) & 0xFFFFFFFF == 0)

    tmp = tempfile.mkdtemp(prefix='ifs_build_selftest_')
    try:
        tree = os.path.join(tmp, 'tree')
        os.makedirs(os.path.join(tree, 'sub'))
        with open(os.path.join(tree, 'hello'), 'wb') as f:
            f.write(b'hello from ifs_build\n')
        with open(os.path.join(tree, 'sub', 'data'), 'wb') as f:
            f.write(b'\x00\x01\x02\x03' * 1000)
        lst = os.path.join(tmp, 'ref.lst')
        with open(lst, 'w') as f:
            f.write(SELFTEST_LISTING)
        out = os.path.join(tmp, 'out.ifs')

        import types
        a = types.SimpleNamespace(
            tree=tree, listing=lst, stock=None, out=out, replace=[],
            repack=True, keep_compress_flag=False, image_off=0, quiet=True)
        rc = build(a)
        check('build rc', rc == 0)
        data = open(out, 'rb').read()
        check('output starts with imagefs', data[:7] == IMAGE_SIGNATURE)
        image_size = u32le(data, 8)
        check('image_size == file length', image_size == len(data),
              '0x%X vs 0x%X' % (image_size, len(data)))
        check('trailer satisfies zero-sum', imagefs_cksum(data[:-4]) ==
              u32le(data, len(data) - 4))

        dumpifs = find_dumpifs()
        if dumpifs:
            rc, text, err = _dump_listing(dumpifs, out)
            check('dumpifs reads it', rc == 0, err.strip())
            if rc == 0:
                _, entries = parse_listing(text)
                names = [e.name for e in entries]
                check('dumpifs names', names == ['', 'hello', 'sub/data', 'sub/link'],
                      repr(names))
                sizes = {e.name: e.size for e in entries}
                check('file sizes', sizes.get('hello') == 21 and
                      sizes.get('sub/data') == 4000, repr(sizes))
                modes = {e.name: e.full_mode() for e in entries}
                check('modes', modes.get('hello') == S_IFREG | 0o644 and
                      modes.get('sub/link') == S_IFLNK | 0o777, repr(modes))
                check('Checksums line', 'Checksums:' in text)
        else:
            print('SKIP dumpifs round trip (dumpifs not found)')
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    if failures:
        print('ifs_build: SELFTEST FAILED (%d)' % len(failures))
        return 1
    print('ifs_build: SELFTEST PASSED')
    return 0


# ---------------------------------------------------------------------------
def main(argv):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd')

    s = sub.add_parser('build', help='emit an imagefs + prefix')
    s.add_argument('--tree', required=True, help='dumpifs -x extraction tree')
    s.add_argument('--listing', required=True,
                   help='dumpifs -vv listing of the reference image')
    s.add_argument('--stock', help='stock .ifs (prefix source, 0x0..image_off)')
    s.add_argument('--out', required=True)
    s.add_argument('--replace', action='append', metavar='NAME=PATH',
                   help='replace one image path (repeatable)')
    s.add_argument('--repack', action='store_true',
                   help='ignore reference offsets and pack data')
    s.add_argument('--keep-compress-flag', action='store_true',
                   help='leave startup flags1 compression bits alone')
    s.add_argument('--image-off', type=lambda x: int(x, 0), default=None,
                   help='override the imagefs offset (default: listing)')
    s.add_argument('--quiet', action='store_true')
    s.set_defaults(func=build)

    s = sub.add_parser('check', help='verify an image with dumpifs')
    s.add_argument('--image', required=True)
    s.add_argument('--listing', required=True)
    s.add_argument('--dumpifs')
    s.add_argument('--compare-offsets', action='store_true')
    s.set_defaults(func=check)

    s = sub.add_parser('listing', help='capture a dumpifs -vv listing')
    s.add_argument('image')
    s.add_argument('--out')
    s.add_argument('--dumpifs')
    s.set_defaults(func=cmd_listing)

    s = sub.add_parser('selftest', help='offline self test')
    s.set_defaults(func=cmd_selftest)

    a = ap.parse_args(argv)
    if not getattr(a, 'func', None):
        ap.print_help()
        return 2
    return a.func(a)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
