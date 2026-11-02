# 143 — The RPMB frame key is pinned; and a reported "safe" procfs read reset the unit

**Session 19 (absolute), Linux-era session 13.  2026-10-30.**
Status: **the key `f` is closed** — the 32-byte frame key and the 16-byte block
key are known, and both reproduce device MACs exactly.  The Linux write is
implemented in `pbrpmb` (`scan`/`prep`/`write`) but was **not yet issued** when
this report was written; §7 is the gate.

> **Update (same session, report 144): the write is blocked.**  The record
> block is **encrypted on media by the secure monitor** (`0x2F`); the MACs
> verify over the ciphertext.  §5's `write` path (patch the received block) is
> therefore invalid until the seal is reproduced.  See
> `144-rpmb-record-store-sealed-2026-10-30.md`.  The key work here stands.

---

## 1. The incident: a `/proc/<pid>/as` read took TabletOS down

While locating the key page I read another process's address space with the
interface the QNX docs describe (`open("/proc/<pid>/as")`, `lseek()` to a virtual
address, `read()`), using `dd`.  The third read — a `MAP_PHYS` page — returned
`dd: /proc/53263/as: Server fault on msg pass`, and the unit rebooted (operator:
"the playbook rebooted … it's booting up now").  The procfs server is part of
`procnto`; a fault there takes the system down.

**This is recorded as trap 59: do not use `lseek()`+`read()` on
`/proc/<pid>/as` on this unit.**  It is a QNX/procnto bug, not a
misunderstanding of the interface — the QNX 6.6 cookbook documents exactly this
pattern.  The two earlier reads (ordinary DRAM pages) printed nothing; the
faulting one was at a `MAP_PHYS`/device page.  Whether an ordinary DRAM page
would fault is untested and **must not be tested** — the route below avoids the
address-space file entirely.

State after the reboot, verified before anything else:

| item | value |
|---|---|
| TabletOS | back up, door up, `rpmb-omap4` running (new PID) |
| `BOOTCTR_FCTRY` | **`32 00 00 00`** — the TabletOS top-up restored it |
| `accounts/devuser/startup.sh` | 4490 B, sha256 `c265cfbb…` (the fixed version) |
| sealed IFS | sha256 `c72b0530…` (unchanged) |
| region-1 root at `0x29000000` | sha256 `0b87c29f…` (unchanged) |

So the reboot cost nothing durable: a factory/CRC boot spends one counter unit
and the next TabletOS boot puts it back.

---

## 2. How the key was read without touching `/proc/<pid>/as`

The safe route uses **metadata only**, then a **physical** read:

1. `DCMD_PROC_MAPINFO` (the same devctl `pidin -p <pid> map` issues) lists the
   process's mappings.  The RpmbInit page is the only mapping whose flags are
   `0x1090b02` = `MAP_ANON|MAP_PHYS|MAP_PRIVATE` + `PROT_READ|WRITE|NOCACHE`,
   matching the `mmap(0, 0x1000, PROT_READ|PROT_WRITE|PROT_NOCACHE,
   MAP_SHARED|MAP_PHYS|MAP_ANON, …)` at `0x3060`–`0x3070` in `rpmb-omap4`
   (report 141 §3.2).  This boot: vaddr **`0x786f7000`**.
2. `DCMD_PROC_PTINFO` returns the page-table entry for that virtual address:
   **`paddr 0xbfb2b000`**, size `0x1000`, flags `0x00010b00`
   (`READ|WRITE|NOCACHE`).
3. `memdump3 0xbfb2b000 0x1000` (QNX `mmap_device_memory`, the project's
   established physical reader) dumps the page.

The page content is the RPMB working page.  `+0x440` begins the seed slot and
`+0x460` the 32-byte frame key:

```
bfb2b440: 15 00 00 01 4d 43 47 41 46 41 10 00 46 28 01 b4  ....MCGAFA..F(..
bfb2b450: 3e 00 00 00 69 6e 67 20 69 6e 20 74 68 65 20 6f  >...ing in the o
bfb2b460: df ee c0 ce 39 28 99 c2 74 e1 2a 01 c8 b3 3d 6f  ....9(..t.*...=o
bfb2b470: f0 ee f5 85 1a 6f 9a 0b b2 4a fd 0a 1c 5e 22 74  .....o...J...^"t
```

Two things are visible:

* The **seed is the CID-derived one**, not the literal: `MCGAFA` (the eMMC
  product name) is at `+0x444`.  The literal's tail (`ing in the o`) survives
  at `+0x454`..`+0x45f` because the 20-byte CID seed was copied over the first
  20 bytes of a 32-byte literal copy — i.e. the fallback copy happened and a
  KEK/CID path then overwrote its head.  This settles report 141's U2: the seed
  is CID-derived at runtime.
* `+0x460` is 32 high-entropy bytes: the frame key.

Tool: `tools/qnx-procmem.c` → `pbmem <pid> map|page|pt <vaddr>` (metadata only;
the source carries the warning).  The page dump is kept in
`docs/reports/data/rpmb-page-0xbfb2b000-2026-10-30.txt`.

---

## 3. The keys, verified against the captured device MAC

### 3.1 The frame key

```
df ee c0 ce 39 28 99 c2 74 e1 2a 01 c8 b3 3d 6f
f0 ee f5 85 1a 6f 9a 0b b2 4a fd 0a 1c 5e 22 74
```

Against the captured 284-byte MAC input from `0xDDB9D733`
(`docs/reports/data/rpmb-probe-0xDDB9D733-2026-10-30.txt`):

```
HMAC-SHA256(key, resp[0xE4..0x200)) = fc395840…5645f568  == the device's MAC
```

**Match.**  `f` is closed: the key is `f(CID seed)`, deterministic, and usable
from any OS.  (The captured file prints only 272 of the 284 bytes; the missing
12 are the big-endian tail `00000000 0000 0001 0000 0400`.  Note also that the
file labels `b5085d0f…` as the nonce; it is really `data[224:240]`.  The nonce
is `ec0fd292…`.)

### 3.2 The block key and the byte-order convention

The record block's own 32-byte HMAC is **not** over the bytes as received.  With

```
logical = reverse(frame.data[256])
```

the captured block 0 satisfies

```
HMAC-SHA256(seed[0:16], logical[0:0xE0]) == logical[0xE0:0x100]
```

where `seed[0:16] = 15 00 00 01 4d 43 47 41 46 41 10 00 46 28 01 b4`.
It does **not** match in the wire view.  This confirms report 141 §4.1's QNX
256-byte reversal and fixes report 141 §7.1 step 3, which said to recompute over
`F[0xE4..0x1C4)` (the wire view): the correct order is patch the **logical**
block, re-MAC `logical[0:0xE0]`, reverse back to wire, then MAC the frame over
`F[0xE4..0x200)`.  Trap 60.

### 3.3 A correction to report 141 §3.5

Report 141 §3.5 concluded the `0x2F` op 1 call writes its output to
`page+0x460`; §10 (the bench result) corrected that to "16 bytes at
`page+0x000`".  The **conclusion** stands — the 32-byte frame key is at
`page+0x460` and is the one that verifies — but §10's buffer accounting is the
right one.  What fills `+0x460` is still inside the monitor; it does not matter
now that the value is known and verified.

---

## 4. Block 0 is not the record store

The captured block 0 is high-entropy in both views and contains no record
header.  It is authenticated (the block HMAC verifies), so it is a legitimate
data block — almost certainly the first record block holding the large
`BSIS_PRIV`-style record(s), not a hole.  The `BOOTCTR_FCTRY` record is in some
later block; `pbrpmb scan` finds it by MAC-verifying each block and searching
the **logical** view (with a wire-view diagnostic that prints `WIREVIEW` if it
ever finds it there).

---

## 5. The write implementation (`initramfs/rpmb.c`)

`pbrpmb` now has three new, ordered commands:

| command | writes | what it does |
|---|---:|---|
| `scan [max]` | **no** | MAC-verifies blocks `0..max-1` (default 256), finds `BOOTCTR_FCTRY`, prints block, name offset, length, value and whether the block HMAC verifies |
| `prep <block>` | **no** | gets the counter, re-reads the block, validates both HMACs, checks the value is `1..0x32` and not already `0x32`, patches `32 00 00 00`, prints the frame — and stops |
| `write <block> yes` | **one** | the same build, then one `MMC_IOC_MULTI_CMD`: CMD25 `write_flag=1\|BIT(31)` (rel. write) + CMD25 result-read + CMD18, then counter and an authenticated readback (frame MAC, block HMAC, value) |

Gates: the value must be exactly one non-zero byte `1..0x32` in the low position
and three zeros; `0`, `> 0x32`, a length ≠ 4, a bad frame MAC, a bad block HMAC,
or an already-`0x32` value all refuse.  `write` requires the literal `yes`
argument.  A rejected write leaves the value unchanged and the unit bootable.
It never retries.

---

## 6. Safety posture

* The write is the single most dangerous operation in the project; the value is
  `0x32` only, once, with a readback.
* Failure is asymmetric but a *refused* write is benign (value stays at `0x31`
  after a Linux boot, still bootable).  The brick modes are a wrong value or a
  malformed record (length ≠ 4), both excluded by the gates.
* Each authenticated write spends one unit of the RPMB write counter (a
  monotonic device counter, not `BOOTCTR_FCTRY`); do not loop.
* Region 0 is still the reset path: restore `local/images/ifs0.sealed.bin` and
  verify `c72b0530…` after the test.

---

## 7. The gate for the first write

1. Boot Linux on the new image (a factory boot decrements `BOOTCTR_FCTRY` to
   `0x31`).
2. `run /pbrpmb probe` — the pinned key must verify block 0's MAC on-device.
3. `run /pbrpmb scan` — find the record; the value must read `31 00 00 00`.
4. `run /pbrpmb prep <block>` — inspect the frame; it must print the same block
   and value and `NOT written`.
5. Re-read `SAFETY.md` §2/§4.
6. `run /pbrpmb write <block> yes` — one write, then readback; require
   `write: PASS — BOOTCTR_FCTRY is 32 00 00 00` and counter +1.
