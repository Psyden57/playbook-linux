# 260 — the reference's `cpu_startnext` decompiled: an SCU-power SMC our kernel omits

Date: 2026-11-17 (session 64, Linux-era 58).  Static RE (Ghidra 12.1.4).

## 0. Headline

Ghidra decompiled `startup-playbook`'s secondary-CPU routine
(`FUN_8010016c`, the function the QNX syspage publishes as `cpu_startnext`).
It is five SMC calls and an SEV:

```c
FUN_8010016c(uint bitpos, uint entry_addr) {
  SMC(0x104, 0,          0xffffffff);  // AUX_CORE_BOOT0: clear release bit
  SMC(0x104, 1 << bitpos, 0);          // AUX_CORE_BOOT0: set release bit (0x200 for bit 9)
  SMC(0x105, 0,          0xffffffff);  // AUX_CORE_BOOT1: clear
  SMC(0x105, entry_addr, 0);           // AUX_CORE_BOOT1: secondary entry address
  SMC(0x108, 0,          0xff);        // *** SCU power service ***
  SendEvent();                         // SEV wakes CPU1 from WFE
  return 1;
}
```

Our kernel's `omap4_boot_secondary()` (`arch/arm/mach-omap2/omap-smp.c`) and
`omap-smc.S` do the equivalent 0x104/0x105 and SEV, plus a wakeup SGI — **but
they never call SMC `0x108`**.  `0x108` is the documented
`OMAP4_MON_SCU_PWR_INDEX` (secure-monitor SCU power service; the mainline
`omap-secure.h` names exactly that).  This is the first concrete, reference-
side difference in the CPU1 bring-up path, and a plausible contributor to the
nondeterministic post-`execve` wedge (report 258: the TLB-flush IPI is sent and
serviced, so coherency/SCU state is a live suspect rather than IPI delivery).

**Caveat: this is a difference, not a proven cause.**  Report 254 measured
`SCU_CTRL = 0x531` (enabled) from non-secure Linux, so the SCU is not obviously
off; the monitor service may do more than enable it (power/snoop state).  The
experiment below is the test.

## 1. How it was obtained

* Target `local/work/s74/startup-playbook.bin` (raw ARM, file 0 → `0x80100000`).
* REA's MCP Ghidra provider was unavailable (`GHIDRA_INSTALL_DIR` unset in the
  MCP server environment); the same REA 6.7.0 CLI with
  `GHIDRA_INSTALL_DIR=/home/psyden/tools/ghidra_12.1.4_PUBLIC` is healthy.
  Used the bundled `pyghidra` (installed offline into a throwaway venv) to run
  Ghidra 12.1.4 headless, import the raw image at file-offset addresses, and
  decompile 15 candidate functions.  Pseudocode: `/tmp/opencode/decompiled.txt`.
* Function map from `rea search ... --kind procedures` (459 `FUN_*` entries).
* SMC service ABI: `FUN_80107804(service, arg1, arg2, out1, out2)` does `dsb;
  smc #0` with `r12=service` — the same ABI as our `omap-smc.S`.
* `0x108` = `OMAP4_MON_SCU_PWR_INDEX` from mainline `arch/arm/mach-omap2/
  omap-secure.h` (confirmed via web lookup, 2026-11-17).

## 2. The rest of the decompiled set (context)

* `FUN_8010016c` — `cpu_startnext`, above.  The `1 << bitpos` value for CPU1's
  release is `0x200` when `bitpos = 9`, matching our kernel's
  `AUX_CORE_BOOT0_HS_RELEASE = 0x200` and the ROM's `read AUX_CORE_BOOT0 >> 9`
  release check.  So the 0x104/0x105 calls are equivalent, not a difference.
* `FUN_8010249c` — one SMC `0x102` (PL310 L2 control) then an unreachable loop;
  this is the L2-cache setup path, already seen in the linear disassembly.
* `FUN_8010275c` — clears `WKG_ENB_A_0/1` (`0x48281010`/`0x48281410`) and
  programs the SDMA (`0x4A056xxx`) and GIC target registers
  (`*(...+0x2c) = 0xffffffff` × 6).  Report 219 §2.2 already catalogued these
  startup writes.
* `FUN_8010784c` — sets `CM_SYS_CLKSEL 0x4A306110 = 7` and walks the syspage
  ASCII strings (report 219 §2.2 recorded `= 5`; the value is board/OPP
  dependent — not a CPU-start difference).
* Other functions decompiled were allocator/syspage helpers (no CPU content).

## 3. The proposed experiment (one variable)

Replicate the one missing reference action in `omap4_boot_secondary`:

1. Add a tiny SMC helper for service `0x108` with two args (`r0 = 0`,
   `r1 = 0xff`) to `local/kernel-tree/arch/arm/mach-omap2/omap-smc.S`,
   mirroring the decompiled call.
2. Call it in `omap4_boot_secondary()` just before
   `arch_send_wakeup_ipi_mask()` / the SEV, with an `PBCPU:` marker so the ring
   shows it ran.
3. Rebuild **zImage only** — no `.config` change, so vermagic and all modules
   are unchanged (trap 313 not triggered).  Carve/re-wrap with the fast
   chainloader and the no-`nosmp` DTB into a new IPI-debug image; CRC will
   change.
4. Flash to region 0, reboot via ACM, and test **three dual-core boots**;
   capture a wedge via the quiet read-back image and decode `--boot prev`.
   Success criterion: 3/3 clean `online=0-1` boots (or the last marker moving
   past `pre run_init(/init)`), versus the current ~1/3.
5. Restore `kernel-fastcl-nosmp.img` (`0x8B83421F`) afterwards.

**Cost / risk:** a zImage-only rebuild is minutes, not the ~7 min full build;
flashing region 0 is the documented loop; a bad image is recoverable.  A
kernel rebuild is still required, so hand the long build to the operator if it
runs long.  Do not change `.config` or any module.

## 3a. Experiment run — negative

The one-variable experiment was run.  A zImage-only rebuild added
`omap_ref_scu_pwr(0, 0xff)` (SMC 0x108) + `PBCPU:` markers to
`omap4_boot_secondary`; no `.config`/module change.  Carved/re-wrapped with the
fast chainloader and the no-`nosmp` DTB into
`local/work/kernel-scupwr-cpu1.img` (CRC **`0x391343F6`**, trailer
`1f2dc8d7 f6431339`), flashed to region 0 (`FLASH_OK`), and booted:

* **0 successes / 3 wedges** (baseline for the same IPI-marker kernel was
  1/3).  `lsusb` never showed `0fca:8010`; the unit cycled through WDT2
  bootrom resets (`local/work/scupwr-usb.log`, `cpu1-boot-outcomes.txt`).
  With n=3 this is not statistically decisive against a 1/3 baseline, but it
  is certainly **not an improvement**.
* The wedge ring capture was inconclusive: the handoff recorded only 50 chars
  ("Uncompressing Linux... done, booting the kernel.") for the failed boot,
  with garbage timing words — either the kernel hung unusually early, or the
  ring was reset by the many WDT2 cycles.  The new `smc108` markers were not
  observable, so the SMC call is **not proven to have executed** or to be the
  cause.  (`local/work/r2-scupwr.{bin,words}`, `r2-scupwr-prev.txt`.)
* Region 0 was restored to `kernel-fastcl-nosmp.img` (`0x8B83421F`, verified);
  `BOOTCTR_FCTRY = 0x29` after the experiment's wedge cycles (healthy,
  self-topping).  The experimental source edits were reverted; the built image
  and logs remain as artefacts.

**Conclusion:** replicating the reference's SMC 0x108 SCU-power call alone does
not fix the dual-core wedge.  It remains a genuine reference-vs-ours difference
worth remembering, but the wedge is still best pursued via CPU0-only markers in
`flush_old_exec`/`mmput` (reports 258/§5, 260 §3) rather than more chainloader
or CPU1-startup replication.

## 4. Evidence / limits

* Pseudocode and function inventory: `/tmp/opencode/decompiled.txt`,
  `/tmp/opencode/procs-all.out` (ephemeral, session 64).
* Prior: report 258 (wedge is post-IPI), report 259 (IFS startup order),
  report 254 §3 (SCU_CTRL = 0x531), report 219 §2.2 (startup writes).
* The decompiler could not resolve some blocks (`Removing unreachable block`
  warnings) and `FUN_80107748` decompiled as an infinite loop — those are
  decompiler artifacts on a raw ARM image, not claims about runtime behavior.
  The `cpu_startnext` body is short and unambiguous.
