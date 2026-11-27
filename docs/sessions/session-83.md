# Session 83 (absolute) — a 545-shaped DMS_CTRL bug fixed, the render's failure moved from a timeout to a page fault, and the GPU marker built but left unvalidated

> Numbering: absolute `83` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era 77**
> (`absolute = Linux-era + 6`).  Continues session 82 (the render wall is the
> MTE state emit; the client stream is proven, the wall is the SGX540 µkernel).

Date / span: 2026-11-27.
Unit state at the end: **Linux 7.3.3 "Nura edge"** on hd3, not TabletOS.
Region 0 `0x02C1DDC2` (unchanged; re-verified from a live media read this
session); region 1 `20a5edd3…`; hd3 `/dev/loop0` ext4 rw; `BOOTCTR_FCTRY 0x22`
at the last boot (self-maintaining, `no write`); firmware `kick18ccb`
(`d4087521…`); module `96368b07…`; `wlan0` **up** (`192.168.0.231/24`), usb0 up;
battery `Full` 100 %; desktop llvmpipe.

## Goal of this session

HANDOFF-83 §0b step 1: **the GPU-side marker** — write a fixed word to client
memory as the first instruction of the state-emit USE task, submit one render,
read it mid-hang, to answer "does the state-copy USSE task RUN?".  Client-only,
no reboot.  The operator then extended the session twice: keep going, and read
the vendor DDK 1.8 drop (including the kernel-module source).

## Confirmed (measured, read in a file, or seen on the bench)

**The marker instrument.**
* All four marker programs read **0**, and so did all four controls, with and
  without `SGXGEN_NO_STATE`.  [tool output; capture "SESSION 83"]
* The vertex USE pipe services **nothing**: `USE0_SERV_VERTEX`,
  `USE1_SERV_VERTEX` and `USE0_SERV_PIXEL` read `0x00010000` (EMPTY) at idle,
  1.0 s into the stall, and after; only `USE0_SERV_EVENT` climbs
  (`0x1067e → 0x106ab → 0x106d7`), which is the microkernel's event handler.
  [live `status`, three samples in one boot]
* Five real instrument defects were found and fixed: `st.a.d base,#0` writes
  `base+4` (ISA.md:1485); a `limm` patch must be a read-modify-write (the naive
  rewrite dropped `skipinv`, `fc200000`→`fca00000`); the marker BO's
  `drm_gem_shmem_object_mmap` mapping is **cacheable** (`map_wc` unset), so a
  plain load returned the `memset`'s 0; the marker must follow the `phas`; a
  program using `r` registers needs a nonzero DOUTU temp count (PDS.md:301-305).
  [`local/work/powervr_sgx/tools/gen/{mkusse.py,sgxgen.c}`]
* The `limm` patch agrees word-for-word with `sgx-asm` for nine addresses
  including `0` and `0xffffffff`.  [host test]
* Production is unchanged: every `usse_*_w[]` the production path uses is
  byte-identical to the pre-session header; the four marker programs are
  additions.  [name-by-name word diff]

**The DMS_CTRL bug — the session's substantive result.**
* `EUR_CR_DMS_CTRL`'s `MAX_NUM_VERTEX_PARTITIONS` is **per-core**:
  `sgx545defs.h` MASK `0xF8000000` SHIFT **27** (5 bits); `sgx540defs.h` MASK
  `0x0E000000` SHIFT **25** (3 bits); `sgx543` shift 26; `sgx535` shift 25.
* The port sent one constant `TA_CTL = 0x50000000` = `10 << 27` (the SGX545's
  `SGX_DEFAULT_MAX_NUM_VERTEX_PARTITIONS 10`, `sgxinfo_client.h`) for every
  core; on the SGX540 `0x50000000 & 0x0E000000 == 0` — **zero vertex
  partitions**.
* The µkernel copies the TA command's control words into that field every kick
  (`ta.asm:3507-3548`), and the vendor's client derives the word per core
  (`sgxkick_client.c:1024`): `4 << 25 = 0x08000000` for the SGX540.
* The **pixel half** was missing too: the µkernel ORs
  `HWRENDERDETAILS.ui32NumPixelPartitions` into the same register at the 3D
  (`3d.asm:2914-2920`) and SPM (`spm.asm:2692-2698`), and our render-details
  struct kept that word inside an unwritten private pad.
* **Fixed per core** in `struct pvrsgx_cmd_core` (`.ta_ctl`,
  `.pixel_partitions`): SGX540 `4<<25` / `6<<22`, SGX545 `10<<27` / `31<<22`,
  SGX530 `4<<25` / `6<<22`; the SGX545's bytes are unchanged; render details
  gained `num_pixel_partitions` with `static_assert(offsetof(...) == 0x6c)`;
  KUnit `core_test_sgx540` asserts both values and that each fits its own core's
  mask.
* **On the wire**: the live TA command reads cmd+0x14/+0x18 = **`08000000`**
  (was `50000000`); the live render details read `rd+0x6c = 0x01800000`.
  [live `render` debugfs dump]
* **The failure changed**: before, `job timed out (… events 0x0/0x10)`; after,
  reproducibly 3/3, `job page fault at 0x00401000 (BIF status 0x0000e000,
  requestor 0x2000, write)`.

**The page fault, diagnosed.**
* `0xe000` = FAULT(b0-13) | **`PF_N_RW` b14 = a write** | FLUSH_COMPLETE(b15);
  the requestor field is a 14-bit **mask** (`FAULT_REQ_MASK 0x3FFF`), not a code.
  [sgx540defs.h:2817-2824]
* The faulting write runs **with the client PD loaded**: the driver message reads
  `DIR_LIST_BASE0 0x8cf3e000 kernel PD 0x86599000` — different values.
* The requestor bases do not explain it: `TA_REQ_BASE 0x08800000` (the 3D params
  heap base, correctly 1 MiB-aligned), `3D_REQ_BASE 0`, `ZLS_REQ_BASE 0`,
  `MEM_REQ_STAT 0`.
* `0x00401000` is **inside the general heap** `[0x00001000, 0x04400000)`
  (`pvrsgx_core.c:283`); the tool allocates only `0x2000..0x87000` there.
  [tool `-v` heap list]
* **Mapping the range removes the fault and restores the original timeout.**
  One clean boot, general heap only: baseline → fault `0x00401000` (requestor
  `0x2000`, status `0xe000`); map 4 KiB at `0x401000` → fault **moves** to
  `0x0001f000` (requestor `0x0002`, status `0xc002`); map 1 MiB or 4 MiB at
  `0x400000` → **no fault**, and the job reports
  `job timed out (… events 0x20000000/0x00000010)` — the session-79 signature.

**Leads closed by reading the vendor code (no boots).**
* `TE_STATE = 0` is **correct**: the vendor zeroes `ui32TEState[]`
  (`sgxkick_client.c:2098-2101`) and `ta.asm:4340-4344` loads `TE_STATE` from it.
* `TE_PSG = 0x01000000` (b24 `ENABLE_CONTEXT_STATE_RESTORE`) is **correct**:
  `ta.asm:4346-4352` ORs it on every kick unconditionally.
* The `0x618` page-table base `0x09822000` is **correct**: the vendor stores the
  raw `sEVMPageTableDevVAddr` and masks `0x0FFFFFF0`
  (`sgx_utils.asm:988-990`; `sgxpb.c:231`) — a byte address.
* The `0x234` OTPM CSM-base write is **excluded by measurement**: with
  `UK_NO284=1` (store removed; disassembly 2033 → 2030 instructions,
  `str #141` ×1 → ×0) the fault was **identical**; reverted and the revert
  proven.  [firmware `kick19no284` `c7cc8e12…`]

**Environment.**
* The kernel-module source **is** in the drop: `eurasia_km/services4/srvkm/
  devices/sgx/` (`sgxkick.c`, `sgxutils.c`, `sgxinit.c`, `mmu.c`, `sgxreset.c`,
  `sgxtransfer.c`, `pb.c`, `sgxpower.c`) — a top-level sibling of `eurasia/`.
* Our render-details struct is a **flat reconstruction** of a struct the vendor
  defines nested (it begins with `SGXMKIF_3DREGISTERS s3DRegs`; `sizeof` 0x90
  against our 0x1ec), so the 1.8 field offsets do **not** transfer to us.
  [compiled the 1.8 struct: `ui32TEState` 0x5c, `ui32NumPixelPartitions` 0x74]

## Inferred (reasoning from evidence, not directly measured)

* **The page fault is a side effect, not the render wall.**  Mapping 1 MiB at
  `0x400000` removes the fault and the job reverts to the session-79 timeout —
  one variable, one clean boot.  Strength: strong.
* **The writer walks forward through the general heap.**  A single 4 KiB page at
  `0x401000` *moves* the fault to `0x0001f000` instead of clearing it; a fixed
  wrong pointer would not move when an unrelated page is mapped.  Strength:
  medium-strong (two addresses, one boot).
* **The DMS_CTRL fix is a genuine defect fix even though it does not render.**
  The field is per-core in both headers, our value evaluated to 0 in the 540's
  field, the live command now carries the vendor's value, and the failure
  signature changed.  Strength: strong for "the old value was wrong"; the link
  to the fault is measured only as "the failure moved".
* **The marker as handed over cannot be validated on this build.**  Four
  controls, all zero, with the pipe counters showing why.  Strength: strong.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"`sa0` holds the state block's address."**  Wrong: the state block is DMA'd
  kind 3 into the **primary** attributes (`pa`); `sa0` is the
  vertex-secondary block's self-address.  Killed by STATE.md §2's capture table
  and the vendor program's `mov o0, pa0`.
* **"`NO_STATE` is a control because the TE wakes."**  Wrong: session 81 already
  recorded the TE wakes yet the draw times out; this session's `NO_STATE` render
  took the full 2 s timeout.
* **"The event ptoff task is a valid control because the event pipe runs."**
  Wrong: that pipe is the **microkernel's** handler; the client pixel-event
  program is dispatched per 3D tile, which never starts.
* **"A zero from a GPU marker means the task did not run."**  Only after the
  store path is proven on the same pipe — which this stack does not provide.
* **"`TA_CTL = 0x50000000` is the TA flags word."**  It is the SGX545's
  vertex-partition constant, outside the SGX540's field.  This closes trap 429:
  the RMW *does* land; it lands in a partition-count field.
* **"The `0x234` OTPM CSM-base write is the fault."**  Measured negative
  (identical fault with the store removed).
* **"`TE_STATE = 0` is a defect from a zeroed pad."**  Correct value; the vendor
  zeroes it too.
* **"`TE_PSG`'s b24 is unexplained."**  It is the vendor's own unconditional OR.
* **"The fault runs after the PD is restored to the kernel PD."**  Wrong: the
  fault message shows the client PD loaded.
* **"The fault address lies in no heap the VM maps."**  **Wrong** (an earlier
  claim of mine this session): `0x00401000` is inside the general heap.  It is
  merely *unallocated*.  Corrected by reading the heap layout and the tool's
  allocation list.
* **"Map 1 MiB at `0x400000` still faults."**  That reading was taken at
  `resets 19` on a degraded GPU and is **invalid** (trap 423/424 class); on a
  fresh boot the mapping clears the fault.
* **"The vendor drop has no kernel-module source."**  Wrong — `eurasia_km/`
  exists; the operator corrected me and was right.
* **My hand-parse of the 1.8 render-details offsets ("0x6c") was wrong** — it
  double-counted the nested `SGXMKIF_3DREGISTERS`; the real offset is 0x74 and
  none apply to our flat struct.  Trap 431's *mechanism* (the µkernel reads a
  partition word we never wrote) stands for our layout.
* **I overwrote the host's `boot/chainloader/kernel.img`** by running
  `tools/build_kernel_image.py` expecting a `--check` mode it does not have.  It
  is gitignored, so git could not restore it.  Recovered by reading region 0 off
  the media and writing those exact bytes back — size `10,450,520`, magic
  `0xD7C82D1F`, CRC `0x02C1DDC2`, sha256 `70474c01…`, matching the record.  No
  flash occurred, so media was never at risk.

## Unknown / open questions

* **Does the state-copy USSE task run?**  Needs the marker validated by a
  vertex-pipe task that provably completes, a microkernel-side store, or a
  driver-side read of the BO.
* **Which block writes `0x00401000`?**  The requestor is a mask; bit 13 vs bit 1
  are unnamed in the drop.  Needs a per-bit test or a block-by-block disable.
* **Why ~4 MiB into the general heap?**  Likely a size/stride used as an address
  or offset in `TA_PB`/`TE`/`D3_RD`; mapping successive ranges until the fault
  stops moving would bound it.
* **Whether the side-fault is worth fixing before the wall** — a real
  out-of-allocation write, but not the render blocker.  Operator's call.
* Unchanged: `FIX_HW_BRN_33181` (`IDLETA`/`RESUME_TA`) unimplemented; the frame
  tool's two-kick layout; the owed patch-series export (§110).

## Superseded / uncertain

* **Trap 429's question** ("why does the RMW not land the TA field in
  `DMS_CTRL`") is superseded by trap 431: the RMW lands; `0x50000000` is a
  vertex-partition constant in the wrong position for this core.
* **Trap 418's open "`TE_PSG` b24 is unexplained"** is closed: the vendor ORs it
  on every kick.
* **This session's own eighth-part reading** ("mapping 1 MiB still faults") is
  superseded by the ninth part (clean boot: it clears).  The eighth part was
  taken on a degraded GPU.
* **This note's own first-half claim** that `0x00401000` "lies in no heap the VM
  maps" is superseded (it is in the general heap, unallocated).

## Artefacts produced

* **Driver** (`local/work/powervr_sgx`, uncommitted, not in a patch — §110):
  `pvrsgx_cmd.h`, `pvrsgx_cmd_defs.c`, `pvrsgx_render.c`, `pvrsgx_fwif.h`,
  `pvrsgx_job.c`, `tests/pvrsgx_core_test.c` (per-core values + the assertions).
* **Generator/tool**: `tools/gen/{mkusse.py,sgxgen.c,sgxgen.h}`,
  `drm/tests/powervr_sgx_frame.c`; `tools/gen/usse_progs.h` regenerated (every
  production program byte-identical; four marker programs added).
* **µkernel**: `ukernel/cores/sgx540.py` (`TA_RT` note),
  `ukernel/ukernel.py` (`UK_NO284` gate).
* **Firmware built, not shipped**: `kick19ctl` `3956feb5…`,
  `kick19no284` `c7cc8e12…` (golden `83b5b0a7…` intact, `test_emu` 8/8).  Live
  image stays `kick18ccb` `d4087521…`.
* **Modules**: live `96368b07…`; reverts `.pre-dms540` `92e14ad4…`,
  `.pre-reqlog` `91729a08…`, `.pre-pdlog` `21833840…`, `.pre-reqbase`
  `2c191e1a…`.
* **Device tools**: `/tmp/powervr_sgx_frame.s83` `3da50fab…`,
  `/tmp/frame-mapto` `775889e7…`; pre-session `1177fafd…` / `.loc 68bc15af…`
  untouched.
* **Docs**: trap 431, DECISIONS §124, the capture's eight session-83 sections,
  STATE/HANDOFF/ROADMAP updates, this note.
* **No media write, no flash, no RPMB write.**
