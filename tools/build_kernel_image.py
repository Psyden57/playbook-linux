#!/usr/bin/env python3
"""Build the CRC-path kernel image: chainloader + zImage + DTB.

Layout (Variant B): base IFS 0x2BFE10, zImage at 0x2C0000, DTB after it.
The chainloader copies both to 0x84000000 (kexec-proven high DRAM) and jumps.
"""
import os, sys, struct, zlib, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE_IMG = ROOT + '/local/images/ifs0.sealed.bin'
ZIMAGE = os.environ.get('ZIMAGE', ROOT + '/local/kernel-tree/arch/arm/boot/zImage')
DTB = os.environ.get('DTB', ROOT + '/local/kernel-tree/arch/arm/boot/dts/ti/omap/omap4-winchester.dtb')
TPL = ROOT + '/boot/chainloader/chainloader_kernel.S.in'
OUTDIR = ROOT + '/boot/chainloader'
# The chainloader is assembled with the QNX 6.6 ARM toolchain, which is not part
# of this repository.  Order: an explicit override (QNX660 / Q / QNX_HOST), then
# the workspace payload (local/toolchains/qnx660-master, set up by tools/env.sh),
# then ~/qnx660-master.  The old …/wsl-migration location is not referenced.
def _default_qnx():
    for var in ('QNX660', 'Q'):
        v = os.environ.get(var)
        if v:
            return v
    host = os.environ.get('QNX_HOST')
    if host:
        return os.path.dirname(os.path.dirname(host))
    local = ROOT + '/local/toolchains/qnx660-master'
    if os.path.isdir(local):
        return local
    return os.path.expanduser('~/qnx660-master')

Q = _default_qnx()
AS = Q + '/host/linux/x86/usr/bin/arm-unknown-nto-qnx6.6.0eabi-as'
LD = Q + '/host/linux/x86/usr/bin/arm-unknown-nto-qnx6.6.0eabi-ld'
OC = Q + '/host/linux/x86/usr/bin/arm-unknown-nto-qnx6.6.0eabi-objcopy'


def require_qnx_tools():
    if not os.access(AS, os.X_OK):
        raise SystemExit(
            'QNX 6.6 toolchain not found: %s\n'
            'It lives in local/toolchains/qnx660-master on the bench host; '
            'run ". tools/env.sh" first, or set QNX660=/path/to/qnx660-master, '
            'or QNX_HOST=.../host/linux/x86.' % AS)

EP_TICKS = int(os.environ.get('EP_TICKS', '0'), 0)   # epoch-test delay, WDT2 32 kHz ticks

BASE = 0x800FF800
ZOFF = 0x2C0000
MAX_END = 0xB00000          # IFS zone: 0x100000..0xC00000
KDEST = 0x84000000


def align(x, a):
    return (x + a - 1) // a * a


def main():
    require_qnx_tools()
    base = bytearray(open(BASE_IMG, 'rb').read())
    if len(base) != 0x2BFE10:
        raise SystemExit('base is 0x%X' % len(base))
    z = open(ZIMAGE, 'rb').read()
    d = open(DTB, 'rb').read()

    zoff = ZOFF
    dtoff = align(zoff + len(z), 8)
    # Reserve the IFS signature-trailer region AFTER the DTB.  The trailer
    # magic/CRC at end-8/end-4 must not land inside the appended DTB, or the
    # kernel's FDT parser sees a corrupt string/struct block.
    end = align(dtoff + len(d) + 0x230, 4)
    if end > MAX_END:
        raise SystemExit('image 0x%X exceeds IFS zone 0x%X' % (end, MAX_END))

    zsrc = BASE + zoff
    dtsrc = BASE + dtoff
    ddest = KDEST + align(len(z), 8)

    subs = {
        '__ZIMAGE_SRC__': '0x%08X' % zsrc,
        '__ZIMAGE_SIZE__': '0x%08X' % len(z),
        '__DTB_SRC__': '0x%08X' % dtsrc,
        '__DTB_SIZE__': '0x%08X' % len(d),
        '__KERNEL_DEST__': '0x%08X' % KDEST,
        '__DTB_DEST__': '0x%08X' % ddest,
        '__EP_TICKS__': '0x%08X' % EP_TICKS,
    }
    asm = open(TPL).read()
    for k, v in subs.items():
        asm = asm.replace(k, v)
    asm_path = OUTDIR + '/chainloader_kernel.S'
    open(asm_path, 'w').write(asm)

    for cmd in ([AS, '-march=armv7-a', '-o', OUTDIR + '/chainloader_kernel.o', asm_path],
                [LD, '-Ttext=0x80100000', '-e', '_start', '-o',
                 OUTDIR + '/chainloader_kernel.elf', OUTDIR + '/chainloader_kernel.o'],
                [OC, '-O', 'binary', OUTDIR + '/chainloader_kernel.elf',
                 OUTDIR + '/chainloader_kernel.bin']):
        r = subprocess.run(cmd, capture_output=True, env=dict(os.environ, QNX_TARGET=Q + '/target/qnx6'))
        if r.returncode != 0:
            raise SystemExit('build failed: %s\n%s\n%s' % (cmd, r.stdout.decode(), r.stderr.decode()))
    code = open(OUTDIR + '/chainloader_kernel.bin', 'rb').read()

    img = base + bytearray(end - len(base))
    img[zoff:zoff + len(z)] = z
    img[dtoff:dtoff + len(d)] = d
    if 0x800 + len(code) > 0x2BFBE0:
        raise SystemExit('chainloader too large')
    img[0x800:0x800 + len(code)] = code

    struct.pack_into('<I', img, 0x08, (BASE + end) & 0xFFFFFFFF)
    struct.pack_into('<I', img, end - 8, 0xD7C82D1F)
    crc = zlib.crc32(bytes(img[:end - 4])) & 0xFFFFFFFF
    struct.pack_into('<I', img, end - 4, crc)

    out = OUTDIR + '/kernel.img'
    open(out, 'wb').write(img)
    print('chainloader: %d B at 0x80100000  (EP_TICKS=0x%X = %d ms)' %
          (len(code), EP_TICKS, EP_TICKS * 1000 // 32768))
    print('zImage     : %d B at image 0x%X (load 0x%08X) -> 0x%08X' % (len(z), zoff, zsrc, KDEST))
    print('DTB        : %d B at image 0x%X (load 0x%08X) -> 0x%08X' % (len(d), dtoff, dtsrc, ddest))
    print('image      : 0x%X bytes; LoadEnd 0x%08X; crc 0x%08X' % (end, BASE + end, crc))
    print('wrote %s' % out)


if __name__ == '__main__':
    main()
