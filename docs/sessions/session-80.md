# Session 80 (absolute) — the VDM stream's record encodings walked and excluded; and the operator opens the DDK drop

> Numbering: absolute `80` (the project's chronological scheme — see
> `docs/sessions/NUMBERING.md`); the reports call this **Linux-era session 74**
> (`absolute = Linux-era + 6`).  Continues session 79 (the first render job,
> the stall localized to the VDM).

Date / span: 2026-11-27.
Unit state at the end: **Linux 7.3.3** on hd3 (Nura), region 0 `0x02C1DDC2`
(no flash), region 1 `20a5edd3…` (re-hashed live this session), firmware
`kick16tail` (`744b072d…`), module `1a2949ab…`, desktop llvmpipe, `wlan0` **UP**
with a lease, `BOOTCTR_FCTRY 0x20`.

## Goal of this session

HANDOFF-80 §0b: the 0xb14 method on the **VDM control stream** — dump the stream
and the state-load programs it names, check every record type against
`sgx540defs.h` and the vendor's own stream builder, then the `TA_TE` tail
hygiene, one render to see whether the stall moved.  Side work, from the
operator mid-session: check how the closed pvr stack was disabled, read the
DDK drop's microkernel source and its specification PDFs.

## Confirmed (measured, read in a file, or seen on the bench)

* **The bench matched the handoff on arrival** — `kick15pds` (`24322c2a…`) live
  and named by dmesg, module `1a2949ab…`, `.working` = kick10 `3fbbd11b…`,
  `BOOTCTR_FCTRY 0x21`, `wlan0` up, battery full.  [live reads]
* **The state-load programs are dumpable and are real PDS programs.**
  `dump_prog()` searched only the pixel rings; extended to
  `pds_pix`/`pds_vtx`/`usse_pix` and wired to visit each STATE record's PDS
  address.  The five STATE records name five programs in the PDS-vertex ring.
  [capture `sgx540-first-render-2026-11-27.txt`, SESSION 80 CONTINUATION]
* **The stream's record field layout IS per-core in the vendor's own source,
  and ours was the SGX545's — three divergences** (trap 416): the index-list
  header's b24 `IDXPRES45`, the state record's `PARTITIONS` nibble (1 on the
  540, 2 on the 545), and the draw's word 5 (the same four numbers at
  different bit positions, `sgxdefs.h:1124-1155`).  [vendor
  `eurasiacon/opengles2/{validate,clear,scissor,accum}.c` + `srvinit`]
* **Changing them does not move the stall.**  Header+partitions together
  (4 of 28 words), then word 5 alone (2 of 28, `0x05800102` — the 540 packing
  byte-for-byte), each submitted as a real render job: the timeout signature is
  identical both times.  Both reverted; the stream is byte-identical to
  session 79's.  [dmesg + `status` reads; capture]
* **The `TA_TE` tail was a real defect and is now pruned** (firmware
  `kick16tail` `744b072d…`).  Re-derived the vendor 540 release binary's write
  set: 92 `str #imm` sites over 41 distinct offsets, none of the SGX545-only
  tail among them; 0x230/0x234 *are* written, as
  `MTE_OTPM_CSM_FLUSH/LOAD_BASE` addresses.  `TA_TE` 28 → 16 entries, SGX545
  golden byte-identical, 8/8 offline tests, one boot, and the render's timeout
  **and** its whole mid-hang TA/VDM/DPM state are field-for-field session 79's.
  [firmware md5; capture]
* **Trap 417 settled: `EVENT_STATUS2 = 0x10` is NON-DISCRIMINATING.**  It reads
  `0x10` on an **idle** driver before any job, and is unchanged through a null
  job, a full render, and mid-hang 1.2 s in.  Verified by disassembling our own
  image: **seven stores to word 69 (`HOST_CLEAR2`), zero to word 70
  (`EVENT_STATUS2`)**; `srvinit` writes only `EVENT_HOST_ENABLE`/`_ENABLE2`
  (`sgxinit.c:3897-3918`).  ⇒ **session 79's "the TA task STARTS" is not
  supported by that bit.**  [live reads at four points; `sgx-disasm` output;
  KNOWN-ISSUES 417]
* **Trap 418: the 540 header NAMES what our code called "unnamed"** —
  `0x200` = `EUR_CR_VDM_START` (:819) and there is **no**
  `EUR_CR_VDM_TASK_KICK` on this core, so `VDM_START` *is* the TA kick;
  `0x428` = `EUR_CR_ISP_START_RENDER` (:1207); `0x694/0x698` =
  `EUR_CR_DPM_TASK_STATE`/`_CONTROL` (`CLEAR`=2); `0x684/0x680` =
  `EUR_CR_DPM_TASK_TA_FREE`/`_3D_FREE` (`LOAD`=1).  Comments corrected; the
  wrapped image rebuilds **byte-identical** (`744b072d…`).  [header + rebuild]
* **Trap 419: `IDXFIFO` wakes mid-hang and LATCHES, and it exists ONLY on the
  540.**  `CLKGATESTATUS` sampled 7× at 0.3 s through the hang: `0x001000a0` at
  t=0 then `0x001800f0` at t=1…6.  Wake set over idle = **{MTE b4, VDM b6,
  IDXFIFO b19}**; TE never wakes; after the driver's reset the value is
  bit-identical to idle.  The SGX545 header does not mention IDXFIFO once, so it
  is the one unit this 545-derived port was written without.  [7 samples in one
  boot; capture]
* **`USE0_SERV_EVENT` count climbs through the hang, USE1 idles, and the reset
  throws the count away** (`0x10bdb` → `0x10c11` → `0x10047`).  The EDM is
  alive; the *render* is what is stuck.  [live reads]
* **Nothing in our code writes `EVENT_STATUS2`, and `ukernel.py:62` cited the
  SGX545's TRIG_TA (`0x80`/b7) where the 540's is `0x10`/b4.**  Corrected.
  [disassembly + header]
* **The closed pvr stack is disabled at modprobe, and the desktop is on
  llvmpipe — measured, not inferred.**  `/etc/modprobe.d/powervr-open.conf` is
  `install pvrsrvkm_omap4_sgx540_120 /bin/false` (trap 381); `lsmod` shows it
  unloaded; `pb-gbmsample` on `card1` prints **`GL=llvmpipe (LLVM 13.0.1, 128
  bits)`** and `PVR:(Error): Couldn't connect to services`.  `pvr_dri.so` is
  still mapped because the closed userspace dlopens it and fails.  [live probe
  + modprobe.d + lsmod]
* **`TA_TE`'s register order, our DPM sequences and the 17-word
  `SGXMKIF_TAREGISTERS` all MATCH the vendor** — verified, not assumed: the
  vendor's order from `ta.asm:3439-3460` (read from `SGXMKIF_TAREGISTERS` in
  struct order) is byte-for-byte ours; the struct is 17 words on the 540 (the
  GSG*/VDM-PDS tail is `#if defined(SGX545)` only); `dpm_load` writes
  CLEAR=2 and waits `EVENT_STATUS 0x240` = `DPM_STATE_CLEAR(0x40) |
  DPM_CONTROL_CLEAR(0x200)` — the vendor's 540 `#else` branch, since
  `SGX_FEATURE_RENDER_TARGET_ARRAYS` is **not** set; `dpm_freelist` writes
  LOAD=1 and acks `HOST_CLEAR2` like `LTAPB_WaitForPBLoad`
  (`sgx_utils.asm:1071-1097`).  [header + vendor asm + our source]
* **The state-load programs decode clean** as our own `state_block()` records
  (`tst;bra;doutd;tst;bra;doutd;doutu;halt`; the empty stub is
  `0x07000345 0xaf000000`), splitting each dump at `data_rows*4` words.
  [disassembly of the dumped programs]
* **Our µkernel never tests `SGXMKIF_TAFLAGS_LASTKICK`.**  The vendor's
  behavioural spec (§6.2.3) says the scene becomes renderable *only* on a kick
  carrying LASTKICK; `ta.asm` tests it in eight places; our `do_ta` masks
  `ta_flags` with `#1` (FIRSTKICK) and `ta_finished` does none of the
  last-kick work.  The kernel *does* deliver it (`pvrsgx_render.c:806,874-876`).
  [spec §6.2.3 + `ta.asm:1131-1178` + our source]
* **The traffic to the vendor image is mechanically possible but
  architecturally blocked** (see Inferred).  [binary analysis]

## Inferred (reasoning from evidence, not directly measured)

* **The stream's record field layout is not the wall.**  Two controlled
  client-only experiments, each changing only the words claimed, each leaving
  the failure identical.  Strength: strong — direct measurement.  It does *not*
  say the encodings are correct; they are wrong per the vendor's builder.
* **`IDXFIFO` is the sharpest remaining observable.**  It is the index FIFO —
  the VDM's first read of a draw — it latches for the whole hang, and it is the
  one unit a 545-derived port could never have watched.  Strength: medium-high
  for "the index fetch is where it stops"; the mechanism is not yet known.
* **`LASTKICK` is the best structural suspect.**  It sits exactly between the
  two measured facts (the µkernel consumes the kick and programs the TA; the 3D
  never starts), it is a missing *branch* rather than a register value, and if
  no render-details entry reaches the 3D run list then nothing is written and
  nothing faults — our symptom.  Strength: medium.  **Not yet measured**: the
  flags reaching the µkernel is a separate claim from the kernel setting them,
  and this project has been burned by that gap three times (traps 403/406/410).
* **"Load the vendor microkernel" is a driver port, not a file swap.**
  Mechanically it is *worse* than it first looked: the vendor `ukernel.bin`
  (19,984 B, 8-aligned) does carry **9** sites matching our magic
  `(i[0] & 0x1fffff) == 0xdbeef`, but every one of them has word 1 =
  **`0xfc237150`** — the sentinel `0xdeadbeef` still *fully encoded* — while
  our own image's **2** placeholders have word 1 = **`0xfc200000`** (value bits
  clear).  `fw_limm()`'s guard `!(i[1] & 0x0003f1f0)` **rejects all 9**
  (`drm/powervr_sgx/pvrsgx_fw.c:170-182`).  I first wrote "the same codec our
  `fw_limm` expects" on the strength of the magic alone; the guard settles it
  the other way.  Beyond that: (a) the vendor image exports **54 labels** with
  one entry point *per kernel-CCB command type* (`HOSTKICKTA 0x978`,
  `…TRANSFER 0x8A8`, `…POWER 0x6F0`, …), while our driver registers only
  programs 0-4 (`ARRAY_SIZE(entry) == 5`) and our image has **one** `kick`
  entry that branches internally; (b) it needs three init programs
  (`InituKernelPrimary1/Secondary/Primary2`) launched with
  `emitpds.tasks.taske` and `IUKP1_PDSDevAddrSec/Prim2`, `IUKP2_PDSExec/PDSData`
  patches, where our launch is one `kick` with `ctlblk`/`pdtable`; (c) the
  single-context core demands `SINGLE_CONTEXT` set, `SECURE` clear **and** a
  `pdtable` patch, which the vendor's two-context protocol has no site for.
  Strength: strong for (a)-(c) — each is a file/struct read, not a guess.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"Catacomb is running on the SGX through `pvr_dri`."**  I read
  `/proc/PID/map_files`, which lists *attempted* dlopens.  Killed by the
  operator ("it's way slower than it was on the GPU") and then by measurement:
  `pb-gbmsample` prints `GL=llvmpipe (LLVM 13.0.1, 128 bits)` and the closed
  userspace reports `PVR:(Error): Couldn't connect to services` because the
  kernel module is `install … /bin/false`.  The correct instrument is a GL
  query or `/proc/PID/maps` *plus* a renderer string, never `map_files`.
* **"The DDK 1.8 drop has no microkernel, only the host-side launcher."**  I
  said this to the operator and it was wrong.  `services4/srvinit/devices/sgx/`
  holds 14 `.asm` files (~865 KB) that *are* the microkernel —
  `ta.asm` (4,876 lines) is titled "USE program for TA kick event processing"
  and exports `TAEventHandler`, `KickTA`, `TAFinished`, `SplitControlStream`.
  It is vendor-toolchain `usedefs.h` assembly, so `sgx-asm` does not parse it,
  but it is the readable reference implementation.  Reading it is what produced
  the `LASTKICK` finding.
* **"The spec PDFs are unusable."**  Also wrong, and the operator corrected the
  project rule (DECISIONS §118).  `pdftotext -layout` extracts them cleanly
  (30 PDFs, ~31.5k lines); `SGX Services Microkernel.Overview.1.1.5` §6.2.3 is
  what named `LASTKICK`.
* **"The vendor image shares our `limm` patch codec, so a wrapper could load
  it."**  Half-right, and the half I first trusted was the wrong half.  Both
  images have sites matching the magic `(i[0] & 0x1fffff) == 0xdbeef` (vendor
  9, ours 2), so I wrote "same codec" — but `fw_limm()`'s **second** condition,
  `!(i[1] & 0x0003f1f0)`, fails on **every** vendor site: theirs are
  `0xfc237150`, ours `0xfc200000`, and `0x37150` is precisely the value bits of
  `0xdeadbeef`.  The vendor's placeholders are *filled in*; ours are blank.  A
  wrapper pointed at it would be rejected by our own loader.  Killed by reading
  the guard (`pvrsgx_fw.c:173`) instead of only the magic.  Trap 421.
* **"`EVENT_STATUS2` latching proves the TA started"** (session 79's headline) —
  withdrawn by measurement, trap 417.
* **"`0x240` is a DPM LOAD wait."**  My own first arithmetic said LOAD|LOAD; it
  is CLEAR|CLEAR, and it is correct for the 540 because
  `SGX_FEATURE_RENDER_TARGET_ARRAYS` is unset.
* **"`git checkout` of the tool is safe."**  Session 79's stream dump was still
  uncommitted in the clone, so the revert took it.  Restored verbatim and
  re-proved (28 words match).  Rule now in DECISIONS §117.
* **"`wlan0` will not re-associate."**  It did — it just took longer than I
  waited (lease `192.168.0.232` at the close).  Corrected in STATE/HANDOFF.
* **The state-program dump's own length rule.**  Reading `vdm_state[10]`'s info
  word one word off gave a 36-word over-read of data-as-code; its real info
  word is `0x0800e103` (rows=2).  Splitting at `data_rows*4` is the fix.

## Unknown / open questions

* **Why the VDM's control-stream execution stalls** — the session's question,
  now narrowed.  Would be settled by the `LASTKICK` measurement and, if that
  fails, by the launch-stub comparison (our PDS0 stub is
  `[0]=0x0020000f … [4]=0x07000305 [5]=0xaf000000`; the vendor's launch is two
  `emitpds.tasks.taske` instructions).
* **Whether the µkernel *sees* `LASTKICK`** — the cheap control is
  `UK_CCBTRACE=1` and ctlblk word 0x38 at `do_ta`.
* **Whether pinning `IDXFIFO` ON moves the stall** — `EUR_CR_CLKGATECTLOVR`
  b16-17 (`IDXFIFO_CLKO`) is named; one boot.
* **`TE_PSG` (0x218) reads `0x01000000` (b24 `ENABLE_CONTEXT_STATE_RESTORE`)
  though the vendor 540 µkernel never writes that register.**  It is a
  *signature* register by its neighbours, so it may be read-only.  Would be
  settled by a write/read-back.
* Unchanged: `CACHE_CTRL`'s PARTDM semantics (trap 411), the `USE_CODE_BASE`
  existence sweep, the owed patch-series export (DECISIONS §110).

## Superseded / uncertain

* **Session 79's "the TA task STARTS (`EVENT_STATUS2 TRIG_TA` latches at the
  driver's timeout)"** — the bit is constant at `0x10` on this stack, idle
  included, and nothing writes it.  Read it as "the host delivered the kick and
  the µkernel consumed it".  The newer measurement is current (trap 417).
* **Session 79's `TA_TE`-tail item (trap 415)** — closed: the tail is pruned
  and the 0x230/0x234 mapping explained.
* **My own mid-session claims to the operator** — "the desktop is on pvr" and
  "the drop has no microkernel" — both corrected above, both measured since.

## Artefacts produced

* Traps **416** (per-core record encodings, with citations and the measured
  negative), **417** (settled: `EVENT_STATUS2` non-discriminating), **418**
  (the 540 header names our "unnamed" registers), **419** (`IDXFIFO`),
  **420** (`LASTKICK` never tested), **421** (the vendor image's `limm` sites
  are filled, not blank — the magic matches, the guard does not).
* `docs/captures/sgx540-first-render-2026-11-27.txt` — three new sections:
  the stream's record encodings, the second continuation (trap 417 settled,
  state-load programs, `IDXFIFO`), and the bench at the close.
* **Firmware** `kick16tail` (`744b072d…`) — the pruned `TA_TE`;
  `.kick15pds` (`24322c2a…`) and `.working` = kick10 (`3fbbd11b…`) preserved.
* **Tool** `/tmp/powervr_sgx_frame` `1177fafd…` (session 79's dump restored +
  the state-program extension); pre-experiment copies `.old79` (`efaefc61…`) and
  `.s80base` (`83c8aef1…`).
* **Clone changes** (`local/work/powervr_sgx`, uncommitted as before):
  `ukernel/cores/sgx540.py` — `TA_TE` pruned, and `CR_TA_KICK`/`CR_3D_KICK`
  renamed to their 540 header identities; `ukernel/ukernel.py` — the K2LOG
  comment corrected (540 TRIG_TA bit, and the measured caveat);
  `tools/gen/sgxgen.c` — `dump_prog`/`dump_progs` extended, and the word-5
  divergence documented at its site with the measured negative.
* **DECISIONS §117** (two rules earned by the session) and **§118** (the
  operator opens the DDK drop's source and specifications as evidence).
* Extract helper used, reproducible: `pdftotext -layout <pdf> <txt>` over
  `/home/psyden/Pdrive/GFX_Linux_DDK-master/docs/*.pdf` (30 files).
