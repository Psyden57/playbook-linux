# Desktop permissions and swap for the PlayBook bench (session 70)

Three small rootfs changes, each fixing a defect measured on the unit.  All live
in `/etc` on hd3 and **must be baked into the next rebake** or a reflash loses
them (trap-343 class).

| file here | install as | fixes |
|---|---|---|
| `polkit/50-playbook-desktop.rules` | `/etc/polkit-1/rules.d/50-playbook-desktop.rules` (root:polkitd 0640) | Wi-Fi scan/connect/enable-disable and brightness denied to the desktop user |
| `udev/90-playbook-backlight.rules` | `/etc/udev/rules.d/90-playbook-backlight.rules` | `/sys/class/backlight/*/brightness` is root-only 0644, so the bar's slider does nothing |
| `deviceinfo.d/bench-overrides` | `/etc/deviceinfo` | `postmarketos-zram-swap` defaults to `zstd`, which this kernel does not have, so zram never came up |

## Measured before/after

**Brightness** — was `Permission denied` for `playbook`; now
`echo 255 > /sys/class/backlight/backlight/brightness` succeeds.  Two things were
needed, and the second was the real blocker:

1. The udev rule, to make the node `root:video 0664`.
2. **`playbook` was not a member of `video`** (`groups=wheel,input,netdev,plugdev`).
   `addgroup playbook video` fixes that.  *Without this the udev rule alone
   changes nothing* — the group is the actual permission.

**Wi-Fi** — `nmcli radio wifi` reported "not authorized to control networking".
Now `nmcli radio wifi on` works and `nmcli -t dev wifi list --rescan yes` returns
**20 APs** as `playbook`.  The exact call the bar makes also works:
`Properties.Set org.freedesktop.NetworkManager WirelessEnabled true` → `()`,
verified off→on.

**zram** — `postmarketos-zram-swap` failed with
`zramctl: /dev/zram0: failed to set algorithm: Invalid argument` because
`zramstart` defaults to `zstd` while the kernel has only `lzo-rle`/`lzo`
(`CONFIG_ZRAM_BACKEND_FORCE_LZO=y`).  With the override: **1489 MB of lzo-rle
swap active**, no error.

## Notes and traps

* **The udev rule needs `SUBSYSTEM=="backlight"` only — no `KERNEL==` match.**
  This backlight is a class device with **no devnode**: `udevadm info -q all -p
  /sys/class/backlight/backlight` reports `E: SUBSYSTEM=backlight` and nothing
  else, so a `KERNEL=="backlight"` match silently never fires.  That was the
  first attempt.
* `polkit` re-reads `rules.d` on change, but a **long-running client keeps its
  cached D-Bus introspection** — `epitaph` had to be restarted to notice the new
  permissions.  **Restart it with the session environment** (`MESA_LOADER_
  DRIVER_OVERRIDE=pvr`, `DBMFIX=1`, `LD_PRELOAD=/usr/local/lib/dbmfix.so`,
  `XDG_SESSION_*`), copied from `catacomb`'s `/proc/<pid>/environ`; a bare
  restart lands it on `swrast_dri` and the bar becomes unusably slow.
* **zram will not be reachable from the bar.**  The bar's Wi-Fi button writes the
  NM property, which works; if the icon does not repaint afterwards that is an
  `epitaph` refresh issue, not a permission one.
* The bar's Wi-Fi toggle logs `Failed to populate properties cache via GetAll:
  Object does not exist at path "/"` — epitaph proxies
  `org.freedesktop.ModemManager` (**no `1`**); the bus only carries
  `org.freedesktop.ModemManager1`.  Cosmetic (it is the cellular proxy), but it
  means MM property-change streams never deliver.
