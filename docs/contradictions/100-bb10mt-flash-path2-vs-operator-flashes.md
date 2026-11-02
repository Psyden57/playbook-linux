# `bb10mt flash` on this unit: path 2 "unavailable" vs. the operator's flashing history

Status: **resolved (2026-11-01)** — bb10mt's containers carry the destination in the
**V1 `qcfp`** record, i.e. they are path-1 correct, and an earlier measurement in
this file that said otherwise was wrong.  Evidence in §3.

## 1. The claims as they stood

**A — report 100 §4 ("closed"):** the live MCT has no `0x39` entries, so the
loader's **path 2** cannot resolve a partition by `PartitionID == 8`, and
`bb10mt flash` — which builds `_type=8`, `rrcq.offset=0` — cannot address the IFS
on this unit.

**B — operator, 2026-11-01:** full firmware images have been flashed with
`bb10mt` in the past, **including the IFS**.

**C — report 98 §9.3/§9.6:** shipped autoloaders use **path 2** (`_type=8`,
`rrcq.offset=0`, needs an MCT `0x39` entry); the equivalent **path 1** needs only
the `0x34` MBR extent and sets `rrcq.offset = 0x20` (`0x825 − 0x805`).  It adds
that `qcfm.pas:582` "drops `Delta`".

**D — report 139 §3:** with a container carrying both records, the loader **used
V1**: `0x815 + V1.offset 0x10 = 0x825`, i.e. path 1.

## 2. The wrong measurement (kept deliberately)

A first pass reported this table and concluded that bb10mt "maps the IFS offset to
0", making its container a path-2 artifact and, where path 2 fails, "actively
unsafe" (a V1 fallback at `0x815`, over the region-0 header and the `0xF0000`
signature block):

| `.lst` offset | "V1 offset" | "V2 offset" |
|---|---|---|
| `16` | `0` | `0x10` |
| `192` | `0xC0` | — |

**That table is wrong.**  The `16` row came from re-packing from a `.lst` that
`bb10mt unpack` had rewritten (`unpack` emits `<path>=Delta,flags`, so the input
being packed was no longer the file I thought), and the V1/V2 values were read as
swapped.  It is reproduced here so the retraction is auditable, not silently
deleted.

## 3. What settles it

`tools/bbusb.py`'s container code transcribes `TMFCQPackStream.BuildHeadersAndMap` and
`TMFCQPackStream.Read` from bb10mt's own source (`qcfm.pas`, commit
`d45b66ba…`) rather than from the binary.  Built from the IFS with `-v 1,2`, it is
**byte-identical to bb10mt's own output — 0 differing bytes at both `Delta` 16 and
`Delta` 192**, 2,949,152 bytes each.

Both the source and the vendor container agree on the record semantics:

| source | meaning |
|---|---|
| `qcfm.pas:559` `arr1[j].offset := Delta + XBRA[i][j].BlockIndex` | **V1 carries the destination** (region-relative, like the `.lst`) |
| `qcfm.pas:582` `arr2[j].offset := XBRA[i][j].BlockIndex` | V2 carries the in-image block index, no `Delta` — by design, because path 2 resolves the partition by `_type` |

Measured on the vendor `PlayBook_2.1.0.1917.signed` (report 161 data set):

| component | type | V1 offset | V2 offset |
|---|---|---|---|
| `.mbr` | `0x06` | 0 | 0 |
| `.rcfs` | `0x09` | **192** | 0 |
| `.sig` | `0x07` | **15** | 0 |
| `.ifs` | `0x08` | **16** | 0 |
| `.ufs` | `0x05` | **16640** | 0 |

and for the RCFS's later runs, V2 = V1 − 192 exactly (`{2229,4283,6323,8185}` vs
`{2037,4091,6131,7993}`).  Fresh `bb10mt pack -v 1,2` runs give V1 `16` / V2 `0`
for the IFS and V1 `192` / V2 `0` for the RCFS.

## 4. Resolution

* bb10mt's IFS container puts **`16`** in the V1 run record, so on this unit the
  loader — which consumes V1 (claim D) — computes `0x815 + 0x10 = 0x825`: the IFS
  slot.  **There is no `0x815` hazard.**  Claim A's "path 2 unavailable" never
  comes into it, because the container does not depend on path 2 for addressing,
  and the operator's flashing history (claim B) is consistent with that.
* Report 98's "drops `Delta`" line refers to the **V2** record, where dropping it
  is intentional; the V1 record is correct.  `tools/pre_os_ifs_flash.py`'s
  `rrcq.offset = 0x20` and bb10mt's `qcfp.offset = 16` describe the same
  destination under the two conventions the two records use.
* Still open, but no longer blocking anything here: which MCT the loader parses
  (`External MCT/HWV: Enabled`, report 161 §3).

**Closed later the same day, by static RE of the loader (report 164, §Q5):** `0x39`
is **not** an MCT partition type at all — it is the CREAD opcode.  The dispatch tree
compares it as a *command byte* (`0x801173B4` router, `0x80102DE8` CREAD
dispatcher), and partition resolution goes through a fixed four-entry u16 table at
`0x80136998` (`{0x31, 0x2B, 0x2C, 0xFF}`) with nothing comparing against 8.  So
report 100 was right that path 2 cannot resolve a partition, and my "path 2 must
resolve" reading of claim B was wrong — path 1 is the only write path, which is
precisely the one bb10mt's V1 record addresses.  The contradiction is fully closed
in favour of A and D together.

## 5. Side finding — `-s true` is a *dummy* signature, by design

Measured, unrelated to addressing: `pack -l … -v 1,2 -s true` grew the container by
**8 bytes**, not 560, ending in `ffffffff` + `crc32` of everything before it
(`0x8B018059` for that file).  The source (`qcfm.pas:698-705`) appends the whole
560-byte `QNXH` trailer instead, and the **vendor** container ends in the 560-byte
trailer with its CRC field left as `ffffffff`.

**Explained by the operator (2026-11-01):** `-s true` is deliberately a *dummy*
signature.  When modifying a firmware's **UFS** partition you must skip the `.sig`
component (if present) and use the dummy — the device boots fine with it.  That
makes the three forms consistent rather than contradictory: the real trailer
belongs to the unmodified image, and a changed UFS cannot carry it.  (Why the
dummy is accepted is not established; the observation is the operator's.)
