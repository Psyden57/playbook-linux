# 252 — The app layer: touch reaches xdg clients, and gorm brings up Wi‑Fi

Date: 2026-11-16 (session 60, Linux-era 54).
Bench: the Nura root read-write from hd3, region 0 `0x294F8E36` (kernel
`7.3.3 #77`).  `epitaph` r10005 runs; `tzompantli` (1.2.0) and `gorm` (1.2.1)
are installed; NetworkManager is running.

## 0. Headline

* **Touch does reach xdg toplevels.**  After the panel fix, an injected
  touchscreen tap on tzompantli's Foot icon launched `foot` (tzompantli exits
  on a successful spawn), and gorm responds after it is launched into a clean
  workspace.
* **The earlier "tapping does nothing" was a window state, not a touch
  pipeline.**  The first tzompantli (launched from SSH alongside other windows)
  stayed alive, logged no launch error and ignored taps; a fresh fullscreen
  launch accepted the same injected tap.  Its grid rendered 3 columns (scale 2)
  once and 6 columns (scale 1) later — the surface scale/configure varies.
* **Wi‑Fi was disabled in NetworkManager**, not broken: `nmcli radio wifi` was
  `disabled`, so `wlan0` was "unavailable".  `nmcli radio wifi on` makes it
  scan (~10 APs, both bands), and gorm lists them.
* **The ~2 s interaction latency is not the GPU driver.**  A Wayland EGL
  client gets **softpipe** here; each slider move redraws the whole client
  surface and the compositor re-uploads it.  The profile build is the next
  measurement.

## 1. The injection probe

The kernel has `CONFIG_INPUT_UINPUT=m` but the module is not in the Nura root.
Built from the tree and loaded (trap 306):

```
make -C local/kernel-tree ARCH=arm CROSS_COMPILE=arm-linux- \
     M=drivers/input/misc uinput.ko
# pushed paced, insmod
input: playbook-tap as /devices/virtual/input/input3
```

`tools/bench/uitap.py <x> <y>` creates a virtual touchscreen (`EV_ABS`
multi-touch slots, `BTN_TOUCH`, `INPUT_PROP_DIRECT`), emits one down/up and
destroys it.  udev tags it `ID_INPUT_TOUCHSCREEN=1`; catacomb hotplugs it
(`/dev/input/eventN` appears in `/proc/<catacomb>/fd`).  The `UI_SET_*` ioctls
take the bit value directly — passing a `struct.pack("i", bit)` pointer gives
`EINVAL`.

## 2. Touch to xdg clients

With tzompantli the only, fullscreen window:

```
$ python3 /tmp/uitap.py 168 214      # the Foot icon
tap sent at 168,214
$ pgrep -a foot
14266 foot
$ pgrep -a tzompantli                 # exits on a successful spawn
```

Injected later at (82,210) on a 6-column layout it also launched foot, and on
the panel it opened epitaph's drawer (a layer surface).  So the compositor
forwards touch to both layer-shell and xdg surfaces.

**The false lead.**  The first failing instance coincided with wvkbd running,
and killing wvkbd appeared to fix it.  It did not: a visible wvkbd also
accepted a tap once the app was launched cleanly.  The difference was the
window state (trap 307), not the keyboard.  Do not "fix" this by changing the
OSK.

## 3. gorm and Wi‑Fi

Gorm is the Wi‑Fi settings GUI.  It needs its own polkit rule (shipped in its
source, not installed by the package):

```
/etc/polkit-1/rules.d/10-gorm.rules
  NetworkManager.wifi.scan / network-control /
  settings.modify.system / enable-disable-wifi  → group catacomb
```

Measured:

```
$ nmcli radio wifi
disabled
$ nmcli dev status          → wlan0:wifi:unavailable
$ nmcli radio wifi on
$ nmcli dev status          → wlan0:wifi:disconnected
$ nmcli -t dev wifi list --rescan yes
 :C6:ED:DC:EE:B0:CF:MovistarFibra-EEB0C8:Infra:5 GHz:36:1170 Mbit/s:39:▄▄▄_
 …  (≈10 APs, 2.4 and 5 GHz)
```

`wlcore` reports `firmware booted (Rev 7.3.5.0.98)` throughout, and the
per-unit MAC `14:74:11:8f:9c:c3` is the `permaddr`.  Gorm's screen then lists
the APs with signal and lock icons.  Joining an AP (a password prompt) was not
attempted.

## 4. The interaction latency

`epitaph` is a Wayland EGL client, and the Mesa Amber EGL on this stack gives
it `softpipe` (`GL renderer "softpipe", … Mesa 21.3.9 Amber`); only the
compositor's GBM/DRM path selects `pvr_dri`.  The client redraws the whole
drawer surface on each touch motion (`set_value` is called in
`Drawer::touch_motion`, not on release) and catacomb uploads that surface as a
texture each commit.  A 1024×600 CPU redraw is milliseconds, so the ~2 s is
either the compositor's upload/commit path or the renderer's stall/frame
handling — the `PBGL_PROFILE` catacomb build is the instrument.  The driver is
not "faulty": the compositor composites on the SGX540 and the cursor,
wallpaper and gesture handle are responsive.

## 5. Remaining

* Why the first window instance ended up tiny/stale is unconfirmed (trap 307);
  relaunching in the workspace is the workaround.
* The ~2 s latency: profile before changing anything.
* The restorable hd3 image predates r10005, the udev/polkit rules and
  `10-gorm.rules`; rebake before flashing.
