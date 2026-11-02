# 97 — Region architecture, the switch mechanism, and live USB

Status: design, 2026-10-25. Read-only. Supersedes the A/B assumptions in `85`/`94` for the
factory path. Inputs: `94`, `95`, `96`, `88`, `91`, `69`, `66`, `PLAYBOOK-REFERENCE`.

## 0. The correction: `resetqnxosregion` cannot switch a CRC boot

The factory/CRC path **hardcodes region 0**. In `0x800142A8`:

```
800142b0  bl 0x8000911a        ; selector (RPMB BOOTCTR_FCTRY)
800142ba  cbz r0, 0x800142d0   ; selector == 0 -> product path (reads 0x2074)
800142bc  add r1, sp, #0xf0
800142be  str.w sl, [r7, #8]   ; sl == 0 -> region index := 0
800142c2  mov r0, sb
800142c4  ldm r1, {r1,r2,r3}
800142c6  bl 0x80014148        ; verify region 0 ONCE, then return
```

- Selector `!= 0` (record present, 1..0x32) → **region 0 only**, and `0x2074` is never read.
- Selector `== 0` (record absent) → product path, which **does** read `0x2074` and honours
  `requested_region`, but verifies with **ECDSA** (`0x80014716`), not CRC.

`resetqnxosregion` (and the loader's `0xC0` COMPLETE) writes NVRAM `0x2074`. That is a
**product-path** control. It cannot point the factory/CRC boot at region 1, and it cannot
make the product path accept an unsigned/CRC Linux image. The two modes are mutually
exclusive:

| selector | path | region | verifier |
|---|---|---|---|
| absent (0) | product | `0x2074` requested, then the other | ECDSA |
| 1..0x32 | factory | **region 0 only** | CRC (`os_type==6` + trailer) |

So "Linux in region 0, QNX in region 1, switch with `resetqnxosregion`" cannot work as
stated. Region 1 is **dead weight** to the factory bootloader.

## 1. What does work: a chainloader-managed one-shot + restore

Put a small **CRC chainloader** in region 0 and use region 1 as the payload/backup store.
The chainloader owns the switch, not the bootloader:

```
region 0 IFS  = chainloader (CRC, os_type=6)
region 1      = [backup] sealed TabletOS IFS (0x2BFE10 bytes)
                [payload] Linux kernel + DTB + initramfs
                [flags]   one-shot / restore / attempt counter
```

Boot sequence:

1. Bootloader verifies region 0 (CRC) → chainloader runs.
2. Chainloader kicks WDT2, initialises/uses eMMC, reads the flag block in region 1.
3. **One-shot flag set?** clear it, then boot Linux from region 1.
   **Otherwise?** restore region 0 from the sealed TabletOS backup, clear the attempt state,
   force a warm reset.
4. Before jumping to Linux, stop kicking WDT2. A hang therefore warm-resets in ~58.6 s.
   The next boot sees the flag cleared → restores TabletOS → QNX boots.
5. QNX (or Linux, if it has the driver) reads the breadcrumb rings and decides the next
   experiment.

This gives **automatic fallback and a closed readback loop without kexec**:

- Each Linux attempt is one boot. If it fails, the next boot returns to TabletOS.
- The breadcrumbs at `0x90000000`/mirrors survive the WDT2 warm reset and are read from the
  restored QNX over SSH (`memdump3`).
- The backup is the **sealed CRC** TabletOS image (`ifs0.sealed.bin`), not the original
  ECDSA image, because the bootloader's CRC gate must accept it after restore.
- Region 1 is 512 MB; the backup is 2.75 MB and the kernel/DTB are tens of MB. Plenty.

What it requires:

1. **eMMC read and write from the chainloader.** The bootloader already initialised the MMC
   controller to load the image; its routines are at `0x80000000`-based addresses and are
   still resident. Reusing them (or re-initialising MMC2 `0x480B4000`) is the enabling
   capability.
2. **A flag block both sides can write**: a reserved sector in region 1 (easy for the
   chainloader) is enough; QNX writes it with `dd`, Linux with its MMC driver.
3. **WDT2 control**: kick `0x4A314030` while working; stop before the jump.

This is close to the user's idea, with the switch moved from `0x2074` into the chainloader.

## 2. The other switch: pre-OS reflash via the signed loader

If the signed loader's `0xF7/0xF8` write path covers the OS/IFS region (MCT allow-list,
`88` §5), then `bbusb.py` can rewrite region 0/1 from the bootrom with no OS. That is the
cleanest recovery and would make the chainloader restore optional. This is the most valuable
open question to close: extend `88`'s unknown **U1** from "NVRAM partition" to "OS region /
IFS LBA range in the allow-list". If yes, build the host-side QCFM/LBA writer.

## 3. Live USB

### 3.1 Once Linux is up (straightforward)

Mainline has `musb` and the OMAP4 glue. Add the USB node to `omap4-winchester.dts`, enable
`CONFIG_USB_MUSB_OMAP2PLUS`, and a gadget (`g_serial` for CDC-ACM, or `g_ether` for RNDIS).
The host already has the `0fca` udev rules and PyUSB. This gives a live console/log channel
as soon as the kernel reaches gadget init — no bootrom protocol needed.

Caveat: gadget init is relatively late. Early boot still relies on UART3/rings. The kexec
project's `DEBUG_PLAYBOOK_BC` shows how to put earlyprintk into the rings.

### 3.2 In the chainloader (harder, but feasible)

The OMAP4 bootrom contains a full MUSB-class device driver:

- USB OTG MMIO base **`0x4A0AB000`**, driver `0x4002CE90-0x4002DB92`, reached through the
  per-driver methods for device ID `0x45` (`68`).
- It has FIFO and indexed-window transfer paths and a `0=done, 1=error, 4=continue` status.
- The ROM API table at `0x40028400` (48 Thumb pointers) is the documented entry surface.

Options, in order of effort:

1. **Port the bootrom MUSB driver** into the chainloader (extract from the NS-readable ROM
   dump, reimplement the PHY/clock init). Then expose a minimal vendor bulk IN/OUT with a
   host tool (PyUSB) — live logs and simple commands, no bootrom framing.
2. **Call the ROM API** from the chainloader if the USB driver can be re-entered; needs the
   bootrom's expected context/state and is riskier.
3. **Kernel gadget only** (§3.1) and keep the chainloader on LED/rings.

Live USB in the chainloader is a real project (a few thousand lines at worst), but it is the
only way to watch the pre-kernel stage in real time. Treat it as a separate track with a
dedicated subagent; the bootrom dump and `68` are the reference.

## 4. Recommended work order

1. **Close U1**: is the OS/IFS region writable through the signed loader's `0xF7/0xF8`?
   Static audit + read-only probe. If yes, build the pre-OS reflash tool — that alone makes
   iteration safe.
2. **Prove eMMC read/write from the payload**: identify/reuse the bootloader's MMC routines,
   read a known sector from region 1, write and verify a scratch sector. This enables the
   one-shot + restore design.
3. **Build the chainloader skeleton**: LED stages, breadcrumb rings, WDT2 keepalive, flag
   block read/write, restore path, then Linux load/jump.
4. **First Linux milestone**: kernel + appended DTB from region 1, `maxcpus=1`,
   `earlyprintk` to UART3/rings, one-shot boot with automatic restore.
5. **Kernel USB gadget** for live logs; chainloader USB later if needed.

## 5. Open questions

1. Can the bootloader's MMC read/write routines be called safely from the payload (state,
   DMA, IRQ/FIQ masked)? Or must MMC be re-initialised?
2. Exact WDT2 timeout on this build (`58.6 s` per `kexec/README` #9, `60 s` user-timed).
3. Does the bootloader re-run churn `0x90000000`/mirrors? The survivor band was proven
   across a QNX reboot, not across an hd0 re-run.
4. Region-1 flag sector choice (avoid the RFS partition, which is empty in region 1).
5. Is `0x2074` ever useful again? Only if the counter is ever absent (product/ECDSA mode);
   it is not part of the CRC path.
