# 94 — Track A–E collation, Phase 0 audit, and the revised native-boot plan

Status: read-only. No device writes were made. Date: 2026-10-25.
Supersedes `85-linux-boot-plan.md` Phase 3/4 and the `region[0xd4]` statements in
`71`/`85`. Tracks: `88`–`92`. Live dumps: `playbook-live-2026-10-25/` (reference),
`playbook-live-spare-2026-10-25/` (spare).

## 0. Bottom line

The native-boot plan is **simpler and more dangerous** than `85` assumed:

- The live active IFS is **already CRC-valid** (`0x8D46FB4A`). The CRC path fails only the
  **OS-type gate**: `desc[0xD4] = 4`, and the gate requires `6`.
- The **factory/CRC path verifies region 0 (`hd1`) only**. There is no A/B fallback in
  factory mode. `85`'s "modified region 1 + CRC-valid original in region 0" design is
  impossible.
- `BOOTCTR_FCTRY` is **absent** on both working units, which is why the selector reads `0`
  and the unit boots the ECDSA/product path. Creating it is the arming step.
- A successful factory boot **decrements** the counter; reaching `0` halts. Any failure in
  the retry branch halts at `0x800094BE` (`b .`) with no OS and no writer.

Therefore the first real experiment is: **reseal the unmodified region-0 image with
`os_type = 6`, then create `BOOTCTR_FCTRY = 0x32`, reboot, and see the same TabletOS boot
under `FACTORY`.** No payload change. If it boots, the CRC path is proven and only then do
we swap in Linux.

## 1. Corrections to the prior plan

| Prior claim | Corrected |
|---|---|
| `region[0xd4] == 6` (`71` §2, `85` §0) | The byte is `desc+0xD4`. `desc = hdr+0x2C` (magic `0xC3D0E193` at `desc+0x9C` = `hdr+0xC8`), so the live byte is at **`hdr+0x100`** (media `hdN+0x100100`) and reads **4**, not 0. `hdr+0xD4` is zero and is not the field. |
| `0x803BF610` is the entry point (`85` §0) | It is **LoadEnd**. Image size = `LoadEnd - LoadStart` = `0x2BFE10`. Word 0 is `0xEA0001FE`, an ARM `b` to `image+0x800` = **`0x80100000`**, which is the effective entry. |
| Driver verifies requested then falls back (`85` §1) | Only on the **product** path (selector 0, via `0x2074`). With `BOOTCTR_FCTRY != 0`, `0x800142B0 bl 0x8000911a` / `cbz r0, 0x800142d0` sends nonzero to `0x800142BC`, which sets region 0, calls `0x80014148` once, and returns. **No `0x2074` read, no fallback.** Independently re-verified. |
| The boot stage never decrements the counter (`71` §7b) | `0x8000911E` reads, `cmp r0,#1`, `subs r0,#1`, writes via `0x800115e6`. Called on every successful factory boot. `0x800094BE` is a `b .` halt. Independently re-verified. |
| `0x2074` `flags` is never interpreted (`83`) | The bootloader read-modify-writes bit 0 (`0x80014340`/`0x8001437C`). |
| `sane()` restores counts/epoch (`85` Phase 1 restore) | `sane()` sets only `requested := actual` and `max := 0`; it does not zero the counts or epoch. |
| `/dev/rpmb` is a devctl device | It is a **directory resmgr**: each record is `/dev/rpmb/<NAME>`; read/write/unlink only. No devctl. |
| Both A/B regions must be CRC-valid before arming | Not required and not sufficient. Required: **region 0 CRC-valid with `desc[0xD4] == 6`**. Region 1 is ignored in factory mode. |
| The ECDSA path is reachable after arming | Once the counter is `1..50`, both regions are CRC-only; the ECDSA signatures are irrelevant. |

## 2. Track summaries

- **A (`88`)** — `06001A06` is signed, CRC-intact, flat router `0x8010F280`; every write is
  bounded by the MCT allow-list and gated on an authenticated `FS_DIRTY` RPMB RMW; no
  arbitrary read; `0xDE` read-only; no `BOOTCTR` setter. No path to arbitrary eMMC access.
- **B (`89`)** — host model of the boot-stage verifier and `89-crc_tool.py`. Header:
  `+0x00` branch, `+0x04` LoadStart, `+0x08` LoadEnd, `+0x20` cookie, `+0x24` LoadBase,
  `+0x28` MetricsPtr; descriptor at `+0x2C`; `desc+0x9C` magic; `desc+0xD4` OS type.
  CRC-32/ISO-HDLC over `[0, end-4)`, stored at `end-4`; ECDSA trailer at `[end-0x230, end)`.
- **C (`90`)** — `0x2074` A/B runbook; byte-level patches; boot-outcome table; `max=4` is an
  immediate 4-boot budget, not a slow risk; `actual` corruption hazard.
- **D (`91`)** — enter-OS handoff: ARM SVC, IRQ/FIQ masked, **MMU off**, VBAR not written,
  WDT2 running and kicked immediately before `0x800192A8 bx r0`; `r0` = image base
  `0x800FF800`. No per-entry logic in the loader.
- **E (`92`)** — RPMB store format and `/dev/rpmb` file interface; the MAC/nonce/write-counter
  are produced by the secure monitor; the bootloader decrements the counter and halts at 0.

## 3. Phase 0 — live read-only audit

Both units are the same build (`Nov 15 2011`, `os_version 0x02001305`, `hwid 0x06001A06`)
and the active IFS is **byte-identical** (`sha256 75be24e8…`, size `0x2BFE10`).

| Item | Reference 64 GB | Spare 64 GB |
|---|---|---|
| PIN (`0x2002`) | `0x50175127` | `0x50176EC1` |
| `0x2801` A / B | 0 / 10 | 0 / 10 |
| `0x2801` iterations | 5060 | 5810 |
| password `0000` verified | yes (`84`) | **yes** (digest reproduced exactly) |
| `0x2004` | absent | absent |
| `0x2040` | absent | **present = 2** |
| `0x2074` | V3, req=act=0, count=`0x017C`, max=0 | V3, req=act=0, count=3, max=0 |
| `0x2802` | mostly zero | mostly zero |
| `BOOTCTR_FCTRY` | absent | absent |
| `desc[0xD4]` | 4 | 4 |
| CRC trailer | valid `0x8D46FB4A` | valid `0x8D46FB4A` |
| `hd2` IFS | zero | zero |

`0x2040 = 2` on the spare takes the `tbb` case 2 -> `0x80014998` gate, the same decision as
an absent record; it does not change anything.

Block devices on the spare: `hd0`–`hd6`, plus exactly two per-region partition nodes,
`hd1t179`/`hd2t179`, each a type-`0xb3` (t179) partition at LBA `0x6000` (`0xC00000`),
`0xFA000` sectors, end `0x20000000` — the **RFS** partition inside each 512 MB region. They
do not expose the IFS; the IFS is the raw region at `/dev/hd1`/`/dev/hd2` + `0x100000`.

`/dev/hd0` sector 0 is the MLO header (`0x4000`, `0x4D70`, `"MLO"`), not an MBR.

## 4. Revised plan

### Phase 1 — prove the CRC gate with the unmodified payload (first destructive step)

Preconditions: spare unit, both backups off-device, `0x2801 A=0/B=10`, `BOOTCTR_FCTRY`
absent, image CRC verified.

1. Host: `89-crc_tool.py reseal ifs0.img ifs0.sealed.img --type 6`.
   Expected: `os_type: image offset 0x100 4 -> 6`, new CRC trailer.
2. Host: `89-crc_tool.py verify ifs0.sealed.img` -> `RESULT: VERIFIED (CRC path)`.
3. Backup: `hd1` sectors 2048 (`0x100000`) and `0x1DFF` (`0x3BFE00`), plus the full image.
4. Device: write the two patched sectors to `/dev/hd1` (`seek=2048` and `seek=0x1DFF`,
   `conv=notrunc`); read them back; verify the readback host-side.
5. Rehearse the RPMB write path on a sacrificial `TEST_PAD` record (create, read back,
   delete).
6. Device: create `BOOTCTR_FCTRY` = `32 00 00 00` with
   `dd ... seek=0 conv=notrunc`; read back exactly 4 bytes.
7. Reboot. Expect `BOOT_MODE: FACTORY` and the same TabletOS on the CRC path; the counter
   becomes `0x31`.
8. Before any later boot, top the counter back up from the booted OS (it has `/dev/rpmb`).

Order is mandatory: the CRC covers the header, and the ECDSA signature is invalid after the
gate byte changes, so the selector must not be allowed to fall back to 0 between steps 4 and
6.

### Phase 2 — Linux payload

Same reseal procedure with `--patch`/a rebuilt payload. Keep the header, `+0xD4=6`,
`end-8 = 0xD7C82D1F`, and a valid CRC trailer. `91` gives the chainloader environment:
ARM state, SVC, IRQ/FIQ masked, MMU off, WDT2 service, `r0 = 0`, `r1 = ~0`, `r2 = DTB`.

### Phase 3 — fallback

kexec from the rooted 0.4869 OS remains the lower-risk path if the CRC payload stalls.

## 5. Risk / rollback

- **One-way door:** if the sealed image fails the gate or CRC, the unit halts at
  `0x800094BE`; no OS, no `/dev/rpmb`, no pre-OS writer. Recovery needs eMMC access.
  Mitigation: the CRC and structure are verified with the bootloader's own algorithm before
  the write, and the payload is unchanged.
- **Rollback while the OS still boots:** `rm /dev/rpmb/BOOTCTR_FCTRY` restores selector 0
  (product/ECDSA), and the two sectors can be restored from the backup.
- **Budget:** `0x32` = 50 successful factory boots. Each success decrements; top up from the
  booted OS.
- **Do not touch region 1**; the factory path ignores it.
- **Do not arm the counter before the image is sealed and read back.**

## 6. Open items

1. Confirm the boot-stage CRC covers exactly `[0, end-4)` on the sealed image after the
   gate-byte change (host tool says yes; the live unmodified image already matches).
2. Confirm `dd conv=notrunc` does not `O_TRUNC` a new `/dev/rpmb` record (rehearse on
   `TEST_PAD`).
3. Whether the OS notices `os_type = 6` after boot (expected: no; only the bootloader reads
   it).
4. Region 1 stays empty; if the counter ever returns to 0, the product path returns and
   `0x2074`/ECDSA come back into play.
