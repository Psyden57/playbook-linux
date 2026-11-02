# Session 19 (absolute) — the RPMB key is pinned, the record block is sealed, and the monitor rejects Linux

> Numbering: absolute **19**; the reports call it Linux-era **"session 13"**
> (`NN-6`).  Reports: **143** (the key and the `/proc/pid/as` reset), **144**
> (the sealed record store), **145** (the monitor call: bias, then `0xFF05`),
> **146** (static RE of every smc call site).

Date: 2026-10-30.
Unit state at the end: **TabletOS** (QNX 6.6) from the sealed IFS, door/SSH up,
`0fca:8010`; **sealed IFS** sha256 `c72b0530…`, `os_type` 6, CRC `0x116B2562`
(recomputed this session), `end-8 = 0xD7C82D1F`; **`BOOTCTR_FCTRY = 0x32`**;
`accounts/devuser/startup.sh` sha256 `c265cfbb…`; **region-1 Linux root
present** (`0b87c29f…`, left in place); **no kernel image on media** (region 0
is the sealed IFS).  The last session-19 image, `boot/chainloader/kernel.img`
CRC `0x96FC5CA9`, is saved as `local/images/kernel-0x96FC5CA9-pbmon12.img`.

## Goal of this session

The handoff's first task: pin the RPMB derivation `f` (the frame key), then make
the Linux `BOOTCTR_FCTRY` top-up (one verified `0x32` write), and then move to a
real userland and the hd3 cut-over.

## Confirmed (measured, read in a file, or seen on the bench)

* **`f` is pinned.** The 32-byte frame key in the live `rpmb-omap4` page at
  `page+0x460` is `dfeec0ce392899c274e12a01c8b33d6ff0eef5851a6f9a0bb24afd0a1c5e2274`;
  HMAC-SHA256 of it over the captured 284-byte MAC input equals the device MAC
  `fc395840…5645f568`.  [report 143; `docs/reports/data/rpmb-probe-0xDDB9D733-2026-10-30.txt`]
* **The block key is `seed[0:16]`.** `150000014d43474146411000462801b4`;
  HMAC-SHA256 of it over the reversed block-0 first 224 bytes equals the stored
  block HMAC.  [report 143 §3.2]
* **The live seed is CID-derived, not the literal.** `page+0x440` holds the
  20-byte CID seed (`MCGAFA` at `+0x444`) with the literal's tail `"ing in the
  o"` left at `+0x454`.  [report 143 §2]
* **How the key was read without `/proc/<pid>/as`:** `DCMD_PROC_MAPINFO` finds
  the RpmbInit page (flags `0x1090b02`), `DCMD_PROC_PTINFO` gives its physical
  address (this session: vaddr `0x78585000` → paddr `0xbfb13000`), and
  `memdump3` reads it.  Tool: `tools/qnx-procmem.c` → `pbmem`.  [report 143 §2;
  the live paddr changed each boot]
* **The record block is encrypted on media by the secure monitor.** All 512
  blocks authenticate (frame MAC) and **none** contains the record magic or
  `BOOTCTR_FCTRY` in either byte view; the resmgr unseals with `smc` `0x2F`
  (op 0, dir 0) in `0x2aa4` and seals with dir 1 in `0x2bf0`; both HMACs cover
  the ciphertext.  [report 144; image `0x9CE309CF`]
* **A known plaintext/ciphertext pair exists for block 0x43.** The live page
  buffer `page+0x700` holds the plaintext (`bgroupcrt_dat8`, `fsecsyskey`,
  `drmclock.cfg`); the media block 0x43 is different.  No standard AES
  mode/key tried reproduces it.  [report 144 §3/§4;
  `docs/reports/data/rpmb-seal-block0x43-2026-10-30.txt`]
* **The monitor addresses DRAM `0x10000000` below the non-secure physical
  address.** Live: page true `0xbfb13000` (PTINFO) but the descriptor given to
  the monitor says `0xafb13000`; the frame buffer true `0xbfb8e0e4` is passed as
  `0xafb8e0e4`.  The two ranges are **not** aliases — but the comparison was the
  **first `0x20` bytes** of the page and the first `0x10` of the frame, not a
  full-page compare.  [report 145 §2]
* **With the bias, the smc call returns instead of hanging.**  Because the call
  runs from userspace (`/dev/pbseal`, `/dev/pbunseal`), a hang costs one reboot
  of the same image, not a flash.  [report 145]
* **Every service returns `0xFF05` (65285).** `0x12` (nonce fill; only the first
  4 bytes of the *true* output buffer were checked and were unchanged — the
  biased address was not read back, so "refused before touching memory" is not
  proven), `0x31` (KEK state), `0x33` (key load).  The veneer, CM2 L4SEC setup
  and command-page/descriptor layout match QNX and the bootloader, **except** the
  second PRCM write: session 19's driver wrote `0x4A004004` instead of
  `CM_MPU_STATICDEP` `0x4A004304` bit 14 (`L4SEC_STATDEP`), so the MPU→L4SEC
  dependency was never enabled.  [reports 145 §2/§3, 107 §4; TRM Table 3-1034]
* **There is no registration/init smc anywhere.** `rpmb-omap4` and
  `trustzone-omap4` have one veneer each; `procnto` and `devb-mmcsd` have zero;
  the bootloader's `r12=0x100/0x102/0x109/0x112` calls are the TRM §27.5 ROM
  PL310 L2 services.  The domain `r2` is not the cause (bootloader 5/0, QNX 4);
  `0x2F` op=1 is KEK provisioning only; `devb … crypto=enable` is an `fs-qnx6`
  mount option, not the driver.  [report 146, 38 proven claims]
* **Bench state re-verified at the end:** IFS `c72b0530…` (CRC `0x116B2562`
  recomputed and equal), `BOOTCTR_FCTRY = 32 00 00 00`, `startup.sh`
  `c265cfbb…`, region-1 root `0b87c29f…`, TabletOS `QNX localhost 6.6.0`
  with the door up.  [measured this session]
* **The "run `pb_bootlog.sh` after every Linux boot" instruction was removed**
  from `DEVELOPMENT.md` and `HANDOFF.md` at the operator's request; it is now
  on demand only.  [this session]

## Inferred (reasoning from evidence, not directly measured)

* **The monitor's DRAM translation is a fixed `-0x10000000` for the top band.**
  Two independent buffers match it; whether it covers our buffer's 1 MiB section
  is inferred, not proven.  [report 145]
* **`0xFF05` is a refusal tied to the command-page physical address (`r3`).**
  `r3` is the only argument constant across all six failing services that is
  also an address; the pre-bias attempt *hung* (so it is dereferenced), and the
  `0x12` output buffer is untouched (so the refusal precedes payload DMA).
  Strength: moderate.  [report 146]
* **The cipher is deterministic and survives reboots** (the media is persistent
  and QNX decrypts after every boot), so the block-0x43 pair taken on one boot
  is valid for another.  [report 144; assumed, not re-measured]

## Dead ends and wrong assumptions (do not repeat)

* **"A `lseek()`+`read()` on `/proc/<pid>/as` is the safe, documented way to
  read the page."**  It is documented, and it faulted the procfs server
  ("Server fault on msg pass") and reset the unit (unplanned reboot, one factory
  boot spent).  Killed by that reset.  Trap 59.  [report 143 §1]
* **"The RPMB key is the 32-byte literal."**  The live page shows the
  **CID-derived** seed, with only the literal's last 12 bytes left over; the
  frame key is `f(seed)`, not the literal.  [report 143 §2]
* **"The block HMAC is over the wire view; recompute it over `F[0xE4..0x1C4)`."**
  It is over the **reversed (logical)** 224 bytes.  Killed by the exact
  block-0 HMAC match on the reversed view and failure on the wire view.  Trap
  60.  [report 143 §3.2]
* **"Patch the received block, re-MAC, write one `0x32`."**  The block is
  sealed; a plaintext patch would decrypt to garbage and a wrong value is the
  brick path.  Killed by the block-0x43 plaintext/ciphertext pair.  Report 144,
  trap 61; `pbrpmb write` is compiled blocked.
* **"The monitor call hangs because of the veneer/registers (r6 tag, r12,
  ICIALLU, r4-r12 save)."**  All of those match QNX and the bootloader; the hang
  was the **DRAM bias**.  Killed by adding `phys - 0x10000000`.  [report 145]
* **"QNX's second PRCM (CM1) write is at `0x4A004000 + 4`."**  Wrong base: QNX
  maps `0x4A004300` (`rpmb-omap4` 0x2254) and sets `CM_MPU_STATICDEP` at
  **`0x4A004304`** bit 14 (`L4SEC_STATDEP`, TRM Table 3-1034; report 107 §4).
  Session 19's driver wrote `0x4A004004` — an undefined
  `INTRCONN_SOCKET_CM1` offset — so the MPU→L4SEC dependency was never enabled,
  and `0x4A004304` was never read back.  Killed by re-reading the `mmap`
  arguments and report 107.  [session 20 correction]
* **"The monitor requires a secure-world OS registration/handshake."**  The RE
  found none: no smc in `procnto`/`devb`, one veneer each in `rpmb-omap4` and
  `trustzone-omap4`, and `trustzone-omap4` calls the monitor only from its
  devctl handler.  [report 146 Q1]
* **"The domain `r2=4` is required."**  The bootloader uses `r2=5` (and `0` for
  `0x12`); since `0x12` is called with 0 and 4 and `0x31` with 5 and 4, a fixed
  domain cannot be required.  [report 146 Q3]
* **"`0x2F` op=1 (the key derivation) must run before `0x33`/`0x2F`."**  `op=1`
  appears only in the KEK-provisioning routine; the seal path never issues it.
  [report 146 Q4]
* **"`devb-mmcsd … crypto=enable` is the block seal."**  That is `fs-qnx6`'s
  mount option; `devb-mmcsd` has no smc, AES or HMAC and does RPMB with raw MMC
  commands.  [report 146 Q5]
* **"`0xFF05` is a bad argument to `0x33`."**  `0x12` (a trivial `{2,phys,16}`)
  and `0x31` (`{1,1}`) fail identically; it is a global refusal.  [report 145]
* **"The selftest can run at driver probe."**  That image (`0xFCCE507A`)
  reset-looped until the sealed IFS was restored, spending counter units.  The
  test must be userspace-triggered.  [report 145 §2, §5]

## Unknown / open questions

* **What `0x4A004304` (`CM_MPU_STATICDEP`) reads, and whether bit 14
  (`L4SEC_STATDEP`) is the missing setup.**  It was never read back (TRM reset
  value 0, bit 14 disabled).  Settle by reading it on the unit and setting bit 14
  before the next `0x33`/`0x2F`; the running session has already corrected the
  driver's address.
* **The exact monitor address translation, and whether `r3` is the cause.**
  Settle with the `0x12` nonce-fill as an address oracle: try the biased
  `0xaf…`, the true `0xbf…`, and a buffer deliberately allocated **below
  `0xB0000000`** with an identity address; only the candidate that returns 0 and
  fills the buffer is right.  A wrong candidate can hang (one reboot) and could
  corrupt 16 bytes elsewhere.
* **What `0xFF05` means.**  It is not in the TRM or any binary string; it is
  monitor-internal.  Settle with the monitor image (we do not have it) or with
  the address oracle.
* **Whether this monitor serves the `0x12/0x31/0x33/0x2F` family to any
  non-QNX caller at all.**  If the oracle shows no address works, the answer is
  probably no, and QNX's `/dev/rpmb` stays the only writer.
* **The bootloader's actual `r3` values** (its command pages are in the first
  MiB at a 1:1 mapping) — these would derive the translation rule directly.
  [report 146, "What to try next"]
* **Whether the boot path's MLO stage matters.**  Its `0x38–0x3C` wrappers have
  no caller in the image; report 146 suggests checking whether our chainloader
  bypasses the stage that would issue them.
* **Whether a wrong-address seal can corrupt memory.**  It writes 224 bytes
  where told; the address oracle's 16-byte writes are the safer probe.

## Superseded / uncertain

* **Report 141's "the Linux top-up is feasible"** is qualified by **144**: the
  key work and read path stand, but the record block is sealed, so report 141's
  plaintext-patch recipe is invalid.  Reports 143–146 are current.
* **Report 141 §7.1 step 3** ("recompute the block HMAC over the wire view
  `F[0xE4..0x1C4)`") is wrong; the logical (reversed) view is current.  [143]
* **Report 141 §3.2's "page+0x20 is zeros"** is contradicted by the live page,
  which holds the `.rodata 0x5A94` constant `86787c2a…` at `+0x20` and
  `443ac202…` at `+0x40`.  Who writes them remains **[U]** (the monitor? a path
  not yet decoded).  Report 92 §2.4 said the `.rodata` constants are handed to
  the services; report 141 said they are dead.  The measured page supports
  report 92's *contents*, not its *deadness* reading of 141.  [143 §2, 146]
* **Report 92's "0x5a84/0x5a94 are dead data"** — see above; the live page has
  those bytes at `+0x20`/`+0x40`, so at least the contents are real.
* **The report-141 §3.5 claim that the `0x2F` op=1 output goes to `page+0x460`**
  — report 141 §10 corrected the output to 16 bytes at `page+0x000`; the
  32-byte frame key at `+0x460` is set separately and is the one that verifies.
  [143 §3.3]

## Artefacts produced

* **Reports**: `143` (key pinned; `/proc/pid/as` reset), `144` (sealed record
  store; known pair), `145` (monitor call: bias, `0xFF05`), `146` (static RE of
  every smc call site).  Data: `docs/reports/data/rpmb-page-0xbfb2b000-2026-10-30.txt`,
  `docs/reports/data/rpmb-seal-block0x43-2026-10-30.txt`.
* **Tools**: `tools/qnx-procmem.c` (`pbmem <pid> map|page|pt <vaddr>`, metadata
  only); `initramfs/rpmb.c` gains `scan`/`dump`/`sealtest`, `write` compiled
  blocked; `drivers/misc/pbmon.c` + `pbmon-pair.h` (`0x33`/`0x2F`, CM2 clocks,
  `/dev/pbseal`, `/dev/pbunseal`, opt-in selftest), copies in `kernel/source/`.
* **Kernel patch**: `kernel/patches/0001-playbook-winchester-6.15.11.patch`
  re-exported (4629 lines / 34 files, applies cleanly to pristine 6.15.11).
* **Images** (gitignored; the earlier ones are overwritten, only the last was
  saved): `0x87FD6EED` (write path), `0x9CE309CF` (`scan`/`dump` diagnostics),
  `0xFCCE507A` (**auto selftest — do not use**), `0x65599D90` (opt-in),
  `0xC462AE22` (bias), `0x771355C0` (address diagnostics), `0x94640F82` (L4SEC
  domain wake-up), `0x853C5323` (`0x31` probe), **`0x96FC5CA9`** (`0x12` probe;
  saved as `local/images/kernel-0x96FC5CA9-pbmon12.img`).
* **Boot logs**: `local/logs/boot-20261030-101031.log` (`0x87FD6EED`),
  `…101619.log` (`0x9CE309CF`), `…110840.log` (`0xC462AE22`).
* **Docs**: `STATE.md`, `HANDOFF.md`, `KNOWN-ISSUES.md` (traps 59–61),
  `DEVELOPMENT.md` and `HANDOFF.md` bootlog wording, `DECISIONS.md` §20,
  `ROADMAP.md` §6, `docs/contradictions/rpmb-key-kek-vs-fallback-literal.md`
  and this note.
