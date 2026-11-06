# Session 26 (absolute) — the factory top-up: load base, state wipe, per-unit keys

> Numbering: absolute `26`; the reports call this Linux-era **session 20**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-11-04
Unit state at the end: **PlayBook OS 2.1.0.1917 running from the 1917 IFS
(`local/work/ifs-shim-sealed.ifs`, sha256 `b52195bbf85b7618f9860ad73aebbae65932443b63335f57053020402b771861`),
`/dev/hd1t179 on /base`, `BOOTCTR_FCTRY = 0x32`** (it drained to `0x28` and
`0x2F` during reset loops; the TabletOS boot refilled it).  Door up.

## Goal of this session

Restore the working 2.1.0.1917 set from the ramloader, then resume
`ROADMAP.md` item 6.  The operator proposed three routes past the unreproduced
`0x2F` transform: (a) call the bootloader's own RPMB writer from the
chainloader, (b) decompile it and implement it, and (c) re-open the Linux
`pbmon` seal with the bootloader's exact sequence.  "Done" was a real answer on
those routes, and the unit left restored.

## Confirmed (measured, read in a file, or seen on the bench)

* **The session-start restore worked.**  `bbusb.py flash --signed
  local/work/back-1917.signed` from the boot ROM wrote 67,498 frames,
  `COMPLETE`, and the unit booted 2.1.0.1917 with the counter at `0x32`
  (report 173 §6; the flasher log read this session).
* **The arm9000 section is loaded at `0x80000000` from media file `0x9000`**,
  so every *code* address in reports 150/151 is `0x9000` too high (data
  literals are correct).  Evidence: the `BL_PROBE=1` chainloader dump matched
  media `0x12000` at DRAM `0x80009000` and media `0x12078` at `0x80009078`;
  the report addresses read `0xFFFFFFFF`; media first 1 MiB FNV-1a64
  `21c38ed917e0b38f` = host (`blk sumb 0 1048576`).  Report 173 §1–§2.
* **The "wipe-table anomaly" of report 151 §Q5 is an artefact of that base
  error.**  At media `0x1D31C` (= DRAM `0x8001431C`) the table holds valid
  ranges (`0x800267C0` len `0x2B004`, `0x80025E78` len `0x948`).  Report 173 §3.
* **The tail wipes its own RPMB state before the OS jump.**  The wipe covers
  `[0x80025E78, 0x800517C4)`, which includes the session flag `0x80025F00`,
  store state `0x80025FB0`, catalogue `0x8002C760` and staging `0x80033F80`.
  The chainloader selector then returns `0` ("absent").  Report 173 §3/§5.
* **The tail disables the CM2 L4SEC crypto clocks** (`0x8000F184`); the
  chainloader re-enabled them and `0x8000F108` returned `1`.  Report 173 §4/§5.
* **Calling the bootloader's selector from the chainloader works** (returned
  `0`); **calling its mount (`0x800082DC`) hangs and reset-loops** the unit
  (mode-3 probe, CRC `0x984267D3`).  Report 173 §5.
* **The compiled RPMB test keys were the spare's.**  `initramfs/rpmb.c`'s
  `rpmb_key` was `dfeec0ce…2274` and its `block_key`/`lit_seed` seed ended
  `46 28 01 b4`.  The **reference's** keys are the frame key
  `1611b3bb0eedaacbe0c155b1a545cccd34b3f1ad78f29a375cf0c468f79fd922` and the
  seed `…2be11da8` (report 156; `page+0x460`/`page+0x440`).  The reference
  frame key reproduces this unit's **live** frame MAC offline
  (`HMAC(key, msg284) = bcc563ed…ad34`); after the fix `probe` prints
  `KEY PINNED` and `blockscan` reports **512/512 blocks read/MAC-ok** (was
  **0/512**).  Report 174 §0/§1.
* **The passphrase and `0x33` key are in the bootloader.**  Media `0x1D73E`:
  `"retreating is advancing in the other direction"`; immediately after it,
  the `0x33` key `86 78 7c 2a 63 2f be 16 48 ec ab a6 d8 84 01 09`.
  Report 174 §2.
* **The block transform is two-stage.**  Unseal (`0x80008048`): `0x2E`
  (decrypt `frame+0xE4`, key `page+0x20`, IV `frame+0x10`, ivlen 32) → compare
  `frame+0x10` with `frame+0x1C4` (32-byte tag) → `0x2F` (key **`page+0x60`**).
  Seal is the reverse.  `pbmon` issues only `0x2F`, preloaded with
  `page+0x20` — the wrong key for that stage.  Report 174 §3.
* **The domain is not the variable.**  `pbmon`'s new `dom` parameter at 5 and 4
  both gave `512/512` MAC-ok and **0 hits** (`blockscan`).  Report 174 §3.
* **The unit was restored twice and the counter recovered.**  The mode-3 and
  wrong-IFS reset loops spent ~13 units (`0x32 -> 0x28`/`0x2F`); the 1917 boot
  logged `bootctr: wrote 0x32, readback ok (was '(')` and `(was '/')`.
* **`pkexec chmod 666 /dev/ttyACM0` is the documented console step**; the
  operator confirmed `pkexec` is only discouraged for the flashing tools.

## Inferred (reasoning from evidence, not directly measured)

* **The mount hang is the MMC re-init.**  The mount re-opens the MMC from the
  wiped slot (`0x8000420C`) and then walks the store; the bootloader's
  controller state has been torn down.  Strong reading of the mode-3 reset
  loop; the exact spin was not captured.
* **The reference page capture is this unit's.**  The frame key matches this
  unit's live frame MAC exactly, and `page+0x440[0:16]` matches report 156's
  reference seed.  High confidence.
* **The passphrase feeds the seed/`0x33` key somehow.**  The full string is
  stored at media `0x1D73E` and `page+0x440[16:32]` contains
  `3e 00 00 00 "ing in the o"`, so the 32-byte seed is not just the CID bytes
  (which is why `cid_seed32`/`try_seed` never matched).  Mechanism unresolved.
* **`page+0x60` may be zero for the read path only.**  The read capture shows
  zeros; the seal may populate it before the `0x2F` stage.  Unverified.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"Call `record_write`/`provision` from the chainloader."**  The tail wipe
  removes the catalogue, so the selector returns `0` (report 173 §3/§5).
* **"Rebuild the catalogue with the bootloader's mount from the chainloader."**
  It hung and reset-looped the unit (mode-3, CRC `0x984267D3`), costing ~10
  counter units.  Do not boot it again.
* **"Restore `local/images/ifs0.sealed.bin`."**  It is the 2.0.0.4869 IFS, not
  compatible with the 1917 RCFS; the unit reset-looped.  The right asset is
  `local/work/ifs-shim-sealed.ifs` (1917 + `rfs_validator` shim).
* **"`pgrep -af door-hold` is a safe liveness check."**  It matched its own
  command line (trap 32), so no holder was started.  Start it with
  `setsid nohup tools/door-hold.sh … &` and check the `BerryShell` process.
* **"Domain 5 is the missing variable."**  Both domains gave 0 hits; the
  `0x2E` stage and the `0x2F` key are the real gap (report 174 §3).
* **"`blockscan` with the pinned key is a valid gate."**  It reported
  `0/512 read/MAC-ok` because the compiled key was the spare's.  The live frame
  MAC offline match identified the reference key (report 174 §0).
* **"`cid_seed32` derives the frame key."**  `try_seed`'s identity/HMAC/SHA
  variants of the CID seed and the `lit_seed` passphrase prefix all failed;
  the frame key is a stored per-unit value, and the seed's upper half is not
  zero (report 174 §1).

## Unknown / open questions

* **Why exactly the mount hangs** (which loop in `0x8000420C`/the catalogue
  walk).  Settle by capturing the spin (DRAM ring/WDT state) or static-tracing
  the MMC open from a cold state.
* **How the passphrase derives the seed / `0x33` key.**  Settle by RE of the
  bootloader code around media `0x1D73E` and the `0x2F00`-area initialisation.
* **What `page+0x60` holds during the seal** (the read capture is zero).
  Settle by capturing the page on a *seal* path, or by the seal test.
* **Whether the two-stage `0x2E`+`0x2F` reproduces the media block on this
  unit.**  Settle by implementing the two-stage `pb_xform` and re-running
  `blockscan` (report 174 §5).
* **The exact `0x2E` descriptor/IV semantics for the seal** (the decode is
  strong, but the seal's IV comes from the `0x2F` output).

## Superseded / uncertain

* **Report 151 §Q5's "wipe table is inconsistent"** is withdrawn: at the
  corrected offset it is valid (report 173 §3).
* **Reports 150/151's code addresses** are to be read with `-0x9000`; the
  analysis of the code stands (report 173 §2, trap 82, correction 3.18).
* **The compiled RPMB keys (`dfee…`, `…462801b4`)** are the spare's and are
  superseded by the reference's (report 174).
* **The earlier "bootloaders live on boot0" reading** is not supported here:
  the supplied boot0 dump is an MCT header plus zeros (523,899 of 524,288
  bytes `0x00`, opening with `0x92BE564A`); the executing copy matches the
  user-area capture (report 173 §1).

## Artefacts produced

* `docs/reports/173-bootloader-load-base-and-rpmb-state-wipe-2026-11-04.md`.
* `docs/reports/174-rpmb-per-unit-keys-and-two-stage-seal-2026-11-04.md`.
* `boot/chainloader/chainloader_kernel.S.in`: build-time `BL_PROBE` (1 = dump,
  2 = selector, 3 = mount+selector; default 0).
* `tools/build_kernel_image.py`: `BL_PROBE=<n>` substitution.
* `local/kernel-tree/drivers/misc/pbmon.c` + `kernel/source/pbmon.c`:
  `pb_dom` module parameter (default 5).
* `initramfs/rpmb.c`: reference frame key and block seed (were the spare's).
* Probe images: mode 1 `0xBD8966DD`, mode 2 `0xA051A23C`, mode 3 `0x984267D3`
  (do not boot); domain build `0x0F769663`; reference-key build `0xC5356E38`;
  the default (probe-off) rebuild `0x0A52B4B9`.
* Restored media: 1917 IFS sha256 `b52195bb…` at block `0x825`; counter `0x32`.
