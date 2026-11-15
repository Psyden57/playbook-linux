# Session 53 (absolute) — BT works: the core inquires, and the service pack registers `hci0`

> Numbering: absolute `53` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 47** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-15.
Unit state at the end: **our Linux image `0xDD66D8C3`** on region 0, region 1
**`49e318bf…`** (43 modules).  `0fca:8010`, console `pb> `, **ssh on
`169.254.0.1`**.  No flash, no NVRAM/MCT/RPMB write, no restore needed.

## Goal of this session

Handoff item (a): BT — power `hci0` over mgmt, load the service pack, and give
it a stack.  The operator then set the real target: **fully working Bluetooth,
LE included where possible, enough for HID** (the tablet as a wireless
keyboard/mouse).

## Confirmed (measured, read in a file, or seen on the bench)

* **The BT core does real Bluetooth.**  Over a plain H4 tty it answers
  `HCI_Reset`, reports `hci ver 6 rev 0x0000`, `lmp ver 6 subver 0x0f00`,
  `mfr 0x002b` (Texas Instruments) and BD_ADDR `8e:e4:3c:22:61:00`, then **runs
  a full BR/EDR inquiry that finds a peer**:
  `Inquiry Result, 1 device(s)` → `FOUND f0:a3:b2:bb:d1:00 class 0x011c00` →
  `inquiry complete`.  Reproduced twice, same peer.  [report 238 §0]
* **The reference's service pack loads completely.**  `btsload` replays
  `WL1283.bts` over H4: **315/315 commands answered**.  The LMP subversion moves
  `0x2b0f` → **`0x2b33`**, exactly what the file's header declares
  (`# Quattro LMP Subversion: 2b33`, version `TI_P6_15.51`).  [report 238 §2]
* **The pack is what makes the controller register with mgmt.**  With the pack
  loaded before the attach, `MGMT_OP_READ_INDEX_LIST` returns **one** controller
  (`13 B: … 03 00 00 01`); without it, **zero** (`11 B: … 03 00 00 00`).  Same
  attach procedure, only the pack differing — a clean negative control.
  `MGMT_OP_READ_INFO` then returns BD_ADDR `8e:e4:3c:22:61:00`, HCI version 6,
  manufacturer `0x000d`.  **This is the first time `hci0` has been in mgmt's
  index list on this unit.**  [report 238 §2]
* **The root cause of the three-session `hci0` failure is mainline's LE init
  stage, and `0x201c` is not benign.**  `0x201c` is
  `HCI_LE_Read_Supported_States` (OGF 0x08), not a TI vendor op.  A bare ROM
  answers **Command Disallowed `0x0c`** with **5 payload bytes where 9 are
  needed**, so `hci_cc_func()` logs `unexpected cc 0x201c length: 5 < 9` and
  returns `HCI_ERROR_UNSPECIFIED` → `bt_to_errno()` → `-ENOSYS` (the `-38`).
  It is the **last** step of `le_init2`, so the whole stage aborts, `hci0` never
  opens, and mgmt reports zero controllers.  The pack fixes the **length**
  (11 → 12 bytes) and that is enough.  [report 238 §0; trap 249]
* **The pack survives `HCI_Reset`.**  A probe inside the loader reads `0x201c`
  before and after a reset and both are full 15-byte events.  It is lost by a
  `BT_EN` power cycle.  [report 238 §2]
* **`HCIDEVUP` was never the issue and mgmt power was never reachable.**
  `HCIDEVUP` is `ENOSYS` as session 52 said, but the deeper fact is that a bare
  ROM never registers the controller, so `SET_POWERED` could not have worked.
  [report 238 §0]
* **Powering still fails, on a UART framing fault.**
  `MGMT_OP_SET_POWERED` → status `0x05` = `AUTH_FAILED`, which `mgmt_status()`
  maps from `-ETIMEDOUT`; dmesg shows `Frame reassembly failed (-84)`
  (`-EILSEQ`) then `Opcode 0x1003 failed: -110`.  The **same commands answer
  instantly from userspace at the same moment**.  [report 238 §4; trap 250]
* **The BT-firmware contradiction is settled.**  The reference's `WL1283.bts`
  says `Compatibility: Quattro, 6.0.15 ROM`; linux-firmware's
  `TIInit_7.2.31.bts` (and `6.2.31`) say `BL6450, WL1271/3 2.0 ROM` — a
  different, older family.  The reference's file is correct.  [report 238 §3]
* **The `wl128x-fw-4-sr/5-sr.bin` files are WLAN-core firmware**, loaded by
  `request_firmware()` in `wlcore/main.c`.  They cannot change BT.  [report 238 §3]
* `BOOTCTR_FCTRY` reads **`0x23`** (`dmesg | grep pbrpmb`), above the `0x1e`
  threshold, so `/init` did not write.  Health check only.

## Inferred (reasoning from evidence, not directly measured)

* **The `-84` is `hci_uart`'s modem-line handling, not the chip.**  Strong: the
  chip answers identical commands from userspace microseconds apart, and the
  only things `hci_uart` does that a plain tty reader does not are
  `hci_uart_set_flow_control()`'s RTS/DTR toggle and `CRTSCTS`
  (`hci_ldisc.c:386-389`) plus a `tty_unthrottle()` per chunk.  The reference
  stack carrying a dedicated "force-RTS" devctl for this pad pair supports that
  this pair is unusual here.  Not proven — no kernel-side instrument was built.
* **LE is unrecoverable on this firmware generation.**  Medium-strong: the
  controller returns `Command Disallowed` for the state read, `0x11` for
  `Write_LE_Host_Support`, and an all-zero LE feature page — three independent
  refusals — and the reference's own stack contains no LE code at all, so the
  vendor never used LE on this unit either.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"`0x201c` is a benign TI vendor op."**  Wrong (report 237 §4).  It is
  standard `HCI_LE_Read_Supported_States` and it is the whole blocker.
* **"Power the controller over mgmt and it will work."**  Unreachable on a bare
  ROM: the controller is not in mgmt's index list, so `SET_POWERED` cannot
  succeed.  The pack is a prerequisite, which nobody had established.
* **"The core is asleep, so it disallows the LE commands."**  Refuted: loading
  the pack with `0xFDA8 01` (disable Cortex Sleep) appended gives the same
  `0x0c`.  Sleep is not the gate.
* **"`Write_LE_Host_Support` must come first."**  Refuted: it returns `0x11` and
  changes nothing.
* **"The chip sleeps between the kernel's init commands."**  Refuted: `0x1002`
  then `0x1003` answers back-to-back, after 1 s, and after 2 s.
* **"Use 5-sr firmware to fix BT."**  The `wl128x-fw-*-sr.bin` files are WLAN
  firmware; the BT core's firmware is the `.bts` pack.  Different cores,
  different files.
* **Three ways my own tools lied** (each cost real time, all now in
  `rootfs/bt/README.md`):
  1. **Buffered stdout.**  A static ARM binary writing to a pipe is
     block-buffered, so a `timeout` SIGTERM discarded every line and the tool
     "looked silent".  Fix: `setvbuf(stdout, NULL, _IOLBF, 0)`.
  2. **Command Complete vs Command Status layouts differ.**  `0x0e`:
     `04 0e len num op_lo op_hi status`; `0x0f`:
     `04 0f len status num op_lo op_hi`.  Matching `ev[4]|ev[5]<<8` for both
     **drops every asynchronous reply** — which is exactly how a working inquiry
     looked like no reply.  Use a raw hexdump to settle it.
  3. **A stale `N_HCI`.**  The discipline attaches to the *tty*, so killing the
     attaching process leaves it consuming bytes; the chip then looks dead.
     `cat /proc/tty/ldiscs` and `btdetach` first.
* **A compound `ssh … 'a; b; c'` with nested quotes and redirects silently does
  not run.**  Twice a load-and-attach one-liner produced no log at all.  Push a
  small script and run that instead.

## Unknown / open questions

* **Whether the `-84` disappears when `hci_uart`'s RTS/DTR and `CRTSCTS`
  handling is taken out for this port.**  This is the next experiment and it is
  one variable.
* **Why the kernel's `0x1003` times out intermittently** — one attach reached
  `0x201c`, others died at `0x1003`.  Likely the same framing fault.
* **Whether LE can ever be enabled on this chip.**  Three independent refusals
  and no reference implementation; the honest position is "unknown, probably no".
* **Why the reference stack's `.bts` replay leaves the core asleep** after the
  script's final `0xFDA8 00`, and whether the reference re-disables it elsewhere.

## Superseded / uncertain

* **"The BT transport works but powering is the next step"** (session 52) —
  superseded: the transport working is now shown to include a *full inquiry with
  a real peer*, and the blocker is the kernel's controller open, not power.
* **"Use linux-firmware's `TIInit_7.2.31.bts`"** — settled the other way; the
  contradiction is resolved in favour of the reference's file.
* **"`hci0` exists, so it is a live controller"** — corrected: the sysfs node
  comes from `HCIUARTSETPROTO`; mgmt registration is the real milestone.

## Bench state inherited by session 54

| item | value |
|---|---|
| region 0 | **`0xDD66D8C3`**, `0x939E4C` B, kernel `7.3.3` (unchanged this session) |
| region 1 | **`49e318bf11bccba163ef990c9fd8858baff0497e696ab3d8371f56ca28ddfef1`**, 67 108 864 B, **43 modules** |
| kernel patch | **61 files / 8 762 lines**, sha256 `20556c5f…` (unchanged — no DTS/driver edit this session) |
| OS running | **our Linux image** (`0fca:8010`), console `pb> `, **ssh `169.254.0.1`** |
| `BOOTCTR_FCTRY` | **`0x23`** — `dmesg \| grep pbrpmb` |
| device-side files | `/tmp/btdump`, `/tmp/btsload`, `/tmp/btinq2`, `/tmp/btgate`, `/tmp/btdetach`, `/tmp/WL1283.bts` — all tmpfs, gone after a reboot; **nothing added to the UFS** |
| flashes this session | **none** |
| way back to TabletOS | `local/work/ifs-shim-sealed.ifs` at `0x825` **+** `local/work/rcfs-1917.rcfs` at `0x8D5` |
| restore required | **none** |

## Artefacts produced

* Report `docs/reports/238-wl1283-bt-inquiry-and-le-init-2026-11-15.md`.
* `rootfs/bt/` — five bench tools **plus a README**: `btdump` (raw hexdump; start
  here), `btsload` (the `.bts` H4 loader, `--stay-awake` / `--probe-reset`),
  `btinq2` (BR/EDR inquiry), `btgate` (LE gate walk), `btdetach` (`N_HCI` →
  `N_TTY`).  All five compile clean with `arm-linux-gcc -static -O2 -Wall`.
* Traps **249** and **250** in `KNOWN-ISSUES.md` §2.
* DECISIONS **§73**.
* `docs/contradictions/bt-firmware-tiinit-vs-wl1283.md` — **resolved**.
* `STATE.md`, `HANDOFF.md`, `ROADMAP.md`, `docs/reports/INDEX.md` updated.
* No kernel patch re-export was needed: no DTS or driver file changed.
