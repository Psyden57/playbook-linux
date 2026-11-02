# `rootfs/` — the region-1 Linux root (first cut)

The first Linux root filesystem that lives **on the eMMC**, in media region 1
(`/dev/hd2`, raw `0x28000000`), at region-1 offset **16 MiB** (raw
`0x29000000`, loader block `0x2915`).  Region 1 is inside the signed loader's
`0x1B` allow-list, so it can be written through the pre-OS `0xF7` loop with no
MCT edit (reports 98 §4, 134 §6, 138).

This tree is intentionally tiny: it exists to prove the **transport and the
mount**, not to be the final userland.  `postmarketOS`/`nura` come later.

| file | what |
|---|---|
| `init.c` | the static `/init` that runs from the image (writes one kmsg line and one console line, prints the version) |
| `etc/pbroot-version` | the version string that `init` reads |

Build the image with `tools/build_rootfs.py`; it produces
`local/images/rootfs-region1.ext4` (ext4, no journal, so a read-only mount
cannot replay anything).  Flash it with:

```bash
python3 tools/pre_os_ifs_flash.py \
  --payload local/images/rootfs-region1.ext4 --dest-raw 0x29000000
```

The initramfs `/init` mounts it read-only (`MS_RDONLY`, ext4 `noload`, a
`LO_FLAGS_READ_ONLY` loop over `/dev/mmcblk1`) and forks `/init` from it; the
console's `rootfs [off-hex]` command does the mount on demand for inspection.
