# 238 — BT works: the core inquires; the kernel's LE init is what blocks `hci0`

Date: 2026-11-15 (session 53, Linux-era 47).
Bench: image **`0xDD66D8C3`** + region-1 rootfs **`49e318bf…`** (43 modules).

## 0. Headline

**The WL1283's BT core does real Bluetooth, and loading its service pack makes
`hci0` a real mgmt controller for the first time.**  Three results, all measured:

1. **The controller works.**  Driven directly over UART2 as H4 it answers
   `HCI_Reset`, reports its own identity, and **runs a complete inquiry that
   finds a nearby device**:

```
btinq2: reset ok (st 0x00)
btinq2: local bdaddr 8e:e4:3c:22:61:00
btinq2: hci ver 6 rev 0x0000 lmp ver 6 subver 0x0f00 mfr 0x002b
btinq2: inquiry started, st 0x04, 9 x 1.28 s
btinq2: Inquiry Result, 1 device(s), plen 15
btinq2: FOUND f0:a3:b2:bb:d1:00  class 0x011c00
btinq2: inquiry complete
```

   Reproduced twice, same peer.  `mfr 0x002b` = Texas Instruments, `hci ver 6` /
   `lmp ver 6` = Bluetooth 4.0.

2. **The service pack loads and it is what registers the controller.**  315/315
   commands answered, LMP subversion `0x2b0f` → **`0x2b33`**, and — the result
   that matters — with the pack loaded first, `MGMT_OP_READ_INDEX_LIST` returns
   **one controller** (`13 B: … 03 00 00 01`); without it, **zero**
   (`11 B: … 03 00 00 00`).  Negative control run both ways.  Session 52's
   premise ("`hci0` exists, use mgmt to power it") was unreachable, because
   `hci0` was never in mgmt's index list; it is now.

3. **Powering still fails, on a stray byte.**  `MGMT_OP_SET_POWERED` returns
   `0x05` (`AUTH_FAILED` = `-ETIMEDOUT`), and dmesg shows
   `Frame reassembly failed (-84)` (`-EILSEQ`) then
   `Opcode 0x1003 failed: -110`.  A temporary instrument in `h4_recv()` found the
   cause, in 6 of 6 runs: after the last init reply completes, a **lone `0x30`
   byte** arrives and is (correctly) rejected as not-an-H4-type.  Whether the
   chip or the host emits it is the open question — it never appears from
   userspace (§4).

**And the LE question is answered separately**: mainline's LE init stage is why
`hci0` could not open at all, and the chip refuses LE commands with
`Command Disallowed`.  Both are covered below.

| command | reply | verdict |
|---|---|---|
| `LE_Read_Buffer_Size` (0x2002) | `04 0e 07 01 02 20 00 1b 00 0f` | Success |
| `LE_Read_Local_Supported_Features` (0x2003) | `… 00 00 00 00 00 00 00 00 00` | Success, **all-zero features** |
| `LE_Read_Supported_States` (0x201c) | `04 0e 08 01 1c 20 **0c** …` | **Command Disallowed**, short |
| `Write_LE_Host_Support` (0x0c6d) | `… **0x11**` | Unsupported parameter value |

`hci_le_read_supported_states_sync()` is the **last** step of `le_init2`, and
`hci_init_stage_sync()` aborts the whole stage on a non-zero return.  The reply
is also **too short**: 5 payload bytes where
`struct hci_rp_le_read_supported_states` needs 9, so `hci_cc_func()` logs
`unexpected cc 0x201c length: 5 < 9` and returns `HCI_ERROR_UNSPECIFIED`, which
`bt_to_errno()` maps to `-ENOSYS` — the `failed: -38` in every dmesg.

The consequence chain is what matters:

```
LE_Read_Supported_States -> 0x0c / short
   -> hci_dev_init_sync() fails
      -> hci0 never completes opening
         -> mgmt reports num_controllers = 0
            -> MGMT_OP_SET_POWERED is impossible
```

**Loading the service pack breaks the first link of that chain.**  With the pack
in place `0x201c` returns a full 12-byte event, so the length check passes and
the controller registers (§2).  What remains is the framing fault of §4.

## 1. Measuring it, and the two tools that lie

The useful instrument turned out to be the dumbest one.  Three probes were
written; two of them lied, in ways worth recording:

* **A buffered stdout discarded the evidence.**  A static ARM binary writing to a
  pipe is block-buffered, so when `timeout` SIGTERMs it, *every* line is lost and
  the tool looks silent.  `btinq` was reported "silent" for several runs before
  this was noticed.  `setvbuf(stdout, NULL, _IOLBF, 0)` fixes it; do this in
  every diagnostic that may be killed.
* **Command Complete and Command Status do not share a layout.**  For a
  **Command Complete** (`0x0e`) the opcode is at offset 4:
  `04 0e len num op_lo op_hi status`.  For a **Command Status** (`0x0f`) it is at
  offset **5**: `04 0f len status num op_lo op_hi`, and the event is only 7 bytes
  long.  A matcher that reads `ev[4] | ev[5] << 8` for both silently **drops
  every Command Status reply** — and `HCI_Inquiry`, `HCI_Create_Connection` and
  every other asynchronous command answer with Command Status.  This is exactly
  why a working inquiry looked like no reply at all.  Verify with a raw hexdump,
  never with a matcher.

The tool that settled it is `btdump <op> [dev] [ms] [param-hex]`: it sends one
command and hexdumps **everything** that arrives, with no interpretation.  That is
how the Inquiry Result event was found after the parser had discarded it.

## 2. The service pack: it loads, it takes, and it is not the LE gate

`WL1283.bts` (the 1917 reference's own pack, 76 587 B) was replayed with a
userspace H4 loader (`btsload`):

```
btsload: /tmp/WL1283.bts, 76587 B, version 1
btsload: HCI_Reset answered
btsload: 315 command(s) sent, 315 answered, 0 unanswered
```

**All 315 commands answered.**  The pack is 227 × `0xFF05` write-memory-block
(54 728 bytes of patch RAM), 49 × `0xFD97`, 5 × `0xFF01`, 8 × `0xFF26`, then
`0xFD09` "release Cortex Reset" and `0xFDA8` "enable Cortex Sleep".

**It demonstrably took:** the LMP subversion moved `0x2b0f` → **`0x2b33`**, which
is exactly what the pack's own header declares
(`# Quattro LMP Subversion: 2b33`, and its version string is `TI_P6_15.51`).

**And it changed `0x201c`'s shape, which is what unblocks `hci0`.**  Before the
pack the reply was 11 bytes with `0x0c` and four bytes that change every call
(`94bc8103`, `96395cc3`, `62060ac6` — junk, hence the kernel's `5 < 9`).  After
the pack it is a full 12 bytes with `0x0c` and eight plausible state bytes
(`00 fa 38 05 00 20 44 02`).  The **status** still says Command Disallowed, but
the **length** now satisfies `hci_cc_func()`, so the event is parsed instead of
rejected and `hci0` gets far enough to register with mgmt.

Proven causally with a negative control, same attach procedure, only the pack
differing:

| before the attach | `MGMT_OP_READ_INDEX_LIST` |
|---|---|
| no pack | `11 B: 01 00 ff ff 05 00 03 00 00 00 00` → **0 controllers** |
| `WL1283.bts` loaded | `13 B: 01 00 ff ff 07 00 03 00 00 01 00 00 00` → **1 controller** |

And `MGMT_OP_READ_INFO` then returns a real controller: BD_ADDR
`8e:e4:3c:22:61:00`, HCI version 6, manufacturer `0x000d`.

**A second, separate fact about the pack: `HCI_Reset` does not wipe it.**  A
probe inside the loader reads `0x201c` before and after a reset and both are
15-byte events, so the patch RAM survives a reset (read `04 0e 0c 01 1c 20 0c
00 00 00 01 00 20 70` → reset → `… 0c 00 52 06 20 ff f7 18`).  It is lost by a
`BT_EN` power cycle, which is why `pbhci`'s `--no-pulse` matters once the pack
is in.

Three hypotheses were tested and **refuted**:

* **"The core is asleep, so it disallows the command."**  The pack's last action
  is `0xFDA8 00` = *enable* Cortex Sleep (the reference sends `0xFDA8 01` =
  disable at the start of its script).  Re-running the load with `0xFDA8 01`
  appended, then reading `0x201c` immediately, gives the same `0x0c`.  Sleep is
  not the gate.
* **"`Write_LE_Host_Support` must come first."**  Sending it before `0x201c`
  returns `0x11` and does not change the states reply.
* **"The chip sleeps between the kernel's init commands."**  Measured
  back-to-back, with a 1 s gap, and with a 2 s gap: `0x1002` then `0x1003`
  answers every time from userspace.  The chip does not idle out between
  commands.

## 3. Which firmware file: settled

The open contradiction `docs/contradictions/bt-firmware-tiinit-vs-wl1283.md`
asked whether to use linux-firmware's `TIInit_7.2.31.bts` or the reference's
`WL1283.bts`.  It is settled by reading both headers:

| file | header |
|---|---|
| reference `WL1283.bts` | `Description : Quattro L ROM Initialization Script`, `Compatibility: Quattro, 6.0.15 ROM`, `Version: TI_P6_15.51`, `# Quattro LMP Subversion: 2b33` |
| linux-firmware `TIInit_7.2.31.bts` | `Description : ORCA-Trio BTIP 2.0 ROM Initialization Script`, `Compatibility: BL6450, WL1271/3 2.0 ROM`, `Version: TI_P31.115`, `Notes: Use this script on BL6450, WL1271/3 2.0 ROM device only` |

`Quattro` is this chip's BT-core generation; `BL6450/WL1271/3 2.0` is a
different, older family.  The other `TIInit_*` names fetched for comparison
(`6.2.31`, `6.6.31`, `6.7.16`, `7.6.15`, `10.2.8`) are all
`BL6450, WL1271/3 2.0` as well.  **The reference's file is the right one** — as
report 216 §0 argued from the file's shape, now confirmed from its own header
and from the subversion it actually produces.

**The `wl128x-fw-4-sr.bin` / `5-sr.bin` files are unrelated.**  They are loaded
by `request_firmware()` into the **WLAN** core (`wlcore/main.c`), not the BT
core.  Switching to 5-sr cannot change BT behaviour; it only changes the WLAN
firmware, which 4-sr is chosen for (report 235, trap 243).

## 4. The remaining blocker: a lone `0x30` byte breaks the kernel's H4 receiver

`MGMT_OP_SET_POWERED` returns status `0x05` = `MGMT_STATUS_AUTH_FAILED`, which
`mgmt_status()` maps from **`-ETIMEDOUT`** — the power request reaches the kernel
and its command times out.  dmesg shows, on every attempt:

```
Bluetooth: hci0: Frame reassembly failed (-84)     <- -EILSEQ
Bluetooth: hci0: Opcode 0x1003 failed: -110        <- -ETIMEDOUT
```

**A temporary instrument in `h4_recv()` found the byte** (the error path does not
print the data, which is the same lesson as report 232).  Every failure has the
identical shape, in **6 of 6** runs:

```
PBTMP h4_recv count=9 first=04 0e 06 01  rx_len=-1: 04 0e 06 01 12 0c 00 00 00
PBTMP h4_recv count=1 first=30 00 00 00  rx_len=-1: 30
>>> Bluetooth: hci0: Frame reassembly failed (-84)
```

The last init reply is **complete and correct**, and then, about 200 ms later, a
**single `0x30` byte** arrives on its own.  `0x30` is not an H4 packet type
(0x01–0x04), so `h4_recv_buf()` returns `-EILSEQ` exactly as designed.  The
kernel is behaving correctly; the stream contains a byte that does not belong.

`0x30` is ASCII `'0'`.  What is **not** yet established is whether the chip emits
it or something on the host side does:

* **From userspace it never appears.**  Driving the same sequence by hand —
  including a passive 3 s listen with no command sent — produces clean H4 framing
  every time.  So it is not simply "the chip emits junk after init".
* It is **not console traffic**: UART2 is not in `/proc/consoles`, and
  `ttyS1` is nobody's console.
* It is **not `hci_uart`'s flow control**: `hci_uart_set_flow_control()` is only
  called by the vendor protocols (`hci_bcm`, `hci_qca`, `hci_intel`, `hci_mrvl`);
  **H4 never calls it**, and `hci_uart_setup()` is a no-op for H4 (`h4p` has no
  `setup`/`set_baudrate`).  An early hypothesis that it was in that path was
  **wrong and is retracted**.
* It is **not a lost/short frame in general**.  An earlier note here claimed the
  252-byte `Read_Local_Name` reply was corrupt; that was **my own display buffer
  truncating at 240 bytes**, not the wire.  Re-measured properly, that reply
  arrives complete (255 bytes, `04 0e fc 01 14 0c 00 00 …`) with flow control on
  *or* off.

### What is excluded, by measurement

* **alignment/padding** — `hci_ldisc.c:533-535` sets `hu->alignment = 1`,
  `hu->padding = 0`, and H4 does not override them.
* **the chip idling out between commands** — `0x1002` then `0x1003` answers
  back-to-back, after 1 s, and after 2 s.
* **leftover unread bytes poisoning the next frame** — after `0x1002` a following
  `HCI_Reset` sees exactly its own 7-byte reply.
* **UART line errors** — `UART2` `LSR` reads `0x60` (THRE|TEMT; no overrun,
  parity or framing bit).
* **a long reply being truncated** — 71-byte and 255-byte replies both arrive
  complete, split across tty chunks, which `h4_recv_buf()` reassembles correctly
  (its `rx_len` climbs 2 → 48 → 113 → 178 → 203 across calls).
* **hardware flow control as the discriminator** — an A/B on a clean boot gives
  **the same result both ways** (`num_controllers = 1` with `CRTSCTS` on *and*
  off).  An earlier reading of this as a clean win was taken while an
  instrumented module was still loaded, and does **not** reproduce; it is
  retracted.

### The next experiment

Find the origin of the `0x30`.  The instrument is written and proven; what is
needed is a **clean** module swap (the previous attempt left the old
`hci_uart` loaded because `btbcm` holds a reference — unload `btbcm` *first*,
then `hci_uart`, then `insmod` the instrumented one, and confirm by
`strings … | grep PBTMP` that the file is the instrument).  Print, in the
instrument, the byte plus the tty's `tty->disc_data`/termios state and the exact
inter-call gap, and log the bytes the driver *writes* too (`hci_uart_write_work`)
so a host-side echo is visible.  A `0x30` that never appears from userspace but
appears 200 ms after the kernel's last init reply most likely has a host-side
origin, and knowing which side emits it decides the fix.

## 5. What this means for the goal

The operator wants full BT: BR/EDR **and** LE, enough for HID (a tablet as a
wireless keyboard/mouse).  The position today:

* **BR/EDR works at the transport and controller level** — reset, identity,
  inquiry with a real peer.  That is the layer HID-over-BR/EDR needs.
* **`hci0` now registers with mgmt** once the service pack is loaded, which it
  never did before.  What is left is opening it (§4), which is a UART framing
  bug, not a BT capability question.
* **LE is refused by the controller** (`0x0c` with a plausible state payload,
  `Write_LE_Host_Support` = `0x11`, LE feature page all zero).  Whether this is
  fixable at all is open; the reference stack has no LE code (see below).

### The reference cannot be copied for LE

Static RE of the reference's own BT stack (file-only, in a subagent) found it is
**BR/EDR-only**: `btp-btmgr.so` has a 154-entry HCI command table whose highest
opcode is `0x1804`, **zero** entries in the LE range (OGF 0x08), a 61-entry event
table with **no LE Meta (0x3E)**, and no `movw #0x20xx` / `#0x0c6d` anywhere in
`.text`.  It contains no SMP/ATT/GATT code and no LE config key, and there is no
BlueZ in the reference rootfs.  So there is **no reference LE sequence to
replay**, and "the reference does it" cannot be the answer here.

## 6. Corrections this report makes

* **Report 237 §4 is wrong** where it says `0x201c` is "a TI vendor op that
  `hci_uart` sends generically" and that the error is benign.  `0x201c` is
  OGF 0x08 = **LE Controller Commands**, OCF 0x01C =
  **`HCI_LE_Read_Supported_States`**.  It is standard, the kernel is right to
  send it, and on a bare ROM the failure is **fatal to `hci0` registering** — it
  is the whole blocker, not a curiosity.  Trap 249.
* **Session 52's "use mgmt `SET_POWERED`" plan was not viable as stated**,
  because on a bare ROM the controller is not in mgmt's index list at all
  (`num_controllers = 0`) and power is downstream of that.  It *is* viable once
  the service pack is loaded — which is the new fact.
* **`hci0` appearing in `/sys/class/bluetooth/` does not mean it opened.**  The
  sysfs node is created by `hci_uart` on `HCIUARTSETPROTO`; opening is a
  separate, later step.  Presence of the node is not liveness (a sibling of
  trap 234's lesson).  Registration with **mgmt** is the real milestone, and
  that is what `READ_INDEX_LIST` measures.
* **"Use 5-sr for BT"** — the `wl128x-fw-*-sr.bin` files are WLAN-core firmware;
  the BT core's firmware is the `.bts` pack.  They are different files for
  different cores.

## 7. Next

1. **Find who emits the `0x30`** (§4).  The instrument exists and is proven; the
   swap has to be done in the right order — `rmmod btbcm` **first**, then
   `hci_uart`, then `insmod` the instrumented one, and verify with
   `strings … | grep PBTMP` that the loaded file really is the instrument (a
   previous attempt silently kept the old module, because `btbcm` holds a
   reference and `insmod` reported success anyway).
2. **Then** `HCIDEVUP`/mgmt power, an HCI-socket inquiry through the stack, and
   only then `bluetoothd` + HID (`CONFIG_BT_HIDP=m` is already set; the
   module is **not** shipped in region 1 — add it).
3. **Also needed for a usable stack:** `l2cap`/`sco`/`rfcomm`/`hidp` are modules
   and none are in the region-1 rootfs (only `bluetooth`, `hci_uart`, `btbcm` and
   their crypto deps are).  Ship them before expecting HID to work.
4. **LE is a separate, later question.**  The *controller* refuses it (`0x0c`,
   `Write_LE_Host_Support` = `0x11`, LE feature page all zeros), but note two
   things before writing it off: the chip reports **HCI 4.0 / LMP 4.0** and its
   LMP feature page sets the LE-supported bit, and the reference OS being
   BR/EDR-only says nothing about the silicon — it reflects that BlackBerry never
   shipped an LE stack on a 2011 tablet.  So "the reference does not do it" is
   **not** evidence that the chip cannot.  What is missing is a working enable
   sequence, not a demonstrated impossibility.

## 8. Artefacts

* Tools (bench, `/tmp`, not yet in `rootfs/`): `btdump` (raw hexdump — the one
  that does not lie), `btinq2` (inquiry on the proven skeleton), `btgate` (LE
  gate walk), `btsload` (the `.bts` H4 loader), `btsurvey` (capability survey),
  `leprobe`, `lehost`, `btdetach` (reset `N_HCI` → `N_TTY`).
* The service pack was replayed from `local/work/1917/base/lib/firmware/bluetooth/WL1283.bts`
  (md5 `78bc1f0a…`, identical to `local/work/qnx-fw/WL1283.bts`).
* No NVRAM, MCT, RPMB or flash write.  Nothing was added to the UFS.
