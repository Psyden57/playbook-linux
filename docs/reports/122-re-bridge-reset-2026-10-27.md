# 122 — RE: hunting the TC358764 RESETN / power-enable in the QNX boot set

**Goal.** On the PlayBook (OMAP4430 "winchester", DSI → TC358764 → Hitachi panel) the
bridge never answers on our path: after a full register reset SYS_ID (0x0580) reads
0x00000000 and a VP_CTRL write does not read back. The reference OS (QNX 6.6) works but
never programs the bridge at display time (111 §4.3, 117), so the bridge must be released
either by the RIM bootloader or by a pull-up/POR. This report statically sweeps the QNX
**boot set** (`dumped4869ifs/proc/boot/*`, plus the RCFS at `/home/psyden/SSDdrive/4869/berry-rcfs/`)
for an early GPIO assertion, chip-select, reset or power-enable that could be the
TC358764's RESETN or its supply enable — and reports, for each place looked at, exactly
what was there. Scope is strictly "is there a pin / transient write"; the bridge's DSI
programming and the panel are out of scope.

---

## Conclusion

**There is no reset/power pin for the bridge anywhere in `setup-core-inactive`, the PMIC
tools, the SPI/I²C/MMC drivers, the TZ loader, the watchdog kicker, `resource_seed` or the
RCFS. There are exactly five GPIO nets in the whole boot set that could be a display
reset/enable, and they all live in the two display-path utilities that `splash_script`
runs. The single best RESETN/power-enable candidate is `GPIO2_24` (global GPIO 56),
driven as a 20 ms active-low pulse by `winch_lcdctl`.**

Ranked candidates (full detail in §7):

| # | net (global GPIO#) | bank.bit | padconf | mux | what the reference does | where | conf. |
|---|---|---|---|---|---|---|---|
| **1** | **GPIO 56** | **GPIO2 bit 24** (`0x0100_0000`) | **`0x080`** (gpmc_nadv_ale) | **mode 3** (reset 0x0) | **CLEARDATAOUT → `usleep(20000)` → SETDATAOUT** — a **20 ms low pulse**, taken on *this* unit's branch | `0x192d4` / `0x19338` | **proven (code + live state)** |
| 2 | GPIO 102 | GPIO4 bit 6 (`0x40`) | `0x090` (gpmc_ncs5) | mode 3 (reset 0x7) | same 20 ms low pulse, **other** branch of the same `if`; **not taken here** (reference leaves it driven *low*) | `0x19208` / `0x1926c` | proven branch not taken |
| 3 | GPIO 45 | GPIO2 bit 13 (`0x2000`) | `0x068` (gpmc_a21) | mode 3 (reset 0x7) | **single SET** to 1 at the very start of display init (no pulse); driven low at `power_off` | `0x190c8` (SET) / `0x1b394` (CLR) | proven (already in 117) |
| 4 | GPIO 13 | GPIO1 bit 13 (`0x2000`) | `0x1B0` (dpm_emu2) | mode 3 (reset 0x7) | H → 15 → **L** → 15 → **H** reset pulse, but for an **I²C device at slave 0x36**, not display | `0x1ca8`/`0x1ce4`/`0x1cf8` in `winchester-bootreason` | proven |
| 5 | GPIO 171 / GPIO 36 | GPIO6 bit 11 / GPIO2 bit 4 | `0x17C` (kpd_col3) / `0x058` (gpmc_ad12) | mode 3 (reset 0x0) | **OMAP-side inputs** wired to the TC358764's *own* GPIO pins; polled by the `-t/-T` bridge test | `0x1986c…0x19878`, `0x19600`/`0x196a8` | proven |

**Confidence: high** for the negative sweep (§1–§6) and for the code facts in the table —
every claim below is an instruction-level read of the shipped binaries with the PIC anchor
resolved. **Medium** for "GPIO2_24 is the TC358764 RESETN specifically": the boot set proves
it is *the* display-path reset pulse and that it is the only such pulse, but no string or
symbol names the net, so the assignment to a particular ball still needs the schematic.

Two corollaries that matter more than the pin itself:

* **The QNX boot set never writes a pad mux.** `CTRL_CORE_PAD` (`0x4A100000`) appears as a
  constant in exactly one boot-set binary (`winch_lcdctl`, and only as a `mmap` base), and
  the only writes into that window land at `+0x618`, which is outside the
  `CONTROL_CORE_PAD0_x` register block. So `GPIO2_13 / 2_24 / 4_6 / 6_11 / 2_4` are *already*
  muxed to GPIO when QNX runs ⇒ **something before QNX does it: the RIM bootloader** (our
  chainloader does not). Writing `GPIO_DATAOUT` on a pad still muxed to `gpmc_*` changes
  nothing at the ball, which is a plausible explanation for "we match QNX's `DATAOUT`
  exactly and nothing happens". (The only alternative is that `winch_lcdctl`'s GPIO writes
  are inert on the reference too — in which case the bridge's reset is not a GPIO at all and
  the answer is a rail/POR. §7.8 item 1.)
* **A cheap hardware-free diagnostic exists** in the reference's own vocabulary: QNX's
  bridge self-test fails unless **GPIO36 and GPIO171 both read high**
  (`"GPIO status good (GPIO36 and GPIO171 high)"` @`0x27634`). In kernel terms: read
  `GPIO2_DATAIN` bit 4 and `GPIO6_DATAIN` bit 11 — both must be 1. If either is 0 the
  bridge is unpowered / held in reset, independent of anything DSI.

Two things this report *extends* beyond the earlier ones:
1. Report **117 §1.3 row 35** lists only `GPIO2 +0x194 ← 0x2000` at `0x190c8` as
   display-time GPIO traffic. The same function does **four more** GPIO writes plus a
   20 ms low pulse (end of §6, and §7.1). 117 was scoped to the DSS-register store table
   and legitimately stopped at the DSS accesses; the GPIO tail of the splash sequence was
   therefore never enumerated until now.
2. The reference *also actively drives lines low*. With the OMAP4 direction convention
   nailed down (`GPIO_OE` bit **1 = input**, i.e. bit **0 = output**; TRM
   `swpu231ap.pdf.txt:387692-387707`), the live dump says the reference drives
   `GPIO1` {0,4,15,18,28,31}, `GPIO2` {5,7,22,23}, `GPIO4` {0,1,6,7} **low** as well as the
   high set everybody had been matching. Full map in §7.2.

What I did **not** rule out — see §7.8.

---

## 0. Method, anchoring, and what counts as evidence

**Anchoring.** Every file in `proc/boot` is a QNX ARM `ELF32 ET_DYN`, stripped,
`/usr/lib/ldqnx.so.2`. For all of them (except `procnto-smp-instr`) the first `PT_LOAD` is
`off=0, vaddr=0`, so **text vaddr == file offset**; the RW `PT_LOAD` is normally
`off=X, vaddr=X+0x1000`. Addresses in this report are vaddrs of the on-disk image at base 0.

**Tooling.** `arm-linux-objdump -D -b binary -m arm --adjust-vma=0` over the raw first
`PT_LOAD` (the ELF has no `.text` section header, so plain `objdump -D` on the ELF only
dumps `QNX_info`). `rizin 0.8.2` for string inventory. `arm-linux-readelf --use-dynamic -r`
for GOT/PLT resolution.

**PIC anchors used.**
* `winch_lcdctl`: `r4 = 0x2B000` (`0x1902c ldr r4,[pc,#960]` pool@`0x193f0`=`0x11fcc`,
  `add r4,pc,r4` at `0x1902c` ⇒ `0x19034+0x11fcc`). `0x2B000` is `DT_PLTGOT` (`readelf -d`),
  and its slots carry `R_ARM_JUMP_SLOT`. **Data target = `0x2B000 + pool_word`**, exactly as
  in 111/117. Verified by resolving PLT stubs:
  `0x40f0 → GOT 0x2b028 = usleep`, `0x421c → GOT 0x2b08c = disp_mmap_device_memory`,
  `0x424c → GOT 0x2b09c = slogf`, `0x40a8 → GOT 0x2b010 = printf`.
* `winchester-bootreason`: same idiom, anchor `0x3A24` (`0x1c38 ldr r4,[pc,#296]`,
  `add r4,pc,r4`).
* `pmic-winchester-twl6030`: anchor `0x9000` (report 119's value, re-verified).

**GPIO base ⇄ register map used** (OMAP4430 TRM §25): `GPIO_OE = +0x134`,
`GPIO_DATAIN = +0x138`, `GPIO_DATAOUT = +0x13C`, `GPIO_CLEARDATAOUT = +0x190`,
`GPIO_SETDATAOUT = +0x194`.
**OE polarity** (TRM `swpu231ap.pdf.txt:387692-387707`, verbatim): *"When configured as an
output (the desired bit **reset** in the GPIOi.GPIO_OE register) … When configured as an
input (the desired bit is **set to 1**)"*. So **OE bit 0 = output, 1 = input** — the same
convention report 118 implicitly used (`GPIO3 OE=0xffffffff` ⇒ "no outputs").

**Two independent detection passes were run, and they agree.** (a) raw-byte scan of every
file for the little-endian literals `0x4A310000 / 0x48055000 / 0x48057000 / 0x48059000 /
0x4805B000 / 0x4805D000 / 0x4A100000` (literal-pool loads); (b) disassembly scan for
`movt rX,#0x4A31|0x4805|0x4A10` (the `movw/movt` idiom) and for the GPIO accessor shapes
`add rX,rY,#404` / `#400` / `#308` / `#312`. A file that passes both passes is claimed
GPIO-clean; the claim is falsifiable by re-running either pass.

---

## 1. `proc/boot/.script` and every `*script*` — order, and utilities before splash

### 1.1 `.script` format and exact order

`.script` is the QNX mkifs boot script, not text: `[u16 len][u16 type][payload…]`, with
`type 0x00` = a command record (`u32 pad, u32 pad, NUL-separated argv[0..]`) and
`type 0x03` = a display message. **Messages precede the command they describe** (proved by
`"starting Watchdog"` immediately preceding the `omap4430-wdtkick` record, and
`"SETUP-CORE-INACTIVE"` preceding `setup-core-inactive`).

Order of actors up to splash (all verified against the hexdump, `0x0`–`0x7a0`):

| # | record | note |
|---|---|---|
| — | `Welcome to QNX Neutrino Trunk on the TI OMAP4430 (ARMv7 Cortex-A9 core) - Winchester Board` | message only |
| 1 | `setup-core-inactive full` | earliest PRCM actor — §2 |
| 2 | `resource_seed dma=0,31` | DMA reservation, no MMIO |
| 3 | `devc-seromap -o devperm=0600 -e -F -b115200 -c48000000/16 0x48020000^2,106` | UART3 |
| 4 | `slogger -U 99:99 -s2048k`, `pipe -U 99:99` | |
| 5 | `trustzone-omap4 -U99:102` | §6 |
| 6 | `i2c-omap35xx-omap4 -p 0x48070000 -i 88 …` (i2c0), `…0x48072000 -i 89` (i2c1), `…0x48350000 -i 94` (i2c3) | §4 |
| 7 | `omap4430-wdtkick -t 15000 -P -U 99:99` | §6 |
| 8 | `pmic_twl6030_cfg` (no args) — message `"Configure power management chip"` | §3 |
| — | message `"Take touch pad out of reset"` @`0x480` — **orphan: no command record follows it** | see §1.3 |
| 9 | `pmic-winchester-twl6030 -U 99:99` (message `"Start PMIC driver..."`) | §3 |
| 10 | `sh /proc/boot/pmic-twl6030-ctl_script` (message `"Initialize regulators"`) | §3 |
| 11 | `random -p -l entropy.so:10:300000:256` | |
| 12 | `devb-mmcsd-winchester blk cache=10M,… cam quiet,cache qnx6 crypto=enable` | §4 |
| 13 | `spi-master -P 0600 -u 3 -U 99:99 -d omap4430 base=0x480ba100,irq=80,sdma=0`; `waitfor /dev/spi3` | §4 |
| 14 | **`sh /proc/boot/splash_script`** ; `waitfor /dev/hd3` | ← display happens here |
| 15 | `backlight_win -U 99:99` ; `waitfor /dev/hd6` | 111 §4.3: `/dev/backlight` only appears *here* |
| 16 | `rpmb-omap4`, `nvram_resource_manager`, `rfs_validator -c`, `sanityChecking …`, `ksh /base/scripts/startup.sh` | all *after* splash |

**Answer for §1:** no utility before `splash_script` can poke a GPIO except
`setup-core-inactive`, `pmic_twl6030_cfg` and `pmic-winchester-twl6030` — and of those, only
the last one touches a GPIO bank (§3). Everything else is a UART/slog/pipe/I²C/SPI/MMC/TZ
driver.

### 1.2 `splash_script` (385 B, plain text)

```
winchester-bootreason
rc=$?
if [ $rc != 60 ] && [ $rc != 55 ]; then
    winch_lcdctl -s splash &
else
    winch_lcdctl -b power_off &
fi
```

So the two binaries that run at display time are **`winchester-bootreason`** (which
*does* contain a GPIO1 reset pulse — §7 candidate 4) and **`winch_lcdctl`** (which contains
the display-side pulses — §7 candidates 1–3). Nothing else.

### 1.3 `pmic-twl6030-ctl_script` (259 B)

Dispatches on `uname -m`: `*_Winchester2_*` ⇒ `pmic-twl6030-ctl -c .../pmic-twl6030_winchester2.cfg`,
else `.../pmic-twl6030.cfg`. It runs at step 10, i.e. before splash, and it *could* in
principle set a regulator. Measured: `pmic-twl6030-ctl` (31 155 B) contains **no GPIO base
constant and no `+0x190/+0x194/+0x134` access**; it is a TWL6030 I²C CLI (report 108's
subject). Its only "extreg" behaviour is the `regen1`/`regen2` GPIO1 bits 2/15 already
documented in 119, and those only fire on an explicit `set_state` (the cfg files do not
contain one for a display rail — §3.3). **No display-rail action.**

### 1.4 `sanity.cfg` — text, not a script

A list of process/`/dev` expectations for `sanityChecking`. No GPIO content.
(`/base/board.tdf` in the RCFS is likewise data: `BOARD="winchester"`, `HDROOT="hd3"`, …)

---

## 2. `setup-core-inactive` — GPIO/padconf writes beyond report 106

**Result: none. Report 106's inventory is complete as far as GPIO is concerned.**

* Both detection passes: the *only* occurrences of `0x4A310000`/`0x4805_5/7/9/B/D_000`/
  `0x4A100000` in the file are three copies of the DSS **base-address table** in rodata
  (`0x22174`, `0x24438`, `0x24b28`, plus `0x22d84`, `0x231a4`, …) — the QNX `CRED` instance
  table also used by `winch_lcdctl` (111 §"complete QNX-verified OMAP4430 base map").
  There is **no `movw/movt` construction of any of them**.
* No `add rX,rY,#400` / `#404` / `#308` / `#312` anywhere in the file ⇒ no
  `GPIO_CLEARDATAOUT` / `GPIO_SETDATAOUT` / `GPIO_OE` / `GPIO_DATAIN` access at all
  (grep for `[rX, #(400|404|308|312)]` returns zero hits).
* The binary imports `mmap` but **not** `mmap_device_io`; its only MMIO is the PRM/CM/SCRM
  aperture listed exhaustively in report 106 §7 (`0x4A004xxx` CM_DIV_*, `0x4A008xxx`,
  `0x4A009xxx`, `0x4A306xxx`, `0x4A307B88…BAC` VC registers, `0x4A30A320` SCRM,
  `0x48243400/04`, `0x48243800`). **That table contains no GPIO and no
  `CONTROL_CORE_PAD` address.**
* The strings that *look* GPIO-ish are clock-domain identifiers, not pins:
  `prcm_gpio1 … prcm_gpio6`, `ATTILA_L4PER_TA_GPIO2…6`, `ATTILA_L4WKUP_TA_GPIO1,
  GPIO1`. These are `CM_L4PER_GPIO2_CLKCTRL`-class names. `"VLCD"` is a VC/voltage-domain
  token (report 106 §0), not a display line.

**Conclusion:** `setup-core-inactive` cannot assert or release any ball. It only turns
clocks/voltages on, which is relevant to *why* a GPIO write elsewhere might abort, but it is
not the missing reset.

---

## 3. `pmic-winchester-twl6030` and `pmic_twl6030_cfg` — banks beyond regen1/regen2

**Result: `pmic-winchester-twl6030` touches exactly one GPIO bank and exactly two bits; nothing
else. `pmic_twl6030_cfg` touches no GPIO at all.**

### 3.1 `pmic-winchester-twl6030` (38 250 B)

* Exactly **one** `movt #0x4A31` in the file: `0x5e60`, feeding
  `mmap_device_io(0x1000, 0x4A310000, 0, …)` at `0x5e58-0x5e68`, handle stored in
  `struct+4`. (Report 119 §PIC-2 — re-verified.)
* Exactly **one** GPIO-register pair in the file: `winchester_extreg_set_state` @`0x5c80`,
  `add r0,r0,#404` @`0x5cb8` (`SETDATAOUT`) and `add r0,r0,#400` @`0x5ccc`
  (`CLEARDATAOUT`), masks `0x00000004` (bit 2, regen1) and `0x00008000` (bit 15, regen2).
  No `GPIO_OE`, no `GPIO_DATAIN`, no other bit.
* The other two `#404` hits in the file (`0x492c`, `0x4ae0`) are **string arithmetic**
  (`r2 = msg_base + 0x194` as a `slogf` argument), not register access — they are followed
  by `bl 0x1248` (`slogf`), not by a store.
* No `movt #0x4805`, no `movt #0x4A10` ⇒ **no GPIO2/3/4/5/6 and no padconf write.**

### 3.2 `pmic_twl6030_cfg` (12 936 B, 4 028 B of text)

Imports only `printf / perror / puts / devctl / devctlv / open / close / strerror` —
**no `mmap`, no `mmap_device_io`**. Strings: `"twl6030_i2c_write fail"`,
`"twl6030_i2c_read fail"`, `"/dev/i2c0"`, `"open"`, `"TWL6030 ES1.0"`. It is a pure TWL6030
I²C register programmer. **It cannot drive a GPIO.**

### 3.3 Why this matters for the orphan `"Take touch pad out of reset"` message

The `.script` prints `"Take touch pad out of reset"` (`0x480`, len 0x24, type 3) and then
`"Start PMIC driver..."` with **no command record in between**. Since `pmic_twl6030_cfg` has
no MMIO at all, the utility that actually did the touch-pad reset is **not in this IFS** —
the message is a leftover from a build where a reset helper (in an older build, or a
different component) sat there. Practical consequence: *do not* go looking for a hidden
"reset helper" that "we failed to run" — it is not in the shipped boot set.

### 3.4 `pmic-twl6030-ctl` and its cfg files — nothing display-related

See §1.3. The RCFS `etc/system/config/` contains `winchester.pm`,
`winchester2.pm`, `winchester-nocore.pm` (power-management policy), not register lists with
a display rail.

---

## 4. `spi-master`, `spi-omap4430.so`, `i2c-omap35xx-omap4` — CS / reset / enable

**Result: no GPIO is used as a chip-select, reset or power enable. CS is the MCSPI hardware
CS; the I²C driver has no GPIO at all.**

| file | GPIO bank constant? | `+0x190/0x194/0x134/0x138`? | verdict |
|---|---|---|---|
| `spi-master` (17 335 B) | none | none | pure resource-manager front end |
| `spi-omap4430.so` (22 425 B) | **no GPIO bank.** One `movt #0x4805` at `0x2764` and `0x2f44`, both `movw #0x6000` ⇒ **`0x4805_6000`**, which the TRM (Table 2-? / `swpu231ap.pdf.txt:17865`, `:221377`) names **`PER_TA_GPIO2`** — the L4_PER *target-agent* alias for the GPIO2 port, **not** a GPIO register block. It is stored into a driver config field (`[r4,#104]` / `[r4,#0x68]`), never dereferenced as data. | none (`0x48056000` + `0x194` is never formed) | **no GPIO CS** — CS comes from `MCSPI_CHxCONF` (`[r4,#128]=2`, `[r4,#132]=0x41` at `0x2734-0x2740`) |
| `i2c-omap35xx-omap4` (34 204 B) | none (its `movt #0x4A00` hits are `CM_*_CLKCTRL` clock enables: `0x1f68/0x1f84/0x1fa0/0x1fbc`) | none | no GPIO |
| `devb-mmcsd-winchester` (85 009 B) | same `PER_TA_GPIO2` constant at `0x8274` (into `[r4,#0xa8]`) | none | no GPIO; power comes from the PMIC regulator API |

The MCSPI4 base in `.script` is `0x480ba100` (111 §1 corrected this: that is MCSPI4, not
MCSPI3). Nothing in the SPI stack resets or enables the panel or the bridge.

---

## 5. RCFS startup scripts (`/home/psyden/SSDdrive/4869/berry-rcfs/`)

**Result: nothing in the RCFS can run before the IFS, and nothing in it drives a display or
bridge GPIO.**

* The IFS `.script` is the *first* thing `procnto` executes; its **last** record is
  `ksh /base/scripts/startup.sh`. `scripts/startup.sh` (46 419 B) is therefore strictly
  **after** splash, and `SERVICE_bootreason` in it re-runs `winchester-bootreason -c -h -s`
  and then `winch_lcdctl -s splash` / `winch_lcdctl power_on` — i.e. it repeats the same two
  utilities, adding no new pin.
* `scripts/startup.sh:1025` is the only GPIO-flavoured line in the whole script tree:
  `winchester-lc -d led-gpio.so -d ls-cm3217.so`, and it is guarded by
  `*Rev:00*|*Rev:01*|*Rev:02*` — **EVT boards only**. Every other revision (including this
  DVT3+/MBID≥5 unit, cf. 119 §0.1) gets `winchester-lc -d led-fan5702.so -d ls-cm3217.so`,
  an **I²C** FAN5702 LED driver. So even the one `led-gpio` component is (a) post-splash and
  (b) not built for this board revision.
* `etc/system/config/display.conf` is a generic QNX graphics `device{}` list
  (`imx35`, `omap35xx`, `mpc5121`, …) — a stock template, not a Winchester register recipe.
* No `a GPIO` / `lcdctl` / `bridge` / `lvds` / `tc358` utility exists in `bin/` or `sbin/`
  beyond what is already in the IFS.
* `board.tdf`: `BOARD="winchester"`, `HDROOT="hd3"`, `HDIFS0="hd1"`, `OFFSET_IFS0="256"` —
  data only.

---

## 6. Everything else in `proc/boot` — strings/code mentioning display/bridge/LVDS/reset

I swept all 52 ELF files with (a) the two GPIO detection passes and (b) a
`strings | grep -iE 'lvds|tc358|dsi|lcd|display|panel|backlight|bridge'` filter with the
docs/TRM-identifier noise removed.

| file | finding |
|---|---|
| `winch_lcdctl` | **the only display/bridge actor** — §7. Contains `mt_tc358764`, `gpio_test`, `"Get DSI to LVDS Bridge GPIO status"`, `"Fail GPIO36 off"`, `"Fail GPIO171 off"`, `"GPIO status good (GPIO36 and GPIO171 high)"`, `"Invalid or no panel connected"`, `"CMI/TMD/LG panel detected"`. **Only GPIO bank accesses in the entire boot set besides `pmic-winchester`** (§7). |
| `winchester-bootreason` | second (and only other) GPIO-bank user: `mmap_device_io(0x1000, 0x4A310000)` at `0x1c40-0x1c50` and a reset pulse on GPIO1 bit 13 (§7 candidate 4). Its display relevance is nil — its strings are `PHOENIX_START_CONDITION`, `led_init`, `led_blink`, `wait_for_pmic_intr`, PPS charger objects. |
| `backlight_win` | LM3532 over I²C only (111 §4.2). No mmap, no GPIO. |
| `nvram_resource_manager` | has NV item *names* `NV_OSSTORE_LcdCal_NUM`, `NV_OSSTORE_LcdAutoDetect_NUM`, `NV_OSSTORE_LcdBacklightCaledVal_NUM`. **Data, not pins** — confirms the panel-ID/`LcdAutoDetect` result is persisted, which is why a cold read of GPIO176 picks the wrong SPI table (118 §6.2). No GPIO/padconf. |
| `trustzone-omap4` | `movt #0x4A00` hits only (CM/CORE clock + TZ RAM firewall). No GPIO, no padconf. |
| `omap4430-wdtkick` | I²C + WDT2 only (report 108). |
| `devc-seromap`, `resource_seed`, `slogger`, `pipe`, `random`, `rpmb-omap4`, `rfs_validator`, `sanity`, `sanityChecking`, `waitfor`, `uname`, `mount`, `shutdown`, `vfs-hooks-rim.so`, `cam-disk.so`, `io-blk.so`, `chkqnx6fs`, `fs-*.so`, `lib*.so*`, `ksh/less/more/ls/cat/login/pidin` | no GPIO bank constant, no `+0x190/+0x194` access, nothing display-related. (`shutdown`'s literal-pool `0x4A100000` at `0x5f22` is a coincidental data word — it has no `movt`, and its "display" strings are `io-display`/"display drivers" in a slay list.) |
| `setup-core-inactive` | §2 |

**Correction to report 117.** 117 §1.3 row 35 records the *only* display-time GPIO write as
"`GPIO2 +0x194 ← 0x2000` at `0x190c8`". That is the right *first* write, but the same
function contains **four more** GPIO writes further down (`0x19208`, `0x1926c`, `0x192d4`,
`0x19338`) plus a whole teardown block (`0x1b2dc … 0x1b394`). 117 was scoped to the
DSS-register store table and cut off at the DSS accesses; the GPIO side of the splash
sequence is longer than 117 implies. This is the substantive correction this report makes.

---

## 7. The candidate pins, their timing in the boot order, and what was NOT ruled out

### 7.1 The one function that does all display-time GPIO work

`winch_lcdctl`'s display-init sequencer is **`0x19020`** (`push {r4,r5,r6,lr}`,
`mov r5,r0` = config, `mov r6,r1` = "dual-DSI" flag, single caller `0x1cc78` inside the
DSP init wrapper that is entered with the *command* mode). It maps, in one block at
`0x1bdd4`–`0x1c034` (`disp_mmap_device_memory` @ PLT `0x421c`, size `0x10000` each):

| struct offset | PA (`movw/movt` at) | what |
|---|---|---|
| `+0xdc` | `0x48041000` (`0x1be6c`) | DISPC |
| `+0xe0` | `0x48042000` (`0x1bea4`) | DSI1 PHY |
| `+0xe4` | `0x48043000` (`0x1bedc`) | DSI1 PLLCTRL |
| `+0xe8` | `0x48045000` (`0x1bf14`) | DSI2 |
| **`+0x118`** | **`0x48055000`** (`0x1bf4c`) | **GPIO2** (QNX `CRED` index 70) |
| **`+0x128`** | **`0x48059000`** (`0x1bf84`) | **GPIO4** (index 74) |
| **`+0x138`** | **`0x4805D000`** (`0x1bfbc`) | **GPIO6** (index 78) |
| `+0x3D4` | `0x4A100000` (`0x1bff4`) | CTRL_CORE window (`mmap` base; only `+0x618` is ever written — outside the `CONTROL_CORE_PAD0_x` block ⇒ **not a pad mux**) |

(The `slogf` "not mapped %d function %s" diagnostics at `0x19090`, `0x19238`, `0x19954`
carry those `CRED` indices `70 / 74 / 78` as their `%d`, which is how the three banks are
told apart in the log stream.)

Exact write order in `0x19020` on the mode == 1 (splash) path. Note the *nested* condition:
the mode field (GOT slot `0x2b100`, target `0x23afc`) selects splash(1) / dual-DSI(2) / other,
and **mode 2 branches at `0x19138` to `0x1913c` (dual-DSI) and never executes the pulse block**; the pulse
therefore belongs specifically to the splash / power_on paths, which is exactly our case:

```
19068  cmp [mode],#1 ; bne  ->  splash branch
19070-190c8   GPIO2+0x194 <- 0x00002000   (SET  GPIO2_13)          ; "display enable"
190cc  0x1c6f8(0)                          ; LM3532 backlight OFF
190e8  0x18ce8 ; 190ec 0x18b6c             ; DSS clocks / DISPC reset
1910c  0x162ec(cfg, r5) ; 19110 0x18a7c    ; DISPC timing/overlay
1911c  0x179dc                             ; DSI1 PHY/PE/PLL programming (117 §1)
19124  0x16610                             ; DSS_CONTROL
19134  cmp r3,#2 ; bne 0x1919c             ; mode==2 -> 0x1913c (dual-DSI) and SKIPS the pulse
                                             ; mode 1 (splash) / 0 (power_on) -> 0x1919c = pulse block
--- 0x1919c: branch on a global reached through GOT slot 0x2b12c ---
---  (the GOT word's R_ARM_RELATIVE addend is 0x23bb8, but that location is not a clean
---   scalar in the file image, so its identity was NOT used: the branch is decided from
---   live register state, §7.2) ---
191a4  if (flag == 0)  ->  1927c:
192c0-192d4    GPIO2+0x190 <- 0x01000000   (CLEAR GPIO2_24)   <-- RESET ASSERTED
192d8          usleep(20000)               ; 20 ms
19324-19338    GPIO2+0x194 <- 0x01000000   (SET   GPIO2_24)   <-- RESET RELEASED
1933c          usleep(20000)               ; 20 ms
191a8  else (flag != 0) -> 191b0:
191f4-19208    GPIO4+0x190 <- 0x00000040   (CLEAR GPIO4_6)
1920c          usleep(20000)
19258-1926c    GPIO4+0x194 <- 0x00000040   (SET   GPIO4_6)
19270          usleep(20000)
--- both branches rejoin at 0x19344 ---
19344  0x17094                             ; DSI second pass
1935c  0x196dc(r0)                         ; bridge/panel probe (-t/-T family)
1938c  0x13e50
19390  0x16bd8                             ; DSI VC enable
193c0  if (mode == 1) 0x1c580               ; panel strap read  -> GPIO6 (117, 118)
193cc                  0x1c6f8(1)          ; LM3532 ON
```

`usleep` here is exact: PLT `0x40f0` → `GOT 0x2b028` = `usleep`, and the argument is
`movw r0,#20000`, so **20 000 µs = 20 ms** of low time and 20 ms of settle after release.

Teardown (`power_off`, function starting near `0x1abfc`): `GPIO2_13`, `GPIO2_24`, `GPIO4_6`
are made outputs (`bic` on `GPIO_OE` at `0x1aca8`, `0x1ad78`, `0x1ae4c`, `0x1af08`) and then
driven low (`0x1b2dc`, `0x1b334`, `0x1b388`). That is why a *powered-off* reference would show
all three low; a *running* one shows `GPIO2 DATAOUT = 0x31013200` (bits 13, 24 high) and
`GPIO4 DATAOUT = 0x90000120` (bit 6 low, never set).

### 7.2 Which branch runs on this unit (this is how candidate 1 vs 2 is decided)

Both branches end with their bit **set**, so a running unit must show that bit high. Live
reference (118 §10.1), decoded with OE bit = 1 ⇒ *input*:

| bank | `GPIO_OE` | outputs (OE bit = 0) | `GPIO_DATAOUT` | driven high | driven **low** |
|---|---|---|---|---|---|
| GPIO1 | `0x6ffb1faa` | 0,2,4,6,13,14,15,18,28,31 | `0x00006044` | **2, 6, 13, 14** | 0,4,15,18,28,31 |
| GPIO2 | `0xce3ecd5f` | 5,7,9,12,13,16,22,23,**24**,28,29 | `0x31013200` | **9,12,13,16,24,28,29** | 5,7,22,23 |
| GPIO4 | `0x6ffffe1c` | 0,1,5,6,7,8,28,31 | `0x90000120` | **5,8,28,31** | 0,1,**6**,7 |
| GPIO3/5/6 | `0xffffffff` | — (all inputs) | 0 | — | — |

(In every bank, `DATAOUT & ~OE` = 0, i.e. every high `DATAOUT` bit is on an output — an
independent check that the OE polarity used here is the right one.)

⇒ **GPIO2_24 is an output driven high; GPIO4_6 is an output driven low.** The
`flag == 0` / GPIO2_24 branch is the one taken on this unit. (GPIO2_13 and GPIO1_13 are also
outputs driven high; GPIO2_4 and GPIO6_11 are inputs, consistent with being bridge-driven
readbacks.)

### 7.3 Candidate 1 — `GPIO2_24` = global GPIO 56 (best RESETN / power-enable match)

| field | value |
|---|---|
| bank.bit | `GPIO2` (`0x48055000`) bit **24**, mask `0x01000000`, OMAP global **GPIO 56** |
| pad | `CONTROL_CORE_PAD0_G` **GPMC_NADV_ALE**, offset **`0x080`** (`swpu231ap.pdf.txt:254069`, `:276123`); `MUXMODE` bits 2:0, **reset `0x0`** (= `gpmc_nadv_ale`), **`0x3` = `gpio_56`**, `0x4` = `sys_ndmareq3` |
| assert write | **`winch_lcdctl` vaddr `0x192d4`** — `str r2,[r3]` with `r2 = 0x01000000`, `r3 = GPIO2+0x190` (`GPIO_CLEARDATAOUT`) |
| hold | `usleep(20000)` at `0x192d8` → **20 ms** |
| release write | **vaddr `0x19338`** — `str r2,[r3]`, `r3 = GPIO2+0x194` (`GPIO_SETDATAOUT`), `r2 = 0x01000000` |
| settle | `usleep(20000)` at `0x1933c` |
| timing in boot order | inside `.script` step 14 (`sh /proc/boot/splash_script` → `winch_lcdctl -s splash`), **after** DISPC timing + full DSI1 PHY/PE/PLL programming, **before** the panel strap read on GPIO6 and **before** LM3532 backlight-on. Nothing earlier in the boot order touches this pad. |

Why it is the best match: it is the only **edge** (low → release) on a display-path line in
the whole boot set; 20 ms is a classical reset pulse width; it is the last thing that
"wakes" the panel path before the strap read; and it is *not* visible in the steady-state
`DATAOUT` bitmap, which is exactly the class of pin the earlier reports were looking for.

### 7.4 Candidate 2 — `GPIO4_6` = global GPIO 102 (same pulse, other branch)

`GPIO4` (`0x48059000`) bit 6, mask `0x40`, global GPIO 102, pad `0x090`
(`swpu231ap.pdf.txt:254111`, `:276907`; `MUXMODE` bits **18:16**, **reset `0x7`** = safe_mode, `0x3` = `gpio_102`), assert at **`0x19208`**, release at
**`0x1926c`**, 20 ms either side. Not taken on this unit (see §7.2) — and note the reference
therefore *actively drives it low*, so it cannot be the "held in reset" culprit for us
either unless we also drive it low.

### 7.5 Candidate 3 — `GPIO2_13` = global GPIO 45 (level enable, already known from 117)

`GPIO2` bit 13, mask `0x2000`, global GPIO 45, pad `0x068`
(`swpu231ap.pdf.txt:254022`, `:274808`; `MUXMODE` bits 18:16, reset `0x7`, `0x3` = `gpio_45`). **SET** at **`0x190c8`** (first thing the
splash path does, before any DISPC/DSI write); **CLR** at `0x1b394` on teardown. No pulse.
It is a level, and both the reference and our build hold it high, so it is *not* the
difference — but it is the natural "bridge/panel enable" line.

### 7.6 Candidate 4 — `GPIO1_13` = global GPIO 13, in `winchester-bootreason` (not display)

`mmap_device_io(0x1000, 0x4A310000, 0, …)` at **`0x1c40`–`0x1c50`** inside
`winchester-bootreason`'s function `0x1c2c(r0)` (the `led_init`/`led_blink` family; sole
caller `0x20e0`). Helper `0x1794(base, mask, val)` is the same 2-instruction GPIO writer as
report 119's:

```
0x1ca8  gpio_set(GPIO1, 0x2000, 1)      ; SET   GPIO1_13
0x1cb4  delay(15)
0x1cbc  i2c_write(0x36, reg 0x10, 0x00) ; slave 7-bit 0x36 on /dev/i2c0
0x1ce4  gpio_set(GPIO1, 0x2000, 0)      ; CLEAR GPIO1_13   <-- reset asserted
0x1cf0  delay(15)
0x1cf8  gpio_set(GPIO1, 0x2000, 1)      ; SET   GPIO1_13   <-- released
0x1d08  delay(15)
0x1d1c  i2c_read(0x36, reg 0x10, 16 bytes)  ; then parse the "power-on condition"
```

This is a **real H→L→H reset pulse** that runs *earlier* than `winch_lcdctl` (it is the first
line of `splash_script`), and the reference then leaves **GPIO1_13 high**
(`GPIO1_DATAOUT = 0x00006044`, bit 13 set; our build reports `0x00004044`, bit 13 **clear**
⇒ we actively drive it low). Because it is bound to an I²C slave at 0x36 and lives in the
"boot reason / charge LED" code, I do **not** think it is the display bridge — but it is the
one place in the boot set where a line the reference leaves high is left low by us, so it
belongs on the check-list (and it is a cheap thing to fix).

### 7.7 Candidate 5 — `GPIO36` / `GPIO171` identify two real OMAP↔bridge nets

`winch_lcdctl`'s `-t/-T` self-test (`-T` doc string @`0x2dc24`: *"Use bridge GPIOs to test
DSI. Send DSI commands to program bridge GPIO as outputs and then read state on OMAP"*):

```
0x19868-0x19878  GPIO6+0x134 (GPIO_OE) |= 0x00000800   ; GPIO6_11 (global 171)
0x19970-0x19980  tst GPIO6+0x138 (DATAIN) & 0x800
0x196a8          slogf("GPIO status good (GPIO36 and GPIO171 high)")
0x19600          slogf("Fail GPIO36 off")     ; GPIO2 bit 4  (global 36, mask 0x10)
0x19684          slogf("Fail GPIO171 off")    ; GPIO6 bit 11 (global 171, mask 0x800)
```

`GPIO2` bit 4 and `GPIO6` bit 11 are **inputs** in the reference (`OE` bits 4 and 11 are 1) and
both read high on the live unit (118 §"GPIO register dump", row `GPIO6`:
`OE=0xffffffff`, `DATAIN=0x00010800`, `DATAOUT=0`) — i.e.
`GPIO2_DATAIN & 0x10` (from `0x08000018`) and `GPIO6_DATAIN & 0x800` are both **1**. So
GPIO 36 / 171 are the OMAP ends of the TC358764's own two GPIO balls. They are not a reset —
but they are the reference's own *liveness* check on the bridge, and both must read **1**
for the board to be considered good.

> **Hardware-free diagnostic worth adding to the kernel console today:** read
> `GPIO2_DATAIN` bit 4 and `GPIO6_DATAIN` bit 11. `1/1` ⇒ the bridge is out of reset and
> powered; anything else ⇒ keep looking at power/reset, not at DSI.

### 7.8 What I did NOT rule out

1. **The pad mux — the biggest open item, and the most likely explanation of the failure.**
   No boot-set binary writes `CONTROL_CORE_PAD0_G` `0x058`/`0x068`/`0x080`/`0x090`,
   `_K` `0x17C` or `_D` `0x1B0`. Their reset `MUXMODE` values are **`0x0`
   (`gpmc_nadv_ale`, `gpmc_ad12`) and `0x7` (safe_mode, output disabled) for `0x068`,
   `0x090`, `0x1B0`** — *not* GPIO. So on the reference those pads must be re-muxed by
   something **before QNX starts**, i.e. **the RIM bootloader**. That the reference really
   does have them muxed is proved by the code itself: the `-t/-T` bridge self-test *reads*
   GPIO36 (pad `0x058`) and GPIO171 (pad `0x17C`) and would fail 100 % of the time if the
   pads were still on `gpmc_ad12` / `kpd_col3`.
   Consequence for us: if our chainloader leaves `0x080` in mode 0, then every
   `GPIO2_DATAOUT` write we do — *and every one QNX does* — is invisible at the ball, and
   the bridge's RESETN is left to whatever the board's pull does. This is a stronger
   candidate for "we match QNX's `DATAOUT` exactly and nothing happens" than any register
   value, and it is **not** answerable from the boot set (the bootloader is not in the dump).
2. **Whether it is `GPIO2_24` or something off-OMAP.** No string, symbol or table in the boot
   set names the net. A board-level enable could equally be a TWL6030 `regen`/`vaux` rail or
   an RC/POR network with no software actor at all; I did not and cannot prove the
   schematic.
3. **`procnto-smp-instr` (614 400 B).** Its `PT_LOAD`s have non-zero vaddrs
   (`0xfe033000`), so it did not fit the "vaddr == offset" anchoring and was not
   instruction-swept. It is the kernel, not a board-support actor, and the `.script` gives it
   no arguments that could name a pin — but I did not prove its innocence at instruction
   level. (Everything else in §2–§6 is proven.)
4. **The RIM bootloader itself.** Not present in this dump. If the bridge's POR/RESETN is
   driven by the bootloader through an MMU/IO path we have not looked at, that is outside
   this scope by construction.
5. **The `0x4A100000 + 0x618` read-modify-writes** in `winch_lcdctl`
   (`0x17f2c-0x17f34` OR `0x1F000000`; `0x17ff0-0x17ff8` OR `0x0F80000`). They are inside the
   mapped CTRL_CORE window but **outside** the `CONTROL_CORE_PAD0_x` offset range, so they are
   not pad muxes; I did not identify which register they are, and I deliberately did not
   chase them (they belong to the DISPC/DSI path, which is out of scope).
6. **`GPIO2` bits 5,7,22,23 and `GPIO4` bits 0,1,6,7 and `GPIO1` bits 0,4,15,18,28,31** are
   outputs that the reference drives **low** (full enumeration in the §7.2 table). I did not
   try to attribute them to board nets. If the bridge enable is active-low and driven low by
   the reference that would contradict "the reference works", so these are only interesting
   as *unexplained* low outputs (e.g. GPIO4_6, which the not-taken branch of §7.4 would have
   set high).

### 7.9 The actionable short list

1. In the chainloader/kernel, before touching DSI: set `CTRL_CORE_PAD0_G 0x080` = mode **4**
   (`gpio_56`), `GPIO2_OE` bit 24 = **0** (output), then reproduce the reference exactly:
   `GPIO2_CLEARDATAOUT = 0x01000000`, 20 ms, `GPIO2_SETDATAOUT = 0x01000000`, 20 ms.
   Do the same for `0x068`/mode 3 (`GPIO2_13`, set high and leave), and `0x090`/mode 3
   (`GPIO4_6`, drive low) in case the unit takes the other branch.
2. Also mux `0x058` (mode 3) and `0x17C` (mode 3) and read them: `GPIO2_DATAIN` bit 4 and
   `GPIO6_DATAIN` bit 11 must both be 1 — the reference's own pass/fail criterion.
3. Belt-and-braces: replicate `winchester-bootreason`'s `GPIO1_13` H→15→L→15→H pulse
   (pad `0x1B0`, mode 3 = `dpm_emu2`, reset `0x7`) — we currently leave that line low while the
   reference leaves it high.
4. Only after 1–3 fail should the DSI-side programming be revisited.

---

## Appendix A — reproduce the sweep

```
cd /home/psyden/rea-work/playbook-dev/dumped4869ifs/proc/boot
OBJ=/home/psyden/SSDdrive/wsl-migration/home/psyden/toolchains/armv7-eabihf/bin/arm-linux-objdump

# 0. text vaddr == file offset for every file here except procnto-smp-instr;
#    slice off LOAD-1 and disassemble it as raw ARM.
python3 - <<'EOF' > segs.txt
import struct,glob
for p in sorted(glob.glob('*')):
    d=open(p,'rb').read()
    if d[:4]!=b'\x7fELF': continue
    phoff,=struct.unpack_from('<I',d,0x1c); phes,=struct.unpack_from('<H',d,0x2a); phn,=struct.unpack_from('<H',d,0x2c)
    for i in range(phn):
        t,off,va,pa,fsz,msz,fl,al=struct.unpack_from('<IIIIIIII',d,phoff+i*phes)
        if t==1 and fl&1:
            print(p,off,va,fsz); break
EOF
while read -r f off va sz; do head -c $((off+sz)) "$f" > /tmp/x.bin;   $OBJ -D -b binary -m arm --adjust-vma=$va /tmp/x.bin > /tmp/dis-$f.txt; done < segs.txt

# 1. literal-pool pass: who can build a GPIO / CTRL_CORE base as a data word?
python3 - <<'EOF'
import struct,glob
pats=[0x4A310000,0x48055000,0x48057000,0x48059000,0x4805B000,0x4805D000,0x4A100000]
for p in sorted(glob.glob('*')):
    d=open(p,'rb').read()
    if d[:4]!=b'\x7fELF': continue
    for v in pats:
        b=struct.pack('<I',v); i=d.find(b)
        while i>=0:
            print(p,hex(v),hex(i)); i=d.find(b,i+1)
EOF

# 2. movw/movt pass + accessor pass
grep -nE "movt\s+r[0-9]+, #(18993|18960|18437)" /tmp/dis-*.txt      # 0x4A31 / 0x4A10 / 0x4805
grep -nE "add\s+r[0-9a-z]+, r[0-9a-z]+, #(404|400|308|312)\s+@\s+0x1" /tmp/dis-*.txt  # +0x194/0x190/+0x134/+0x138
```

Expected / observed result:

* `0x4A310000` — `pmic-winchester-twl6030` (`0x5e60`), `winchester-bootreason` (`0x1c48`).
  **No others.**
* `0x4805_5/7/9/B/D_000` — **no literal anywhere**; `movt #0x4805` exists only as
  `0x4805_6000` = `PER_TA_GPIO2` in `spi-omap4430.so` (`0x2764`, `0x2f44`) and
  `devb-mmcsd-winchester` (`0x8274`).
* `0x4A100000` — `setup-core-inactive` (rodata base table only) and `winch_lcdctl`
  (`0x1bff8`, mmap). No `movt #0x4A10` anywhere else.
* The `+0x190/+0x194` accessor shape appears with a *mapped GPIO base* only in
  `pmic-winchester-twl6030` (`0x5cb8`/`0x5ccc`, GPIO1 bits 2/15) and `winch_lcdctl`
  (`0x190c0`, `0x19200`, `0x19264`, `0x192cc`, `0x19330`, `0x1b2e0`, `0x1b338`, `0x1b38c`),
  plus `winchester-bootreason`'s `addne/addEQ`-suffixed helper at `0x1798`/`0x17a0`.
  All other `#400/#404` hits in the tree (`io-blk.so`, `random`, `libc`, `libsocket`,
  `libsbgse56`, and `winch_lcdctl` `0x7158`/`0x71bc`/`0xe5a4`/`0xe61c`) are
  `sp`-relative or message-string arithmetic — distinguishable because they are followed by
  `str rX,[sp]` / `bl slogf`, never by `str rX,[rX]`.

## Appendix B — the one-line OE decode used in §7.2

```python
for name,oe,do in [('GPIO1',0x6ffb1faa,0x00006044),('GPIO2',0xce3ecd5f,0x31013200),
                   ('GPIO4',0x6ffffe1c,0x90000120)]:
    outs=[b for b in range(32) if not (oe>>b)&1]       # OE bit 0 => output
    print(name,'outputs',outs,'hi',[b for b in range(32) if (do>>b)&1])
    assert do & ~oe == 0, "a DATAOUT-high bit is not an output -> polarity wrong"
```
