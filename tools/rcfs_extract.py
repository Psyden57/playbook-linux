#!/usr/bin/env python3
"""Read a QNX 8 / BB10 RCFS image ("rimh" container) without mounting it.

WHY THIS EXISTS
  `bb10mt qnx6 mount` cannot open a BB10 `/base`: the image is not a QNX6
  filesystem.  `bb10mt rcfs extract` does understand it, but it aborts the
  whole tree on the first chunk it cannot decompress, so one odd file hides
  the rest.  This reader parses the structure itself and, on a bad chunk,
  writes the raw packed bytes next to the target instead of stopping.

FORMAT (from local/vendor/bb10mt-main/rcfs.pas, verified against
Z10_10.3.03.2049_STL100-1.0.1.rcfs)
  header      @0x0000  "rimh", ver, zone_id[8], hash1[32], hash2[32],
                      verify_size, unc2                      (88 bytes)
  superblock  @0x1000  uuid[16], unk[16], "r-c-f-s", unk1, unk2,
                      image_size, inodes_count, inodes_off, inodes_size,
                      names_off, names_size, data_off, data_size (80 bytes)
  inode       @inodes_off + i*32
                      unk:u16 hash:u16 mode:u16 emode:u16
                      nameoffset:u32 offset:u32 size:u32 time:u32
                      uid:u32 gid:u32
  names       NUL-terminated strings at names_off
  directory   a directory inode's data is an array of 32-byte inodes
  compression emode bits 7:8 -- 0 = stored, 1 = chunked LZO1X with a
              32-bit offset table at the start of the packed data

USAGE
  tools/rcfs_extract.py list    <image.rcfs>
  tools/rcfs_extract.py extract <image.rcfs> <outdir> [--limit N]

READ-ONLY: the image is opened "rb" and never written.
"""

import argparse
import ctypes
import ctypes.util
import os
import struct
import sys

S_IFMT = 0xF000
S_IFDIR = 0x4000
S_IFREG = 0x8000
S_IFLNK = 0xA000

INODE_SIZE = 32
CHUNK = 0x4000
INODE_FMT = "<HHHHIIIIII"
INODE_FIELDS = ("unk", "hash", "mode", "emode", "nameoff", "offset",
                "size", "mtime", "uid", "gid")


class Rcfs:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.size = os.fstat(self.f.fileno()).st_size
        self.f.seek(0)
        hdr = self.f.read(88)
        if hdr[:4] == b"rimh":
            (self.ver,) = struct.unpack_from("<I", hdr, 4)
            self.zone_id = hdr[8:16]
            self.hash1 = hdr[16:48]
            self.hash2 = hdr[48:80]
            self.verify_size, self.unc2 = struct.unpack_from("<II", hdr, 80)
        else:
            # Earlier images (10.0.9.1103) carry the "r-c-f-s" superblock at
            # 0x1000 but no "rimh" header; the layout from there on is the same.
            self.ver = 0
            self.zone_id = b""
            self.hash1 = self.hash2 = b""
            self.verify_size = self.unc2 = 0
        sb = self.read_at(0x1000, 80)
        if sb[32:39] != b"r-c-f-s":
            raise SystemExit("%s: superblock magic %r" % (path, sb[32:39]))
        (self.image_size, self.inodes_count, self.inodes_off,
         self.inodes_size, self.names_off, self.names_size,
         self.data_off, self.data_size) = struct.unpack_from("<IIIIIIII", sb, 48)

    def read_at(self, off, length):
        self.f.seek(off)
        return self.f.read(length)

    def inode(self, idx):
        # Inode numbers are 1-based (rcfs.pas GetInode: inodes_off + (idx-1)*32).
        raw = self.read_at(self.inodes_off + (idx - 1) * INODE_SIZE, INODE_SIZE)
        if len(raw) != INODE_SIZE:
            raise SystemExit("inode %d out of range" % idx)
        return dict(zip(INODE_FIELDS, struct.unpack(INODE_FMT, raw)))

    def read_name_at(self, off):
        if not off:
            return ""
        self.f.seek(off)
        buf = bytearray()
        while True:
            c = self.f.read(1)
            if not c or c == b"\0":
                break
            buf += c
        return buf.decode("utf-8", "replace")

    def name(self, idx):
        return self.read_name_at(self.inode(idx)["nameoff"])

    def dir_entries(self, ino):
        raw = self.read_at(ino["offset"], ino["size"])
        return [dict(zip(INODE_FIELDS, struct.unpack(INODE_FMT, raw[i:i + INODE_SIZE])))
                for i in range(0, len(raw) - INODE_SIZE + 1, INODE_SIZE)]

    def packed_chunks(self, ino):
        """LZO chunk list [(image_offset, length)], or None if stored."""
        if ((ino["emode"] >> 7) & 3) != 1:
            return None
        raw = self.read_at(ino["offset"], ino["size"])
        if len(raw) < 4:
            return []
        (first,) = struct.unpack_from("<I", raw, 0)
        first = min(first, len(raw))
        table = list(struct.unpack_from("<%dI" % (first // 4), raw, 0))
        return [(ino["offset"] + table[i], table[i + 1] - table[i])
                for i in range(len(table) - 1)]


_lzo_fn = None


def lzo_decompress(src):
    global _lzo_fn
    if _lzo_fn is None:
        name = ctypes.util.find_library("lzo2") or "liblzo2.so.2"
        fn = ctypes.CDLL(name).lzo1x_decompress_safe
        fn.restype = ctypes.c_int
        fn.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                       ctypes.POINTER(ctypes.c_size_t), ctypes.c_void_p]
        _lzo_fn = fn
    out_len = max(CHUNK, len(src) * 8, 1 << 16)
    for _ in range(6):
        out = ctypes.create_string_buffer(out_len)
        n = ctypes.c_size_t(out_len)
        rc = _lzo_fn(src, len(src), out, ctypes.byref(n), None)
        if rc == 0:
            return out.raw[:n.value]
        if rc != -5:  # LZO_E_OUTPUT_OVERRUN; anything else is a bad stream
            return None
        out_len *= 4
    return None


class Stats:
    def __init__(self):
        self.files = self.links = self.other = 0
        self.lzo = self.stored = 0
        self.bad_chunks = self.bad_files = 0
        self.link_errors = 0


def extract_file(fs, ino, path, stats):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as dst:
        chunks = fs.packed_chunks(ino)
        if chunks is None:
            dst.write(fs.read_at(ino["offset"], ino["size"]))
            stats.stored += 1
            return
        for i, (off, ln) in enumerate(chunks):
            blob = fs.read_at(off, ln) if ln else b""
            data = lzo_decompress(blob) if ln else b""
            if data is None:
                stats.bad_chunks += 1
                stats.bad_files += 1
                if stats.bad_files <= 10:
                    print("  bad chunk %d in %s (%d bytes, emode=0x%04x)"
                          % (i, path, ln, ino["emode"]), file=sys.stderr)
                dst.write(blob)
            else:
                dst.write(data)
        stats.lzo += 1


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("list")
    p.add_argument("image")
    p = sub.add_parser("extract")
    p.add_argument("image")
    p.add_argument("outdir")
    p.add_argument("--limit", type=int, default=None)
    a = ap.parse_args()

    fs = Rcfs(a.image)
    print("rimh v%d  image_size=%d  inodes=%d  names@0x%x+0x%x  data@0x%x+0x%x"
          % (fs.ver, fs.image_size, fs.inodes_count, fs.names_off,
             fs.names_size, fs.data_off, fs.data_size))
    stats = Stats()

    def walk(ino, prefix):
        name = fs.read_name_at(ino["nameoff"])
        path = os.path.join(prefix, name) if name else prefix
        kind = ino["mode"] & S_IFMT
        if a.cmd == "list":
            k = {S_IFDIR: "d", S_IFREG: "-", S_IFLNK: "l"}.get(kind, "?")
            comp = (ino["emode"] >> 7) & 3
            print("%s %04o %9d %s%s" % (k, ino["mode"] & 0o7777, ino["size"],
                                        path, "  [lzo]" if comp == 1 else ""))
            if kind == S_IFDIR:
                for ent in fs.dir_entries(ino):
                    walk(ent, path)
            return
        dst = os.path.join(a.outdir, path)
        if kind == S_IFDIR:
            os.makedirs(dst, exist_ok=True)
            for ent in fs.dir_entries(ino):
                walk(ent, path)
        elif kind == S_IFREG:
            stats.files += 1
            if a.limit is None or stats.files <= a.limit:
                extract_file(fs, ino, dst, stats)
        elif kind == S_IFLNK:
            stats.links += 1
            target = fs.read_at(ino["offset"], ino["size"]) if ino["size"] else b""
            try:
                os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
                if os.path.lexists(dst):
                    os.unlink(dst)
                os.symlink(target.decode("utf-8", "replace"), dst)
            except OSError:
                stats.link_errors += 1
        else:
            stats.other += 1

    walk(fs.inode(1), "")
    print("files=%d links=%d other=%d lzo=%d stored=%d bad_chunks=%d bad_files=%d"
          % (stats.files, stats.links, stats.other, stats.lzo, stats.stored,
             stats.bad_chunks, stats.bad_files))


if __name__ == "__main__":
    main()
