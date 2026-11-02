# 95 — CRC/factory path proven on the spare (2026-10-25)

Status: **destructive test executed on the spare 64 GB unit only** and it succeeded.
The connected reference was not touched. This closes the "is the CRC path reachable and
does a CRC image boot?" question and validates `89`/`92` over `85` Phase 3/4.

## 0. Result

The bootloader took the **factory/CRC** path, verified region 0 with
`desc[0xD4] == 6` and a valid CRC-32/ISO-HDLC trailer, booted the same TabletOS, and
**decremented `BOOTCTR_FCTRY` from `0x32` to `0x31`**. The OS came back with root in ~80 s.
No A/B fallback was involved (none exists on the factory path).

## 1. Preconditions (all verified read-only)

| Item | Value |
|---|---|
| Unit | spare 64 GB, PIN `0x50176EC1`, BSN `1063043093` (reference is `0x50175127`) |
| OS | QNX 6.6, `os_version 0x02001305`, rooted |
| `0x2801` | `A=+0x18=0`, `B=+0x1c=10` → retry branch; password `0000` verified offline (5810-iteration digest matches) |
| `0x2004` | absent |
| `0x2040` | present = `2` (tbb case 2 → gate; same decision as absent) |
| `0x2074` | `OsRegionV3`, requested=actual=0, boot_count_0=3, max=0 |
| `BOOTCTR_FCTRY` | **absent** → selector 0 (ECDSA) before arming |
| Active IFS | `sha256 75be24e8…`, size `0x2BFE10`, CRC `0x8D46FB4A` valid, `desc[0xD4]=4` |
| `hd2` IFS | all zero |

## 2. What was changed (5 bytes)

`89-crc_tool.py reseal … --image-off 0 --type 6`:

- image offset `0x100` (media `0x100100`): `4 -> 6` (OS type; CRC gate)
- image offset `0x2BFE0C` (media `0x3BFE0C`): CRC `0x8D46FB4A -> 0x116B2562`

Only those 5 bytes differ; the reconstructed sealed image matched byte-for-byte.

## 3. Writes performed

1. Wrote two sectors to `/dev/hd1`: LBA 2048 (`0x100000`) and LBA 7679 (`0x3BFE00`),
   `bs=512 conv=notrunc`. Both read back byte-identical.
2. Full 2.75 MB media read-back verified with the bootloader's own checks:
   structure PASS, `desc[0xD4] == 6` PASS, CRC `0x116B2562` PASS → `VERIFIED (CRC path)`.
3. RPMB rehearsal on `TEST_PAD`: create → write 4 random bytes → read back exact → delete.
   `/dev/rpmb` is a directory resmgr; `dd … seek=0 conv=notrunc` is the correct writer.
4. Created `BOOTCTR_FCTRY = 32 00 00 00` (50), size 4, read back exact.
5. `sync`, then `/proc/boot/shutdown -S reboot` (this build's `shutdown` takes
   `-S system|reboot`; there is no `reboot`/`shutdown -r`).

## 4. Post-boot evidence

| Item | After reboot |
|---|---|
| OS | alive, root, `uname` unchanged |
| `BOOTCTR_FCTRY` | `31 00 00 00` (49) — one successful factory boot consumed |
| `0x2801` | byte-identical (`A=0`, `B=10`, same salt/digest) |
| `0x2074` | byte-identical (factory path does not read it) |
| PPS `osregion` | requested=actual=0, counts 3/3, max=0, `BootStatus: Success.` |
| Boot reason | `winchester-bootreason`: "Power on condition previously cleared" |

Counter topped back up to `32 00 00 00` (one extra RPMB write) and verified.

## 5. Confirmations / corrections

- **Confirmed:** the factory path verifies region 0 only, and there is no fallback
  (`0x800142B0` selector, `cbz r0, 0x800142d0`; nonzero → `0x800142BC` region 0 →
  `0x80014148` → return).
- **Confirmed:** selector 1 selects the CRC gate and CRC (`0x80014254`/`0x80014260`:
  `cbnz r6` skips ECDSA; `cmp r6,#1` → `0x80013FB6` → `0x80014788`).
- **Confirmed:** a successful factory boot decrements `BOOTCTR_FCTRY`
  (`0x8000911E` → `0x800115E6`), observed 50 → 49.
- **Confirmed:** `0x2074` is not consulted on the factory path, so `actual`/counts do not
  move in factory mode.
- **`85` Phase 3/4 design is dead** ("modified region 1 + CRC-valid region 0 fallback"): the
  factory path never looks at region 1.

## 6. Current state of the spare

- Region 0 image: same TabletOS payload, `os_type=6`, CRC `0x116B2562`, ECDSA signature
  invalid (irrelevant while the counter is nonzero).
- `BOOTCTR_FCTRY = 0x32`, so ~50 successful factory boots remain; top up from the booted OS
  before it runs low.
- The original region-0 IFS (and the two original sectors) are in
  `analysis/playbook-live-spare-2026-10-25/` (`hd1-ifs-image.bin`,
  `sector-0x100000.orig.bin`, `sector-0x3BFE00.orig.bin`).
- `nvram2` + `libgcc_s.so.1` remain at `/accounts/devuser/` (persistent); the 2.x tool loads
  `/lib/libnvram.so.1` and needs `LD_LIBRARY_PATH=/accounts/devuser` for `libgcc_s.so.1`.

## 7. Next step — Linux payload (the truly one-way test)

The CRC path is proven. The remaining risk is the payload swap:

- Build the chainloader per `91`: ARM, SVC, IRQ/FIQ masked, MMU off, WDT2 serviced,
  `r0=0`, `r1=~0`, `r2=DTB`; keep `+0xD4=6`, `end-8=0xD7C82D1F`, and a valid CRC trailer.
- Keep the current sealed TabletOS IFS as the restore image. While TabletOS boots, a failed
  test is recoverable by restoring the two sectors. Once the payload is Linux, there is no
  `/dev/rpmb` writer unless the payload provides one, so the counter only runs down.
- Recommended first payload: a minimal state-probe/chainloader that prints or stores a marker
  before attempting a kernel, so a failure is distinguishable from a hang.
- Do not touch region 1; the factory path ignores it.
