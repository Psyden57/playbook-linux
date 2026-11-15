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
* **The remaining blocker is a stray `0x30` byte, and it is now located.**  A
  temporary instrument in `h4_recv()` (the error path prints no data — the
  report-232 lesson) shows the same shape in **6 of 6** runs: the last init reply
  arrives complete and correct, then ~200 ms later a **single `0x30`** arrives on
  its own; `0x30` is not an H4 type, so `h4_recv_buf()` correctly returns
  `-EILSEQ` and `hci0`'s open fails with `0x1003 failed: -110`.
  `SET_POWERED` → `0x05` (`-ETIMEDOUT`) follows from that.  **Whether the chip or
  the host emits the `0x30` is not yet known** — it never appears when the same
  sequence is driven from userspace.  [report 238 §4; traps 250/251]
* **The BT-firmware contradiction is settled.**  The reference's `WL1283.bts`
  says `Compatibility: Quattro, 6.0.15 ROM`; linux-firmware's
  `TIInit_7.2.31.bts` (and `6.2.31`) say `BL6450, WL1271/3 2.0 ROM` — a
  different, older family.  The reference's file is correct.  [report 238 §3]
* **The `wl128x-fw-4-sr/5-sr.bin` files are WLAN-core firmware**, loaded by
  `request_firmware()` in `wlcore/main.c`.  They cannot change BT.  [report 238 §3]
* `BOOTCTR_FCTRY` reads **`0x23`** (`dmesg | grep pbrpmb`), above the `0x1e`
  threshold, so `/init` did not write.  Health check only.

## Inferred (reasoning from evidence, not directly measured)

* **The `-84` is a stray `0x30` byte, origin not yet known.**  Strong on the
  byte and the timing (instrumented `h4_recv()`, 6 of 6 runs, ~200 ms after the
  last complete reply); **open** on which side emits it, because it never appears
  from userspace.  It is not console traffic (UART2 is in no `/proc/consoles`)
  and not flow control (A/B identical on a clean boot).
* **LE is refused by the controller, but that is not proof the silicon cannot do
  it.**  Medium: the controller returns `Command Disallowed` for the state read,
  `0x11` for `Write_LE_Host_Support`, and an all-zero LE feature page — three
  independent refusals, so some enable step is missing.  But the chip reports
  **HCI 4.0 / LMP 4.0** and its LMP feature page sets the LE-supported bit, and
  the reference OS being BR/EDR-only reflects BlackBerry never shipping an LE
  stack on a 2011 tablet — **not** a hardware limitation (the operator's point).
  So: no working sequence is known; impossibility is **not** demonstrated.

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
* **"`hci_uart_set_flow_control()` is the framing fault" — my own wrong guess,
  retracted.**  H4 never calls it (only the vendor protocols do) and
  `hci_uart_setup()` is a no-op for H4.  Worse, an apparent clean A/B win for
  `CRTSCTS` was measured while an *instrumented module was still loaded*; on a
  clean boot the result is identical with flow control **on and off**.  I
  presented that as a result before re-checking it.
* **"The `Read_Local_Name` reply is corrupt." — also mine, also retracted.**
  The reply is 255 bytes and arrives complete and correctly framed.  What looked
  like corruption was **my probe's own 240-byte display buffer**.  Lesson: the
  diagnostic can manufacture the fault it reports.
* **`insmod` "succeeding" while keeping the old module** — `btbcm` holds a
  reference to `hci_uart`, so the swap silently does nothing and the instrument
  never fires.  Verify a module swap by content, not by exit status.  (Trap 251.)
* **After a cold boot `gpio535` is not exported and reads low**, so a pack load
  fails with "no HCI_Reset answer" — identical to a dead chip.  Export it, set
  direction, pulse low 2 s.  (Trap 252.)
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
  not run.**  Three separate times a load-and-attach one-liner produced no log at
  all.  Push a small script and run that instead.
* **A subagent left running with no time-box.**  One ran 87 minutes with no
  result and had to be killed; the analysis it was asked for took a few minutes
  when done directly.  Time-box them.

## Unknown / open questions

* **Which side emits the stray `0x30`** — the chip, or something host-side ~200 ms
  after init settles.  It never appears when the same sequence is driven from
  userspace, which favours a host origin, but that is not the same as knowing.
* **Why the kernel's `0x1003` times out** when `0x1003` alone answers instantly
  from userspace — almost certainly a consequence of the `-84` above, since the
  H4 receiver is left in an error state.
* **Whether LE can be enabled on this chip at all.**  Three independent refusals,
  no reference implementation, but the silicon reports BT 4.0 and the
  LE-supported feature bit.  Unknown, and **not** shown to be impossible.
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
| `BOOTCTR_FCTRY` | **`0x22`** — `dmesg \| grep pbrpmb`; was `0x23`, one unit spent by the one reboot this session (for a clean module state) |
| device-side files | `/tmp/pbhci`, `/tmp/btsload`, `/tmp/btdump`, `/tmp/mgmtdiag`, `/tmp/WL1283.bts` — all tmpfs, gone after a reboot; **nothing added to the UFS** |
| flashes this session | **none** (a `hci_uart.ko` rebuild was loaded and reverted on the device; the rootfs copy is untouched) |
| kernel tree | `hci_h4.c` instrument **reverted** and proven (rebuilt module has no `PBTMP`; file matches the pristine backup) |
| dormant fix | `rootfs/hci.c` gained `--no-cts` / CRTSCTS-on-attach; it is **not** evidence of a fix (flow control proved irrelevant) but keeps the A/B available |
| way back to TabletOS | `local/work/ifs-shim-sealed.ifs` at `0x825` **+** `local/work/rcfs-1917.rcfs` at `0x8D5` |
| restore required | **none** |

## Artefacts produced

* Report `docs/reports/238-wl1283-bt-inquiry-and-le-init-2026-11-15.md`.
* `rootfs/bt/` — five bench tools **plus a README**: `btdump` (raw hexdump; start
  here), `btsload` (the `.bts` H4 loader, `--stay-awake` / `--probe-reset`),
  `btinq2` (BR/EDR inquiry), `btgate` (LE gate walk), `btdetach` (`N_HCI` →
  `N_TTY`).  All five compile clean with `arm-linux-gcc -static -O2 -Wall`.
* Traps **249–252** in `KNOWN-ISSUES.md` §2 (249 `0x201c`; 250 the `0x30` byte,
  with the flow-control and display-buffer retractions; 251 `insmod` keeping the
  old module; 252 cold-boot `gpio535`).
* DECISIONS **§73**.
* `docs/contradictions/bt-firmware-tiinit-vs-wl1283.md` — **resolved**.
* `STATE.md`, `HANDOFF.md`, `ROADMAP.md`, `docs/reports/INDEX.md` updated.
* No kernel patch re-export was needed: no DTS or driver file changed.
