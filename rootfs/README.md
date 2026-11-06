# `rootfs/` — the region-1 Linux root (a real BusyBox userland)

The Linux root filesystem that lives **on the eMMC**, in media region 1
(`/dev/hd2`, raw `0x28000000`), at region-1 offset **16 MiB** (raw `0x29000000`,
loader block `0x2915`).  Region 1 is inside the signed loader's `0x1B`
allow-list, so it can be written through the pre-OS `0xF7` loop with no MCT edit
(reports 98 §4, 134 §6, 138).

This is the **real root** since session 28 (ROADMAP item 5): the initramfs
mounts it read-write, bind-mounts `/dev` `/proc` `/sys`, chroots and execs
BusyBox `/sbin/init`.  It boots to a shell on `/dev/ttyGS0` and can load the
kernel's `=m` drivers from `/lib/modules/$(uname -r)`.

| path | what |
|---|---|
| `etc/inittab` | BusyBox init: rcS, then `ttyGS0::respawn:/bin/sh -l` |
| `etc/init.d/rcS` | mounts proc/sys/dev, tmpfs `/tmp` `/run`, hostname, `depmod -a` |
| `etc/profile` | `PATH`, `HOME`, **`PS1='pb> '`** (so `tools/pbcon.py` works on this shell) |
| `etc/motd` | the login banner: reboot, insmod, modprobe, dmesg, pbconsole |
| `etc/pbroot-version` | the version string |
| `init.c` | the small diagnostic `/init` used only when the media has no `/sbin/init` |
| `modules/pbhello.c` | the "modules loadable" proof module (built against `local/kernel-tree`) |

The build (`tools/build_rootfs.py`) adds the parts that are not source here:
BusyBox 1.36.1 (static, from `local/work/src/busybox-1.36.1`), its 401 applet
links, `/sbin/init`, the bring-up console as `/bin/pbconsole`, `pbrpmb` as
`/bin/pbrpmb`, and any `*.ko` from `--modules` under `/lib/modules/<uname -r>`
with a host `depmod`.  It produces a journal-free ext4, root-owned (built under
`fakeroot`) so a read-only mount cannot replay anything.

```bash
. tools/env.sh
python3 tools/build_rootfs.py --size-mb 64 \
  --busybox local/work/src/busybox-1.36.1/busybox \
  --busybox-links local/work/src/busybox-1.36.1/busybox.links \
  --modules local/work/modules
python3 tools/pre_os_ifs_flash.py \
  --payload local/images/rootfs-region1.ext4 --dest-raw 0x29000000
```

The full loop (BusyBox, modules, hand-off, both flashes) is
`docs/project/DEVELOPMENT.md` §3a.
