# 113 — Static RE of the PlayBook `/base` USB device stack (OMAP4430 MUSB DCD)

Scope: the real TabletOS root filesystem now dumped at
`/home/psyden/SSDdrive/4869/berry-rcfs/` (build 79475), read **statically only** — no
target, no USB, no ssh, no flashing, nothing modified outside this file.

This is the file report **110** could not have: 110 established (from the IFS-only
dump) that the USB driver lived on `/base` and was therefore invisible, and report
**112** measured on the bench that MUSB is already in device mode with a valid
session (`MUSB_DEVCTL = 0x80`, `USBOTGHS_CONTROL = 0x15`). With `/base` dumped we
can now read the actual driver, its PMIC code, its descriptors, and the manager's
ordering — and answer 110's open questions directly.

Method: `file`/`readelf -lS -n -r --dyn-syms`/`strings -a -t x` on every USB-related
object; targeted `arm-linux-objdump -d` (binutils 2.43.1, the ARM toolchain at
`/home/psyden/SSDdrive/wsl-migration/home/psyden/toolchains/armv7-eabihf/bin/`) with
per-function address ranges; PC-relative literal resolution computed with
`python3` against the same files (`GOT base` = `0x9000` for `devu-omap4430-musb.so`
and for the `-musbmhdrc` DCDs, `0x1d000` for `RIM_usbmgr-Winchester`) so every
`"%s ..."` slog argument could be tied to its format string; register/bit
cross-checks against `swpu231ap.pdf.txt`; and cross-checks of PMIC register
semantics against the local 6.15.11 tree
(`drivers/usb/phy/phy-twl6030-usb.c`, `drivers/regulator/twl6030-regulator.c`).

Note on ELF layout: all objects here are QNX/ARM ELF32 `ET_DYN`, and for the
`R-X` `LOAD` segment **file offset == vaddr** in every file used below, so
addresses in this report are simultaneously virtual addresses and file offsets.
The `-musbmhdrc` DCDs have `.data` at vaddr `0x9118` / file `0x8118` (`0x800`
shift) — that is where the descriptor blobs live.

---

## 0. TL;DR

| question | answer | evidence |
|---|---|---|
| **Which PHY?** | **The OMAP4430 internal HS USB2 PHY (UTMI+, 8-bit, UTMI+ level 3).** The DCD explicitly selects it: `OTG_INTERFSEL (0x4A0AB40C) &= ~7` ⇒ `PHYSEL = 0`. Its reset value is `0x1` = *ULPI*, so this write is mandatory. | §2.3 step 2; TRM Table 23-1198 (`PHYSEL` reset `0x1`, `0` = 8-bit UTMI+ level 3) |
| External ULPI PHY or TWL6030 data PHY? | **Neither.** Zero ULPI register accesses in any binary (no `0x4A062800`/TLL access at all in the device path). The TWL6030 is **not** the data PHY — it only supplies the 3.3 V `VUSB` rail, the VBUS/ID comparators, and one VBUS status bit that software copies into the SoC mailbox. | §2.3, §3.4 |
| **Exact startup register writes** | **5 MMIO writes + 1 read-modify-write** on the device path, all in `musb_custom_init1`/`musb_custom_init2` (see §2.3). Plus 7 TWL6030 i2c writes **which are disabled in the shipped configuration** (`no_cfg_pmic`). | §2.3, §2.4 |
| `CONTROL_USBOTGHS_CONTROL` value and when | **`0x15`** = `AVALID|VBUSVALID|IDDIG` = B-device with VBUS. Written by **`RIM_usbmgr-Winchester`**, *before* it spawns the device stack — **not** by the DCD. Cleared to `0` at usbmgr init, set to `0x08` (`SESSEND`) on teardown. Bench value `0x15` (report 112) is exactly this. | §4.1 |
| MUSB soft reset | Via the **OTG bank**, not a MUSB register: `OTG_SYSCONFIG (0x4A0AB404) |= SOFTRESET(b1)`, `usleep(1)`, then poll `OTG_SYSSTATUS (0x4A0AB408)[0] RESETDONE` up to 999 more µs; failure ⇒ `"Reset Failed: SYSSTATUS_RESET_DONE still low"`. | §2.2 |
| `OTG_SYSCONFIG` final value | **`0x0000100D`** (`MIDLEMODE=1` no-standby, `SIDLEMODE=1` no-idle, `ENABLEWAKEUP=1`, `AUTOIDLE=1`) — or `0x2015` if the `smartclock` option is given. Reset value is `0x2011`, i.e. the driver deliberately kills smart-idle/standby. | §2.3 step 1; TRM Table 23-1194 |
| `MUSB_POWER` / FADDR handling | `POWER.SOFTCONN (b6)` is **de-asserted at init** (`linkdown` is the default) and asserted later by `ulink_ctrl -l1`. `POWER.HSENAB (b5)` is cleared **only** if the `forcefs` option is passed (it is not). `FADDR` is written **1 ms after** the SET_ADDRESS CSR0 ack. | §2.5, §2.6 |
| Endpoint FIFO config | 16 KB of MUSB FIFO RAM (`musb_cfg_get` → `0x4000`), 16 endpoints, managed by an **8-byte-granule free list**; per endpoint at enable time: `INDEX=n`, `TXFIFOSZ`/`RXFIFOSZ` = log2(size>>3), `TXFIFOADD`/`RXFIFOADD` = offset/8, `TXMAXP`/`RXMAXP` = wMaxPacketSize. | §2.7 |
| Expected `MUSB_DEVCTL` transitions | **The driver never writes `DEVCTL`.** All `DEVCTL` state is produced by the OMAP OTG state machine from the `CONTROL_USBOTGHS_CONTROL` mailbox + the PHY's VBUS/ID comparators. `0x80` (`BSESSVLD=1, SESSEND=0, IDDIG=0`) is the healthy B-device value and it is what report 112 measured. | §2.8 |
| **Descriptors** | `0fca:8010` = **RNDIS**, `0fca:8011` = **ECM**, `0fca:8012` = rndis_mux, `0fca:8013` = ecm_mux, `0fca:8020` = mass storage, `18d1:2d00` = Android Open Accessory. Manufacturer `"Research In Motion, Ltd."`, product `"RIM Network Device"` (mux variants `"BlackBerry"`), serial from NV (`ser=`), default `"0123-4567-89ab-cdef"`; ECM MAC string `"000200000002"`. | §3 |
| **Required ordering** | usbmgr: `0x233C = 0` → PPS `usbconnector` listener → **`0x233C = 0x15`** → spawn `/scripts/usb_device.sh <mode>` → `io-usb-dcd` → function driver → **10 ms** → **`ulink_ctrl -l1`** (assert SOFT-CONNECT). No GPIO step; no PMIC write is required from usbmgr (it passes `no_cfg_pmic`). | §4 |
| **PMIC step we might be missing** | Nothing, if the mailbox is right: `VUSB` is left in ACTIVE/AMS by the IFS (`set_trans vusb sleep=ams act=ams`, report 110), and all TWL USB-comparator programming is **skipped by design** in the shipped config. The only TWL datum the software path actually consumes is **`TWL6030 USB-page reg 0xE3 bit 2` = VBUS present** (used by the optional poll thread). | §2.4, §4.3 |
| What a Linux MUSB gadget port **must** copy | (a) `OTG_INTERFSEL.PHYSEL = 0`; (b) `CONTROL_DEV_CONF.USBPHY_PD = 0`; (c) `CM_L3INIT_USBPHY_CLKCTRL` with `OPTFCLKEN_PHY_48M`; (d) `CM_L3INIT_HSUSBOTG_CLKCTRL` with `OPTFCLKEN_UTMI_P*`; (e) `OTG_SYSCONFIG = 0x100D` **after** the SOFTRESET poll; (f) `INTRUSBE = 0x27`; (g) `POWER.SOFTCONN` sequencing; (h) `CONTROL_USBOTGHS_CONTROL = 0x15`. | §6 |
| What it **can skip** | TWL6030 i2c programming (8 writes + 8 save-reads), the `OTG_SYSCONFIG` smart-clock variant, the VDMA engine (`nodma`/PIO is what the DCD actually uses for EP0; see §2.7), the OMAP TLL/EHCI/ULPI path entirely. | §6 |

---

## 1. Binary inventory

### 1.1 The device path, end to end (proven from strings + scripts)

```
/base/scripts/startup.sh  →  SERVICE_usbmgr  →  RIM_usbmgr-Winchester
    (run level 0 list: "… powerman … coreservices … usbmgr", startup.sh:1684)
        │  usbmgr sets env from its baked-in defaults (§4.1) and/or
        │  /var/usb/usbd/system/config/usbmgr.conf (empty in this build)
        ▼
/base/scripts/usb_device.sh <mode>
        │  DRIVER_PROTOCOL = RIM_ecm | RIM_rndis | RIM_ecm_mux | RIM_rndis_mux |
        │                    RIM_aap | RIM_usbumass
        │  USB_HW_DRIVER_NAME = "<protocol>-<USB_DEVICE_DRVR>"  = "<proto>-omap4430-musbmhdrc"
        ▼
io-usb-dcd -d RIM_ecm-omap4430-musbmhdrc \
           ioport=0x4a0ab000,irq=124,chargerpoll=2000000,chargerthresh=1,\
           ser=<serial>,no_cfg_pmic,no_session_poll,inherit_cfg
        ▼
lib/dll/devu-RIM_ecm-omap4430-musbmhdrc.so     (function driver + MUSB hardware layer,
                                                statically linked together)
        ▼
lib/dll/devnp-usbdnet.so   (ECM)   /  devn-usbrndis-device.so (RNDIS)
        ▼
io-pkt-v6-hc  →  if_up ecm0 / rndis0  →  dhcp.client
```

The *host* path uses the same hardware layer inside `devu-omap4430-musb.so`
(banner: `"MUSBMHDRC OTG Host Controller Driver"`, loaded by `io-usb -d omap4430-musb`).

### 1.2 Binary facts

| path (under `berry-rcfs/`) | size | Build ID | QNX_info | role |
|---|---|---|---|---|
| `lib/dll/devu-omap4430-musb.so` | 40,192 | `c60a32b2c336300ac0448f6c0a8f8c52` | "MUSBMHDRC OTG Host Controller Driver", 2011‑07‑27, gusbuild5, TAGID 1783 | MUSB hardware layer, **host/OTG** flavour (`.text` `0x2030`+`0x5838`, `.rodata` `0x7870`, `io_usb_dll_entry` obj @ `0xa164`) |
| `lib/dll/devu-RIM_ecm-omap4430-musbmhdrc.so` | 36,420 | `8232e5c1c1022a7d151d31574c5bd880` | "Mentor Graphics MUSBMHDRC peripheral controller / ECM Network device", 2011‑05‑27, TAGID 1443 | **ECM device stack** (the main subject) |
| `lib/dll/devu-RIM_rndis-omap4430-musbmhdrc.so` | 36,440 | `669dcf6e1c8cbd6d1603f76ce777ff61` | same, /RNDIS/ | RNDIS device stack |
| `lib/dll/devu-RIM_ecm_mux-omap4430-musbmhdrc.so` | 36,568 | `6823f16430ccc71cd0a6610d6009c759` | 2011‑07‑20, TAGID 1748 | ECM + mux |
| `lib/dll/devu-RIM_rndis_mux-…musbmhdrc.so` | 36,580 | `4973840a73912f3f24e4d1c9d54cb63b` | 2011‑07‑16, TAGID 1729 | RNDIS + mux |
| `lib/dll/devu-RIM_aap-omap4430-musbmhdrc.so` | 36,136 | `22e7002834c3ba2b2ca9242780496161` | 2011‑10‑20, TAGID 287 | Android Open Accessory |
| `lib/dll/devu-RIM_usbumass-omap4430-musbmhdrc.so` | 36,148 | `c1c2a07ef2a10a823f26cc94a27302b9` | 2011‑05‑27, TAGID 1443 | mass storage |
| `lib/dll/devnp-usbdnet.so` | 77,608 | `7688daf454a93f03f688e6f6af278688` | 2011‑11‑08, TAGID 364 | ECM network driver (host side of `ecm0`) |
| `lib/dll/devn-usbrndis-device.so` | 69,908 | `83d36a9d1df01ecd3ca0328a12fbf6b5` | 2011‑11‑08 | RNDIS network driver |
| `lib/dll/devu-ehci-omap4430-winchester.so` | 56,680 | `6675d2e785c13240e76da1f56a5040fd` | 2011‑11‑11 | EHCI HCD (host path only; the "404-SKU modem probe" of report 110) |
| `lib/dll/devu-omap4430-mg.so` | 48,192 | `10e3440f1277a6910a67cadbc213ce54` | 2011‑11‑11 | Mentor generic HCD; **no** OM4430 custom layer (not used by the device path) |
| `sbin/io-usb-dcd` | 135,060 | `bff352d068fdb889e704ed5ddeec811f` | 2011‑10‑17, TAGID 276 | QNX USB **device** server (`-d dll [opts]`) |
| `sbin/io-usb` | 122,984 | `4146af5b32a5c18013462828d191385a` | 2011‑10‑17 | QNX USB **host** server |
| `sbin/RIM_usbmgr-Winchester` | 123,480 | `ac8e3eac6d6678285a8363291e1e95fd` | 2011‑10‑19, TAGID 285 | USB mode manager (`/dev/RIM_usbmgr`) |
| `sbin/ulink_ctrl` | 27,176 | `f94ca1458da27ce80d7a5722d064673f` | 2011‑05‑27, TAGID 1443 | **SOFT-CONNECT (link) control**: `-l1` connect, `-l0` disconnect |
| `sbin/winchester_phy_ctrl` | 23,324 | `7f684ac43876d59bbb23d2e6e66cc6a4` | 2011‑05‑27, TAGID 1443 | powerman-driven **PHY power gate** (§4.4) |
| `sbin/rimusb` | 43,316 | `cacc534b2f733c84ca1aea1caa24c2f0` | 2011‑10‑17 | user-level RIM USB utility |
| `sbin/probe_ehci_udev_winchester` | 30,768 | `f4c8bda2ccc6b280513f864df3c8017a` | 2011‑08‑03 | radio/modem probe (host path; report 110) |
| `sbin/usb`, `sbin/enum-usb`, `lib/dll/devh-usb.so` | 35,308 / 23,516 / 19,736 | `0535d3df…` / `fe15daeb…` / `349daf6d…` | | enumeration helpers / device-hid |

The stripped binaries still export a **full `.dynsym` function table** (all the
`musb_*` and `fifomem_*` symbols are `GLOBAL FUNC`), which is what makes this
analysis cheap. Key entry points used below:

| object | function | addr |
|---|---|---|
| `devu-omap4430-musb.so` | `musb_controller_init` / `musb_custom_init1` / `musb_custom_reset` / `musb_custom_init2` / `musb_custom_fini1` / `musb_custom_fini2` / `twl6030_i2c_write` / `twl6030_i2c_read` / `fifomem_init` / `musb_endpoint_enable` | `0x3624` / `0x633c` / `0x5e7c` / `0x5f18` / `0x630c` / `0x6080` / `0x5fb4` / `0x6234` / `0x2d0c` / `0x3e58` |
| `devu-RIM_ecm-…-musbmhdrc.so` | `musb_init` / `musb_process_args` / `musb_custom_init1` / `musb_custom_reset` / `musb_custom_init2` / `musb_custom_fini2` / `twl6030_i2c_write` / `twl6030_i2c_read` / `fifomem_init` / `musb_endpoint_enable` / `musb_set_device_address` / `musb_set_bus_state` | `0x2f84` / `0x2cb8` / `0x5930` / `0x5144` / `0x55dc` / `0x5200` / `0x5510` / `0x5350` / `0x290c` / `0x3734` / `0x4460` / `0x3e64` |

---

## 2. `devu-omap4430-musb.so` — controller / PHY init, decoded

Two flavours of the same code exist. I decode **both**, because they differ in
exactly the way that matters (the host flavour touches the mailbox, the device
flavour does not).

### 2.1 Where it maps

`musb_custom_init1` (host DLL `0x633c`, DCD `0x5930`) does two
`mmap_device_memory(NULL, len, /*prot*/0xb00, /*flags*/1, phys)` calls and stashes
the pointers in a 36-byte (host) / 28-byte (DCD) context hung off `ctrl+0xb8`
(host) / `ctrl+0x974` (DCD):

| map | phys | len | covers |
|---|---|---|---|
| MUSB window | the `ioport=` value (**`0x4a0ab000`**, low nibble masked: `and r0,r0,#~0xf`) | **`0x41c`** | MUSB core bank `+0x000…+0x11F` **and** the OTG bank `+0x400…+0x41B` |
| Control/PRCM window | **`0x4a000000`** (hard-coded immediate `#0x4a000000`) | **`0xa000`** | SYSCTRL_GENERAL_CORE `0x4A002000` (→ `0x233C`) and CM_CORE/CM_L3INIT `0x4A008000`/`0x4A009000` (→ `0x9360`, `0x93E0`) |

`musb_init` additionally maps `0x400` bytes at the same `ioport` into a separate
slot for the MUSB core register bank. I2C is opened separately (`/dev/i2c0`,
`O_RDWR`) only when the PMIC path is enabled.

### 2.2 `musb_custom_reset(ctrl)` — the soft reset (identical in both flavours)

```
ctx = ctrl->custom
*(u32*)(regs + 0x404) |= 0x2                 ; OTG_SYSCONFIG.SOFTRESET = 1
usleep(1)
v = *(u32*)(regs + 0x408)                    ; OTG_SYSSTATUS
for (i = 0; i < 999; i++) {                  ; total budget ≈ 1 ms
    if (v & 1) return 0;                     ; RESETDONE
    usleep(1);
    v = *(u32*)(regs + 0x408);
}
slog("%s Reset Failed: SYSSTATUS_RESET_DONE still low");
return -1;
```
Addresses: `0x4A0AB404` (`OTG_SYSCONFIG`), `0x4A0AB408` (`OTG_SYSSTATUS`).
TRM Tables 23‑1194/23‑1196 confirm `[1] SOFTRESET (RW, reset 0)` and
`[0] RESETDONE (R, reset 1)` — note the driver does **not** use the MUSB core's own
`POWER.RESET`; the OCP soft-reset of the whole block is the only reset performed.

### 2.3 `musb_custom_init2(ctrl)` — the PHY / controller selection

DCD version (`devu-RIM_ecm-…`, `0x55dc`), in program order, with `flags = ctrl->[0]`:

```
1.  OTG_SYSCONFIG (0x4A0AB404) = 0x0000100D          ; flags.bit6 (smartclock) == 0
    or                        = 0x00002015           ; flags.bit6 == 1 (smartclock)
      0x100D: MIDLEMODE[13:12]=1 (no-standby), SIDLEMODE[4:3]=1 (no-idle),
              ENABLEWAKEUP[2]=1, SOFTRESET[1]=0, AUTOIDLE[0]=1
      0x2015: MIDLEMODE=2 (smart-standby), SIDLEMODE=2 (smart-idle), ENABLEWAKEUP=1, AUTOIDLE=1
      (TRM reset value is 0x2011, i.e. the default path deliberately disables
       smart-idle AND smart-standby and enables wakeup)

2.  OTG_INTERFSEL (0x4A0AB40C) &= ~0x7                ; PHYSEL = 0 => 8-bit UTMI+ level 3
                                                     ;   (reset is 0x1 = 12-pin ULPI)

3.  CM_L3INIT_USBPHY_CLKCTRL (0x4A0093E0):
      if (flags.bit7 /*inherit_cfg*/ && (byte@0x4A0093E0 & 1))  -> preserve, log
                                                                  "CM_L3INIT_USBPHY_CLKCTRL = 0x%x ... preserve"
      else                                                      -> writel(0x00000101)
         0x101 = MODULEMODE[1:0]=1 (ENABLED) | OPTFCLKEN_PHY_48M[8]=1

4.  if (flags.bit4 /* cfg_pmic, cleared by no_cfg_pmic */) {   <-- NOT taken on this product
        fd = open("/dev/i2c0", O_RDWR)
        twl_read (0x49, 0x00) -> log "usb_vendor_id_lsb = 0x%x"
        twl_read (0x49, 0x01) -> log "usb_vendor_id_msb = 0x%x"
        twl_write(0x48, 0xA2, 0x21)
        v = twl_read(0x48, 0xE5);  twl_write(0x48, 0xE5, v | 0x10)
        twl_write(0x49, 0x04, 0x05)
    }

5.  if (flags.bit5 /* session_poll, cleared by no_session_poll */)      <-- NOT taken
        pthread_create(session_polling_thread, prio 10, detached)
```

Host DLL version (`devu-omap4430-musb.so`, `0x5f18` + `0x633c`) — same steps 1–3 but
with a **different flag numbering** (its `getsubopt` table is
`{verbose, forcefs, nodma, prio, smartclock, inherit_cfg, no_cfg_pmic, dma_threshold,
dmamode_mask, nosg}`, so `inherit_cfg = bit2`, `cfg_pmic = bit3`), and it **also**
writes the mailbox, once, unconditionally, in `musb_custom_init1` after `delay(10)`:

```
*(u32*)(cm + 0x233C) = 5      ; CONTROL_USBOTGHS_CONTROL = AVALID|VBUSVALID, IDDIG=0  -> HOST
```

That single store is the whole reason the host flavour must not be used for a
device-mode bring-up.

### 2.4 The PMIC programming, in full (and why it is off)

`twl6030_i2c_write(ctrl, slave7, reg, val)` / `twl6030_i2c_read(ctrl, slave7, reg, &val)`
are thin `devctlv()` wrappers on the `/dev/i2c0` fd (`DCMD_I2C_SEND = 0x08010505`,
`DCMD_I2C_RECV = 0xC0140507`; iov = {`i2c_send_t`/`i2c_recv_t` blob, register byte,
data byte}). Slaves used: **`0x48`** and **`0x49`** — the TWL6030's `SLAVE_A`
(main PMIC) and `SLAVE_B` (USB transceiver) pages. The `0x49/0x00`+`0x01` reads
returning "usb_vendor_id_lsb/msb" is unambiguous proof that `0x49` is the USB page.

The **host** flavour's `musb_custom_init1` (`0x654c`–`0x6670`) performs a
read-save/write pair per register — the saves are restored verbatim in
`musb_custom_fini2` (`0x60a0`–`0x613c`), so the driver is a good citizen:

| # | action | register | value | mainline equivalent |
|---|---|---|---|---|
| 1 | read  | `0x49/0x00` | → saved | `USB_VENDOR_ID_LSB` |
| 2 | read  | `0x49/0x01` | → saved | `USB_VENDOR_ID_MSB` |
| 3 | R/W   | `0x48/0xF5` | **`0x01`** | `TWL6030_CFG_LDO_PD2 = 0x01` — "set VUSB bit" |
| 4 | R/W   | `0x48/0xE5` | **`0x10`** | `TWL6030_MISC2 = 0x10` — "set VUSB_IN_VBAT" |
| 5 | R/W   | `0x48/0xA3` | **`0x18`** | *(no mainline analogue — see unknowns)* |
| 6 | R/W   | `0x48/0xA1` | **`0x3F`** | *(no mainline analogue)* |
| 7 | R/W   | `0x48/0xA2` | **`0x21`** | *(no mainline analogue)* |
| 8 | W     | `0x49/0x04` | **`0x04`** | `USB_VBUS_CTRL_SET = 0x04` (VBUS_ACT_COMP) |
| 9 | W     | `0x49/0x06` | **`0x05`** | `USB_ID_CTRL_SET` (mainline uses `0x14`: adds GND-drive) |
| 10 | `delay(500)` | — | 500 ms | — |
| 11 | R/W  | `0x49/0xE8` | **`0x40`** | *(no mainline analogue)* |

The **DCD** flavour only does a smaller subset, inlined in `musb_custom_init2`
(`0x5678`–`0x57f8`, the slog prefix string for this block is literally
`"pmic_configure"`): `read 0x49/0x00`, `read 0x49/0x01`, `write 0x48/0xA2 = 0x21`,
`read 0x48/0xE5 → |0x10 → write`, `write 0x49/0x04 = 0x05` (note **`0x05`**, not the
host's `0x04`), then `close(fd)`.

**But none of this runs in the shipped product.** `usbmgr` passes
`no_cfg_pmic,no_session_poll,inherit_cfg`, and `no_cfg_pmic` clears `flags.bit4`
(DCD) / `bit3` (host), which skips the entire block. The consequence is important
and is the answer to 110's open question: **TabletOS does not program the TWL6030
USB block from the USB driver at all on this unit.** `VUSB` is armed much earlier by
the IFS (`set_trans vusb sleep=ams act=ams`, report 110 §1) and the TWL USB
comparators are left at their reset state; the only thing that matters is that the
comparator *outputs* reach the SoC, and on OMAP4 they reach it as the
`CONTROL_USBOTGHS_CONTROL` mailbox, which is software-written (§4.1).

### 2.5 `musb_init` — the full bring-up order (DCD flavour)

```
calloc(1, 0x998) for the controller                    0x2f9c
musb_cfg_get(hw+0x968)   -> {fifo_bytes=0x4000, max_ep=16, 125}       0x2fe0
ctrl fields: [0x10]=0x10000, [0x14]=0x10000, [0x18]=1, [0xc]=0x1c     0x2fe4..0x301c
musb_process_args(ctrl, opts)                          0x3054  (getsubopt table @ .data 0x9214)
musb_custom_init1(ctrl, cfgstr)                        0x3090  (§2.1 / §2.4)
mmap(ioport, 0x400) -> core regs                       0x312c
mutex init; musb_dma_map                               0x3184
call ctrl->fn_ep0_init (function-driver hook)          0x31c8
fifomem_init(hw->fifo_bytes = 0x4000)                  0x3200  (§2.7)
musb_custom_reset(ctrl)                                0x3234  (§2.2)
if (flags & FORCEFS)  POWER (regs+0x01) &= ~0x20       0x3240..0x3254   (HSENAB <- 0)
INTRUSBE (regs+0x0B) = 0x27                           0x3258..0x3260
     0x27 = SUSPEND(0) | RESUME(1) | RESET(2) | DISCONNECT(5)
musb_custom_init2(ctrl)                                0x3268  (§2.3)
if (flags & LINKUP) POWER |= 0x40  else POWER &= ~0x40 0x32d0..0x32fc   (SOFTCONN)
```

`FORCEFS` is **not** passed by usbmgr, so `POWER.HSENAB` stays as-is and the device
does negotiate high speed. `LINKUP` is **not** passed either (the DCD's own usage text
says `"(default)"` next to `linkdown`), so `POWER.SOFTCONN = 0` after init — the
pull-up is off until `ulink_ctrl -l1` (§4.2). That is the single most
Linux-relevant ordering fact in this report after the mailbox itself.

### 2.6 `FADDR` — `musb_set_device_address(ctrl, addr)`

```
slog("%s: %d", addr)
hw->[0x3c] = 0
if (hw->[0x20] & 1) {
    *(u16*)(regs + 0x102) = 0x0048;      ; EP0 CSR0 <- SETUP|SVDSETUPEND (ack the setup stage)
    hw->[0x20] &= ~1;
}
usleep(1000);                            ; 1 ms
*(u8*)(regs + 0x00) = addr;              ; MUSB_FADDR
```
Only the **low 7 bits** matter (`addr` comes from the SET_ADDRESS wValue). Standard
MUSB practice; the 1 ms delay before `FADDR` is the one implementation detail worth
copying if a status-stage race ever shows up.

### 2.7 FIFO / endpoint configuration

`fifomem_init(total)` (`0x290c`) turns `total = 0x4000` (16 KB) into
`0x4000 >> 3 = 2048` free blocks of **8 bytes** in a doubly-linked list
(`calloc(1,16)` header + `calloc(n,16)` block array), with the whole run inserted as
one free extent. `fifomem_alloc`/`fifomem_free` carve/rejoin extents; `fifomem_free_ep0`
and `fifomem_alloc_ep0` are stubs. So the driver manages MUSB's FIFO RAM as a
first-fit 8-byte-granule allocator and *derives* the hardware addresses from it.

`musb_endpoint_enable(ctrl, ep)` (`0x3734`) per endpoint:

```
maxp = ep->wMaxPacketSize (halfword at ep+4)
INDEX   (regs+0x0E) = epnum
size    = maxp >> 3
sz_code = log2(size)                    ; 1 -> 0; then shift right until 1, counting
if (ep is IN)  TXFIFOSZ(regs+0x62) = sz_code ; TXFIFOADD(regs+0x64) = fifo_off/8
else           RXFIFOSZ(regs+0x63) = sz_code ; RXFIFOADD(regs+0x66) = fifo_off/8
TXMAXP / RXMAXP (halfword) = maxp       ; IN: regs+0x100+16*n  ; OUT: regs+0x104+16*n
                                          ;  (flat, non-indexed endpoint window)
if (hw->flags & VDMA_ENABLE && ep->type == BULK)  dma_channel_claim(...)
```
No double-buffering is requested (the `sz_code` written is the plain power of two, not
`|0x10` for double-buffer), and no `CONFIGDATA` register is written. `nodma`/PIO is a
real option the product does not set, but the DMA engine used is the OMAP **CPPI**
(`dma_*` functions, `devu-omap4430-mg.so`'s sibling code) rather than MUSB's internal
DMA — irrelevant for a Linux port, which will use PIO or the same CPPI via
`musb`'s DMA glue.

### 2.8 `MUSB_DEVCTL` — never written

A whole-`.text` scan of the ECM DCD for stores to `regs + 0x60` (byte or word) finds
**none**; the only references to `+0x60` are to non-MMIO struct fields and to
`cm + 0x9360`. So all `DEVCTL` contents come from the OMAP4 OTG state machine, which
is fed by `CONTROL_USBOTGHS_CONTROL` (bits `AVALID/VBUSVALID/SESSEND/IDDIG`) plus the
PHY's internal comparators.

This is a genuinely useful negative: it means the driver trusts the mailbox
completely. It also explains report 112's measurement exactly — `DEVCTL = 0x80`
(`BSESSVLD=1`, `SESSEND=0`, `IDDIG=0`) is *precisely* what
`CONTROL_USBOTGHS_CONTROL = 0x15` produces, and nothing in the driver could have
changed that.

Observed/expected transition table (TRM Table 18‑194 bit semantics + the driver's
`POWER`/state machine):

| mailbox `0x4A00233C` | PHY `DEVCTL` | meaning | who writes it |
|---|---|---|---|
| `0x00` | `?` (IDDIG=0, SESSEND=0) | transient at usbmgr init | `platform_hw_init` (0x8f84) |
| `0x08` | `0x00..` `SESSEND=1` | session ended / unit unplugged | usbmgr teardown (0x8898); poll thread on VBUS low |
| **`0x15`** | **`0x80`** | **B-device, VBUS valid** | usbmgr (0x8864); poll thread on VBUS high |
| `0x05` | `0x40..` `IDDIG=0` | A-device/host | host DLL `musb_custom_init1` (0x6688) |

`BVALID` (bit 1) is never set anywhere — consistent with TRM "signal is currently
unconnected (reserved for future use)".

---

## 3. Descriptors

Device descriptors are 18-byte literals in `.data`, at `0x9154` in every
`-musbmhdrc` DCD (vaddr; `0x8154` in the file) except `devu-RIM_usbumass-…`
(`0x6470`).

| function DLL | `idVendor` | `idProduct` | class/sub/proto | `bcdDevice` | product string |
|---|---|---|---|---|---|
| `devu-RIM_rndis-omap4430-musbmhdrc.so` | `0x0FCA` | **`0x8010`** | `00/00/00` | `0x0001` | `RIM Network Device` |
| `devu-RIM_ecm-omap4430-musbmhdrc.so` | `0x0FCA` | **`0x8011`** | `02/06/00` (CDC ECM) | `0x0001` | `RIM Network Device` |
| `devu-RIM_rndis_mux-…` | `0x0FCA` | `0x8012` | `00/00/00` | `0x0001` | `BlackBerry` |
| `devu-RIM_ecm_mux-…` | `0x0FCA` | `0x8013` | `02/06/00` | `0x0001` | `BlackBerry` |
| `devu-RIM_usbumass-…` | `0x0FCA` | `0x8020` | `00/00/00` | `0x0200` | (mass storage) |
| `devu-RIM_aap-…` | `0x18D1` | `0x2D00` | `FF/FF/FF` | `0x0001` | `Android Accessory Protocol` |

So your `0x0fca/0x8010` expectation is right **for RNDIS**; the ECM build is
**`0x8011`**. (Google's `0x18d1:0x2d00` is the standard AOA VID/PID — the PlayBook
implements Android Open Accessory, consistent with `usb_device_aap.sh` running
`sbin/devu-aap`.)

Strings (all `-musbmhdrc` DCDs, string index 0 = LANGID `0x0409`):
`iManufacturer`(1) = **"Research In Motion, Ltd."**, `iProduct`(2) = per the table
above, `iSerialNumber`(3) = `ser=` override at run time, static default
**"23/06/2010"**; ECM also carries string 4 = **"000200000002"** (`iMACAddress`).

### 3.1 ECM (`devu-RIM_ecm-…`), full config descriptor, `wTotalLength = 0x50` (80)

Full-speed variant at `0x9174`:

```
09 02 50 00 02 01 00 80 fa      config: 2 ifaces, cfg 1, 0x80, 500 mA (0xfa*2)
09 04 00 00 01 02 06 00 00      iface0 alt0: 1 EP, CDC/ECM
05 24 00 10 01                  CDC header, bcdCDC 0x0110
05 24 06 00 01                  CDC union: control 0, subordinate 1
0d 24 0f 04 00 00 00 00 ea 05 00 00 00
                                CDC Ethernet networking: iMAC=4, stats=0,
                                wMaxSegmentSize=0x05ea (1514), 0 mcast, 0 power filters
07 05 81 03 10 00 01            EP1 IN, interrupt, 16 B, interval 1
09 04 01 00 00 0a 00 00 00      iface1 alt0: 0 EP, CDC data
09 04 01 01 02 0a 00 00 00      iface1 alt1: 2 EP, CDC data
07 05 82 02 40 00 00            EP2 IN, bulk, 64 B
07 05 03 02 40 00 00            EP3 OUT, bulk, 64 B
```
High-speed variant at `0x91c4`: identical except the two bulk `wMaxPacketSize` = `0x0200` (512).
Device qualifier at `0x9168`: `0a 06 00 02 02 06 00 40 01 00`.

### 3.2 RNDIS (`devu-RIM_rndis-…`), `wTotalLength = 0x46` (70)

```
09 02 46 00 02 01 00 80 fa      config: 2 ifaces, 500 mA
08 0b 00 02 02 02 ff 00         Interface Association: ifaces 0..1, CDC/0x02/0xff
09 04 00 00 01 02 02 ff 00      iface0: 1 EP, class 02/02/ff
05 24 00 01 10                  CDC header bcdCDC 0x0110
05 24 01 00 00                  CDC call mgmt, caps 0
04 24 02 00                     CDC ACM, caps 0                (RNDIS' dummy ACM)
07 05 81 03 08 00 01            EP1 IN, interrupt, 8 B, interval 1
09 04 01 00 02 0a 00 00 00      iface1 alt0: 2 EP, CDC data
07 05 82 02 40 00 00            EP2 IN, bulk, 64 B (HS: 0x0200)
07 05 03 02 40 00 00            EP3 OUT, bulk, 64 B (HS: 0x0200)
```

### 3.3 Serial number and MACs

* `usb_device.sh`: `USB_DCD_CHARGE_POLL=2000000`, `USB_DEVICE_SERIAL_NUMBER="123456"`
  (only a fallback); the real value comes from `USB_SERIAL_NUMBER`, which usbmgr sets
  from its config/NV key **`serialnum`**, default **`"0123-4567-89ab-cdef"`**, and
  passes as `ser=`. The DCD's `ser=` handler (`musb_process_args` `0x2d90`) builds a
  proper UTF‑16LE string descriptor of up to 126 bytes at run time.
* MACs: `usbmgr` holds defaults **`00c00f000001`** / **`00c00f000002`** and NV keys
  `USBNetMAC1`/`USBNetMAC2`/`GUID`; `usb_device_ecm.sh` defaults are
  `LOCAL_MAC_ADDR="000200000001"`, `MAC_ADDR="000200000002"` (the latter matches the
  descriptor's `iMACAddress` string), MTU 1500, and
  `USBDNET_IP_ADDR`/`usbdnet_ipaddr`=`169.254.0.1` in developer mode
  (`usbdnet_ipaddr_default = 169.254.*.*`). Overridable by `USB_LOCAL_MAC`,
  `USB_MAC`, `USB_MTU_SIZE`.

---

## 4. `scripts/usbd/*.sh` + `RIM_usbmgr-Winchester` — the required ordering

### 4.1 usbmgr's baked-in defaults (exact strings in `.rodata` at `0x18ab8`…`0x18b40`)

```
/dev/i2c0
"%s : Failed to open %s"
"%s : failed to map OMAP4430_L4CFG_BASE"
"Winchester"
"omap4430-musb"                                  <- USB_HOST_DRVR
"0x4a0ab000"                                     <- USB_HOST_ADDR / USB_DEVICE_ADDR
"124"                                            <- USB_HOST_IRQ  / USB_DEVICE_IRQ
"no_cfg_pmic,inherit_cfg"                        <- USB_HOST_DRVROPTS
"omap4430-musbmhdrc"                             <- USB_DEVICE_DRVR
"no_cfg_pmic,no_session_poll,inherit_cfg"        <- USB_DEVICE_DRVROPTS
"serialnum" / "0123-4567-89ab-cdef"
"900" / "1"
"00c00f000001"
```

`irq = 124` is not an arbitrary QNX number: **124 = GIC SPI 92 + 32**, i.e. the
OMAP4430 MUSB "mc" interrupt that Linux's `omap4-l4.dtsi` declares as
`<GIC_SPI 92 …>` (Linux IRQ 124). The two stacks therefore use the same interrupt.

`platform_hw_init()` (`0x8e90`) does exactly three things:

```
open("/dev/i2c0", O_RDWR) -> global[0]
mmap_device_memory(NULL, 0xa000, 0xb00, 1, 0x4a000000) -> global[1]
*(u32*)(global[1] + 0x2300 + 0x3C) = 0        ; CONTROL_USBOTGHS_CONTROL = 0
```

and the two mailbox writers are whole functions:

```
0x8864 (usbport_connect):    *(u32*)(cm + 0x233C) = 0x15   ; AVALID|VBUSVALID|IDDIG
0x8898 (usbport_disconnect): *(u32*)(cm + 0x233C) = 0x08   ; SESSEND
```

`usbport_connect()` is called from the device-start path at `0x58ac`, i.e. **before**
the function module's `init` and before `usbmgr_usb_device_start()`. Ten milliseconds
later (`0x597c` `delay(10)`; `0x5984`–`0x59a4`) it runs

```
spawnp("ulink_ctrl", {"-l1", NULL})            ; prog = .rodata 0x17e18, arg = 0x17e14
```

and on failure logs `"%s() : Error enabling USB link (%d)"`. The stop path
(`0xccf4`) writes `0x08` first and the `usb_device_*.sh` scripts call
`ulink_ctrl -l0` from `usb_driver_stop`.

So the object model is: **usbmgr owns the SoC-side mailbox and the pull-up; the DCD
owns the controller.** With `no_cfg_pmic` + `no_session_poll` the DCD does neither.

Inputs to that decision come from PPS, not from MMIO: usbmgr listens on
**`/pps/services/power/usbconnector`** and requires the triple
`UsbConnector` / `IdPin` / `Vbus` with values `None|TypeA|TypeBConnectedCharger|
TypeBConnectedHost`, `High|Float`, `High|Float`, and it publishes
`/pps/services/usb/info.usbmgr`, `/pps/services/usb/info.device_state`,
`/pps/services/usbd/settings`, and reads `/pps/system/development/devmode`.

### 4.2 `ulink_ctrl`

```
%C USB DCD link control sample Application
  -s name   Name of the USB manager to query (eg. "/dev/io-usb-dcd/io-usb")
  -l[0|1]   0 == set USB link state to disconnected
            1 == set USB link state to connected
  -w num    Wait num seconds for USBDC stack (default 60)
Example: Allow USB host to see USBDC connection:   ulink_ctrl -l1 &
```
It devctls `/dev/io-usb-dcd/io-usb`; the DCD turns that into
`musb_set_bus_state(ctrl, 2)` → `POWER |= 0x40` (`SOFTCONN`), and `state 1` →
`POWER &= ~0x40`. `ulink_ctrl -l0` is only ever called in stop paths.

### 4.3 The TWL6030 VBUS status bit (the alternate path)

`musb_custom_init2`'s optional thread (`0x5428`, priority 10) is:

```
for (;;) {
    usleep(100000);                        ; 100 ms
    twl6030_i2c_read(ctrl, 0x49, 0xE3, &v);
    v &= 0x04;                             ; bit 2 of USB-page reg 0xE3
    if (v == last) continue;
    last = v;
    if (v) { slog("%s VBUS HIGH"); *(u32*)(cm+0x233C) = 0x15; }
    else   { slog("%s VBUS LOW");  *(u32*)(cm+0x233C) = 0x08; }
}
```
i.e. **`TWL6030 USB-page (slave 0x49) register 0xE3, bit 2` is a VBUS-present
status flag**, and it is the *only* PMIC datum the driver needs. This is the
self-contained path for a system without usbmgr; on this product it is disabled
(`no_session_poll`) and usbmgr's PPS-driven equivalent does the same two writes.
Register `0xE3` is past the end of mainline's documented USB map (which stops at
`0x1A`), so this is a genuinely new data point from the `/base` dump.

### 4.4 `winchester_phy_ctrl` — a PHY power gate you should know about

Not referenced by any script in the dump (it is started by `SERVICE_phyctrl` in
`startup.sh`'s run-level 0 list, as `phyctrl`), it maps `0x4a000000` for `0x9364`
bytes — i.e. it can reach exactly up to `0x4A009360` — and has two ~40-byte
functions:

```
usb_phy_off:  *(u32*)(cm+0x2300) = 1;      ; CONTROL_DEV_CONF.USBPHY_PD = 1
              *(u32*)(cm+0x9360) = 0;      ; CM_L3INIT_HSUSBOTG_CLKCTRL = DISABLED
usb_phy_on:   *(u32*)(cm+0x9360) = 1;      ; MODULEMODE = ENABLED
              *(u32*)(cm+0x2300) = 0;      ; PHY powered
```
with `-o` force on, `-f` force off, `-p` (default) **daemonize and let powerman
decide**. So TabletOS has a policy that can legitimately leave the USB PHY powered
down and the OTG module clocked off. The bench state in report 112
(`0x4A009360 = 0x00000101`, i.e. MODULEMODE=1 **and** `OPTFCLKEN_UTMI_P1_CLK` set)
is the *enabled* state and the extra `0x100` bit matches the DCD's own
`writel(0x101)` in §2.3 step 3 — consistent with "the OTG stack was up when the
probe ran".

### 4.5 Dead options (do not waste time on them)

`usb_device.sh` passes `chargerpoll=2000000,chargerthresh=1`. Neither
`io-usb-dcd` nor the DCD DLL contains those strings; the DCD's `getsubopt` table is
exactly `{verbose, ser, linkup, linkdown, forcefs, nodma, no_cfg_pmic,
no_session_poll, smartclock, inherit_cfg}` and unknown tokens are **skipped**
(`cmp r0,#9 ; addls pc,…` falls through to the loop-continue at `0x395c`), so the
options after them still apply. They are leftovers from an older driver that had a
BC‑1.1 charger-detect poll. `RIM_serial` and `usbrump` modes are named in
`usb_device.sh` but have no DLL in this dump.

---

## 5. Proven vs inferred

**Proven (reproducible from these binaries + TRM):**

1. The device stack is `io-usb-dcd` + `devu-RIM_<proto>-omap4430-musbmhdrc.so`, the
   latter containing the complete MUSB hardware layer (`musb_custom_*`,
   `fifomem_*`, `musb_init`) statically linked to the function driver (§1.1, §1.2).
2. The PHY is the OMAP internal UTMI+ PHY: `OTG_INTERFSEL &= ~7` in
   `musb_custom_init2`; no ULPI/TLL access exists anywhere in the device path (§2.3).
3. Every MMIO address and value in §2.2–§2.7, cross-checked against TRM Tables
   23‑1190/23‑1194/23‑1196/23‑1198/18‑174/18‑194. All are in the
   `0x4A0AB0xx` / `0x4A00 23xx` / `0x4A00 93xx` windows already known from report 110.
4. `CONTROL_USBOTGHS_CONTROL` is written by **usbmgr**, with `0`, `0x15` and `0x08`,
   at the sites quoted in §4.1 — and **not** by the DCD in the shipped configuration.
5. The TWL6030 register map used by the driver matches mainline's
   `phy-twl6030-usb.c` exactly for `0x00/0x01`, `0x04`, `0x05`, `0x06`, `0x07`,
   `0xE5` (`MISC2`), `0xF5` (`CFG_LDO_PD2`); the driver's `0xA1/0xA2/0xA3` and
   `0xE8` are *not* in mainline's list (§2.4).
6. The shipped configuration passes `no_cfg_pmic` and `no_session_poll`, so the
   driver's PMIC programming and its VBUS poll are **dead code on this product**
   (§2.4, §4.1, §4.3).
7. `POWER.SOFTCONN` is off at init and turned on by `ulink_ctrl -l1` 10 ms after the
   device stack starts (§2.5, §4.1, §4.2).
8. All six device descriptors / VID-PID pairs, the string descriptors, and both
   configuration descriptor sets (§3).
9. `irq=124` = GIC SPI 92 = the MUSB "mc" interrupt (§4.1).
10. `MUSB_DEVCTL` is never written by the driver (§2.8).

**Inferred (physically motivated, not proven here):**

1. `0x48/0xA1..0xA3 = 0x3F/0x21/0x18` and `0x49/0xE8 = 0x40` are LDO/charger-related
   TWL6030 registers: the three consecutive `0x48` offsets look like a
   `CFG_TRANS/CFG_STATE/CFG_VOLTAGE` triplet, but no TWL6030 register map covering
   `0xA1..0xA3`/`0xE8` exists in this workspace, so the *names* are inferred, the
   *values* are proven.
2. `0xE3 bit 2` being "VBUS present" is inferred from the two slog messages
   (`VBUS HIGH` / `VBUS LOW`) that bracket the write; the bit position and register
   are proven.
3. That the shipped unit enumerates at **high speed** (not `forcefs`), hence that the
   `MUSB_POWER.HSENAB` default is left alone.
4. That `winchester_phy_ctrl`'s `-p`/powerman policy does not hold the PHY down while
   the OTG stack is in use — from report 112's register dump, not from code.
5. The exact ordering *between* `usbport_connect()` and the function module's
   `init()` is read off the control flow at `0x58ac`–`0x5918`; that the module init
   is what fails without a link is inferred.

**Honest gaps:** the numeric identity of TWL `0x48/0xA1..0xA3`, `0x49/0xE3`,
`0x49/0xE8`; the USB `0x49/0x04 = 0x05` vs `0x04` difference between the two driver
flavours; what NV item backs `serialnum`; what `usbmgr`'s "900" default is
(most likely a suspend timeout in seconds).

---

## 6. Ranked list: what a Linux MUSB gadget port must copy, what it can skip

Observation channel is unchanged (DRAM ring at `0x88000000`, report 112 §4).

### 6.1 Must copy (in this order)

| # | step | register / call | value | why |
|---|---|---|---|---|
| 1 | PHY powered | `CONTROL_USB2PHY` = `0x4A002300` bit0 `USBPHY_PD` | **0** | TRM: "power down entire USB phy (data, common module and UTMI)". Reset is already 0, so this is a *check*, not a fix — but `winchester_phy_ctrl` can have set it to 1. |
| 2 | PHY 48 MHz clock | `CM_L3INIT_USBPHY_CLKCTRL` = `0x4A0093E0` | `MODULEMODE=1`, `[8] OPTFCLKEN_PHY_48M=1` → `0x101` | without the 48 M clock the UTMI PHY never leaves reset |
| 3 | OTG module clock | `CM_L3INIT_HSUSBOTG_CLKCTRL` = `0x4A009360` | `MODULEMODE=1` (+ `OPTFCLKEN_UTMI_P*` bits 8..10 as needed) → the DCD ends at `0x101` | module must be out of idle before any MUSB access |
| 4 | PHY interface select | `OTG_INTERFSEL` = `0x4A0AB40C` | **`& ~7`** (`PHYSEL = 0`, UTMI+ 8-bit) | **reset value is `0x1` = ULPI**; getting this wrong is a silent "controller talks to a PHY that is not there" |
| 5 | OCP soft reset | `OTG_SYSCONFIG` = `0x4A0AB404` `\|= 0x2`; poll `OTG_SYSSTATUS` = `0x4A0AB408` bit0 | RESETDONE within ~1 ms | the only reset QNX performs |
| 6 | Idle/standby policy | `OTG_SYSCONFIG` = `0x4A0AB404` | **`0x0000100D`** (or `0x2015` with smart clocking) | kills smart-idle/standby so the module cannot be idled mid-transfer; same lesson as the MMC2 stall in 106/107 |
| 7 | Global interrupts | `MUSB INTRUSBE` (byte `+0x0B`) | **`0x27`** = SUSPEND\|RESUME\|RESET\|DISCONNECT | what the DCD enables before handing over to EP0 |
| 8 | **Cable/session mailbox** | `CONTROL_USBOTGHS_CONTROL` = `0x4A00233C` | **`0x15`** = `AVALID\|VBUSVALID\|IDDIG`, and **`0x08` (`SESSEND`)** to disconnect | the single load-bearing write; without it `DEVCTL` never shows a session |
| 9 | Pull-up sequencing | `MUSB POWER` (byte `+0x01`) bit6 `SOFTCONN` | `0` at probe, **`1`** only when the gadget is ready | avoids the host seeing a half-configured device; matches `linkdown` default + `ulink_ctrl -l1` |
| 10 | FADDR | `MUSB FADDR` (byte `+0x00`) | `wValue & 0x7F` | write **≈1 ms after** the CSR0 setup ack |
| 11 | `idVendor/idProduct` | gadget descriptors | `0x0fca/0x8010` (RNDIS) or `0x0fca/0x8011` (ECM) | makes the host side unambiguous; product string `"RIM Network Device"`, manufacturer `"Research In Motion, Ltd."` |
| 12 | FIFO split | `TXFIFOSZ`/`RXFIFOSZ` (`+0x62`/`+0x63`), `TXFIFOADD`/`RXFIFOADD` (`+0x64`/`+0x66`), `TXMAXP`/`RXMAXP` | log2(mps>>3) and offset/8 over a 16 KB (0x4000) pool, 16 endpoints | the DCD's numbers; Linux `musb` does the same from `fifo_mode`/`ram_bits` |

### 6.2 Can skip (already handled by firmware, or dead in the product)

| item | why it can be skipped |
|---|---|
| TWL6030 `CFG_LDO_PD2 (0xF5)=1`, `MISC2 (0xE5)|=0x10`, `0xA1/0xA2/0xA3`, `VBUS_CTRL_SET (0x04)=0x04/0x05`, `ID_CTRL_SET (0x06)=0x05`, `0xE8=0x40`, `delay(500)` | the product runs with `no_cfg_pmic`; the driver's own PMIC block is disabled. `VUSB` is already in ACTIVE/AMS from the IFS (report 110 §1), and the bench shows `VBUSVALID` reaching the SoC. **Do not re-enable it unless the mailbox stops moving.** |
| TWL6030 VBUS polling (`0x49/0xE3` bit2, 100 ms) | the product runs with `no_session_poll`; usbmgr (or, on Linux, `twl6030-usb`'s IRQ path, or a manual write of `0x15`) supplies the same information |
| `OTG_SYSCONFIG = 0x2015` (smart-clock variant) | requires `smartclock`; the shipped value is `0x100D` |
| `POWER.HSENAB` handling | only the `forcefs` option clears it; not used |
| OMAP USBTLL `0x4A062000`, ULPI window `0x4A062800`, EHCI `0x4A064C00`, OHCI `0x4A064800` | never touched by the device path; keep `usbhstll`/`usbhshost` **disabled** in the board DTS (report 110 §4.2) |
| MUSB's internal DMA (`dma_*`, `fbma_*`) | the DCD uses PIO for control transfers and CPPI (`dma_*`) only for bulk when enabled; Linux has its own glue |
| `winchester_phy_ctrl`'s on/off policy | it is a powerman policy, not an init step — but note it *can* leave `0x4A009360 = 0` / `0x4A002300 = 1`, so check those two first if the block looks dead |
| everything in `devu-omap4430-musb.so` (host flavour) | it is the A-device driver and writes `0x233C = 0x05` |

### 6.3 Ranked next steps

| # | step | success signature |
|---|---|---|
| **N1** | Add `OTG_INTERFSEL & ~7` to the bring-up checklist and read it first on the next boot (`0x4A0AB40C`; expect `0` on a working unit, `1` on a cold one). Nothing in mainline's `omap2430.c` writes it — it relies on the bootloader, exactly as QNX does. | readback `PHYSEL = 0`; if it is `1`, write `0` **before** enabling the PHY clock |
| **N2** | If `DEVCTL` ever looks wrong, write `0x4A00233C = 0x15` from PID 1 and re-read `0x4A0AB060`; this is now *provably* what the product does, not a guess (report 112 = no-op because usbmgr/our firmware had already done it). | `DEVCTL` bit7 `BSESSVLD=1`, bit6 `SESSEND=0`, bit4 `IDDIG=0` |
| **N3** | Enforce the pull-up ordering: probe the gadget with `SOFTCONN=0`, set `POWER.SOFTCONN` only after `usb_gadget_udc_start()` has committed descriptors. Mainline's `musb_gadget` does this via `softconnect`; make sure it is not defeated by `dr_mode=peripheral` + a stale `MUSB_POWER` value from the bootloader. | host prints `new high-speed USB device number N` only once, after `UDC state = configured` in `/sys/class/udc/*/state` |
| **N4** | Use `0x0fca/0x8011` for an ECM gadget and `0x0fca/0x8010` for RNDIS, manufacturer/product strings per §3 — then a PlayBook-aware host tool will bind the same driver it does against TabletOS. | `lsusb -v` shows `0fca:8011`, `iManufacturer="Research In Motion, Ltd."` |
| **N5** | Add a "PHY power gate" trap to the bring-up script: read `0x4A002300` bit0 and `0x4A009360` MODULEMODE. If a prior TabletOS boot left them "off" (perfectly legal for `winchester_phy_ctrl -p`), force them on **before** `ti-sysc` probes. | `0x4A002300 & 1 == 0` and `0x4A009360 & 3 == 1` |
| **N6** | Only if enumeration still fails at "no session": reproduce the DCD's poll path in user space (read TWL `0x49/0xE3` bit2 over `/dev/i2c1` at `0x48/0x49` and mirror it into `0x4A00233C`) — that is byte-for-byte the vendor fallback, so any divergence becomes obvious. | PPS/ring shows VBUS transitions coincident with plug/unplug; `DEVCTL` follows |
| **N7** | Optional, cosmetic: fix report 110's note that the TWL6030 is at `0x48070000` — this dump re-confirms it (`/dev/i2c0`, and `usbmgr`/the DCD both open `/dev/i2c0`). Any custom VBUS code must use `&i2c1` in the DTS, not `&i2c2`. | `i2cdetect -y 1` shows `0x48`/`0x49` |

---

## 7. Appendix — exact artefacts used

**Binaries (all under `/home/psyden/SSDdrive/4869/berry-rcfs/`):**
`lib/dll/{devu-omap4430-musb.so, devu-RIM_ecm-omap4430-musbmhdrc.so,
devu-RIM_rndis-omap4430-musbmhdrc.so, devu-RIM_ecm_mux-…, devu-RIM_rndis_mux-…,
devu-RIM_aap-…, devu-RIM_usbumass-…, devu-omap4430-mg.so,
devu-ehci-omap4430-winchester.so, devh-usb.so, devnp-usbdnet.so,
devn-usbrndis-device.so}`;
`sbin/{io-usb, io-usb-dcd, RIM_usbmgr-Winchester, ulink_ctrl,
winchester_phy_ctrl, rimusb, usb, enum-usb, probe_ehci_udev_winchester}`.

**Text/config:** `scripts/startup.sh` (line 1542 `SERVICE_usbmgr`, line 1684 run-level 0
list), `scripts/usb_device.sh`, `scripts/usbd/{usb_device_ecm,usb_device_rndis,
usb_device_ecm_mux,usb_device_rndis_mux,usb_device_aap,usb_device_msc,usb_nconfig}.sh`,
`board.tdf`, `svnrev` (`Build ID: 79475`),
`seed/rootfs/var/usb/usbd/system/config/usbmgr.conf` (empty), `etc/enum-usb.conf`.

**Key offsets quoted above (vaddr; file offset == vaddr except `.data` in the DCDs,
which is `+0x800`):**

* `devu-omap4430-musb.so`: `musb_controller_init` `0x3624`; `musb_custom_init1`
  `0x633c` (mmap at `0x63f0`/`0x6440`, i2c block `0x64a4`–`0x6670`, mailbox store
  `0x6688`, CLKCTRL `0x6698`, `CONTROL_DEV_CONF` `0x66f0`); `musb_custom_reset`
  `0x5e7c`; `musb_custom_init2` `0x5f18`; `twl6030_i2c_write` `0x5fb4`;
  `musb_custom_fini2` `0x6080`; `twl6030_i2c_read` `0x6234`; `musb_custom_fini1`
  `0x630c`; option token table `.data` `0xa138`–`0xa160`; strings `0x8844`–`0x8b70`.
* DCD `devu-RIM_ecm-…`: `musb_init` `0x2f84` (POWER/INTRUSBE/SOFTCONN at
  `0x3240`–`0x32fc`); `musb_process_args` `0x2cb8`; option token table `.data`
  `0x9214`–`0x923c`; `fifomem_init` `0x290c`; `musb_endpoint_enable` `0x3734`;
  `musb_set_bus_state` `0x3e64`; `musb_set_device_address` `0x4460`;
  `musb_custom_reset` `0x5144`; `musb_custom_init1` `0x5930`;
  `pmic_unconfigure` `0x51e0`; `musb_custom_fini2` `0x5200`;
  `twl6030_i2c_read` `0x5350`; session poll thread `0x5428`; `twl6030_i2c_write`
  `0x5510`; `musb_custom_init2` `0x55dc` (SYSCONFIG `0x55f4`, INTERFSEL `0x561c`,
  USBPHY CLKCTRL `0x5634`, `pmic_configure` body `0x5678`–`0x5824`); device
  descriptor `.data` `0x9154`; config descriptors `0x9174`/`0x91c4`; strings in
  `.rodata` `0x6644`–`0x66c8`; slog format pool `0x6f38`–`0x7288`.
* `RIM_usbmgr-Winchester`: `platform_hw_init` `0x8e90` (i2c+mmap+`0x233C=0` at
  `0x8f78`); `usbport_connect` `0x8864`; `usbport_disconnect` `0x8898`;
  device-start path `0x58ac`…`0x59a4` (mailbox `0x58ac`, `setenv` `0x5918`,
  `delay(10)` `0x597c`, `spawnp("ulink_ctrl","-l1")` `0x5994`); stop path `0xccf4`;
  `run()`/`spawnp` helper `0xbedc`; defaults string block `.rodata`
  `0x18a60`–`0x18b40`; `ulink_ctrl`/`-l1` literals `0x17e14`/`0x17e18`.
* `winchester_phy_ctrl`: `usb_phy_off` `0x1b30` (`0x2300`=1 then `0x9360`=0),
  `usb_phy_on` `0x1b84` (`0x9360`=1 then `0x2300`=0), map `0x4a000000` len
  `0x9364` at `0x1c44`.

**TRM `swpu231ap.pdf.txt`:** 373985‑374010 (HSUSBOTG/OCP2SCP/USBPHY instance map,
`OTG_REVISION/SYSCONFIG/SYSSTATUS/INTERFSEL`), 373359‑373375 (`MIDLEMODE` values),
373425‑373440 (`SIDLEMODE` values), 267739‑267790 (`CONTROL_DEV_CONF` /
`USBPHY_PD`), 268380‑268445 (`CONTROL_USBOTGHS_CONTROL` bit map + reset `0x18`).

**Kernel (cross-checks only):** `drivers/usb/phy/phy-twl6030-usb.c`
(`USB_VENDOR_ID_LSB/MSB 0x00/0x01`, `USB_VBUS_CTRL_SET/CLR 0x04/0x05`,
`USB_ID_CTRL_SET/CLR 0x06/0x07`, `TWL6030_MISC2 0xE5`, `TWL6030_CFG_LDO_PD2 0xF5`,
`TWL6030_BACKUP_REG 0xFA`, `STS_HW_CONDITIONS 0x21`, `VUSB_CFG_TRANS/STATE/VOLTAGE
0x71/0x72/0x73`, and `twl6030_usb_ldo_init()`'s `0xF5=0x01`, `0xE5=0x10`,
`0x04=0x04`, `0x06=0x14`);
`drivers/regulator/twl6030-regulator.c` (`TWL6030_FIXED_LDO(VUSB, 0x70, 3300, 0)`);
`drivers/phy/ti/phy-omap-control.c`; `arch/arm/boot/dts/ti/omap/omap4-l4.dtsi`.

**Prior reports consulted:** 110 (`110-re-usb-2026-10-27.md`), 112
(`112-bench-r3-usb-m3-2026-10-27.md`). No other file was modified.

---

**Status: INCOMPLETE / UNAUDITED.** The subagent hit its turn limit twice while
cross-checking `lib/dll/devu-omap4430-musb.so` against the TRM.  It did write a
substantial report (~48 KB) before stopping, but the tail ends mid-work, so
treat the conclusions as unverified.  It is *not* a blocker: report 114 got the
Linux gadget enumerating (`0fca:8010`, CDC-ACM) without it.  Re-run or audit it
only if the PHY details (ULPI vs UTMI sequencing, `MUSB_DEVCTL` transitions,
TabletOS's descriptor strings) become necessary.
