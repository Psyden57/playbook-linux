# 77 — `nvram_resource_manager`: complete `NV_OSSTORE` ID→name table, the devctl interface, and what `0x2801` actually is

Status: static analysis complete; **read-only** (no device writes, no binary modification).
Date: 2026-10-23.
Target: `/home/psyden/rea-work/bootdumps-2026-09-11/analysis/playbook-live-2026-10-21/device-binaries/nvram_resource_manager`
ELF32 ARM EABI5, QNX shared object (`PT_INTERP /usr/lib/ldqnx.so.2`), 112 873 bytes,
SHA-256 `b50eeb40 8fa52440 ed385844 1c822e9a 1992d88d c7b9b80a 11c68ace ccfffe3e`
(pulled from `/proc/boot/nvram_resource_manager` on the booted PlayBook).
Build stamp (`QNX_info`, file offset `0x1A364`): `NAME=nvram_resource_manager-winchester`,
`DATE=2011/10/27-19:52:50-EDT`, `VERSION=6.5.0`, `TAGID=317`.

Method: `readelf`/`strings`/Capstone 5 ARM (binutils on this host has no ARM backend),
plus Ghidra 12.1.4 headless auto-analysis + decompiler. Because the ELF carries no
`.text` section header, Ghidra images the file at **vaddr = file offset + 0x10000**;
**every address quoted below is a file offset / true vaddr** (subtract `0x10000` from
Ghidra's `FUN_0001xxxx` names, e.g. Ghidra `FUN_0001b840` = **`0xB840`**).

Companion evidence already in this project: `76-live-0x2801-audit.md` (live bytes),
`75-nvram-2801-password-record.md` (hd0 boot-stage gate), `69-live-bootrom-loader-usb.md`
(5060 SHA-512 iterations), and `device-binaries/libnvram.so.1` (the client shim, whose
export table pins every devctl number to a name — see §4.2).

---

## 0. Verdict (short)

1. **`0x2801` = `NV_OSSTORE_PasswordStruct`.** It is the **device-password verifier
   record** — a 112-byte (0x70) struct holding a version, an iteration count (5060), an
   8-byte salt, a failed-attempt counter + maximum, a nonce, and a **64-byte salted
   digest**. It is a credential store, and it is the record the `hd0` boot stage gates on.
2. **`0x2801` is *not* the PIN.** The PIN is a **different, separate record**:
   `NV_OSSTORE_PIN_NUM` = **`0x2002`**, 4 or 8 bytes, **no salt, no digest, no counter**
   (`0x369C` reads it as a plain integer and publishes it to the `deviceinfo` PPS object as
   `PIN`/`PIN64`). It is the BlackBerry **device PIN identifier**, not an unlock code.
3. **`0x2801` is not a boot/factory counter.** The OS A/B region and update state live in
   **`0x2074 NV_OSSTORE_OS_REGION`** (`requested_region`, `actual_region`, per-region
   `boot_count_*`, `max_boot_count`, `epoch`, version IDs; see `83`). Its `+0x06`
   `boot_count_0` advances by one per successful boot (`0x017B`→`0x017C`), but it is the
   per-region update budget, not a device/factory counter. The factory counter is the RPMB
   `BOOTCTR_FCTRY`.
4. `0x2801` carries **no protection flags** in the resmgr's descriptor table
   (`flags = 0x0000`), is **read/writable** by any client of `/dev/nvram`, and is only
   **CRC-32**-protected on media (no encryption, no MAC). `nv_is_protected(0x2801) = 0`
   and `nv_get_permissions(0x2801) = 0` live, which independently confirms the table.

---

## 1. The ID→name table: location, record format, and how it was proven

### 1.1 Location and shape

| property | value |
|---|---|
| table start (file offset = vaddr) | **`0x12D54`** |
| entry size | **`0x38` (56) bytes** |
| entry count | **288** (`0x12D54 + 288*0x38 = 0x16C54`, first byte after the last entry is the string `FSEC` @ `0x16C28`… i.e. the table is exactly bounded) |
| entry layout | `+0x00` u16 **record ID**; `+0x02` u16 **flags/permissions**; `+0x04` u8 reserved (always `0x00`); `+0x05` NUL-terminated **name** (max 51 chars in this build) |
| termination | none inside the table; lookups test for `0xFFFF` **and** for the 331-slot runtime hash (§5), so the table is indexed, never walked to a sentinel |

Note the ID is a **16-bit** field — that is consistent with everything else in the binary
(the devctl payload carries the ID in 16 bits, `ldrh r0,[r8,#4]` at `0xB9F0`/`0xBA00`/…,
and the runtime hash key is `id % 0x14B`).

`0x2801` therefore sits at table offset **`0x14A6C`** (`01 28 00 00 | 00 | "NV_OSSTORE_PasswordStruct_NUM"`),
exactly as the earlier note said — but the ID is the u32-truncated-to-u16 at `0x14A6C`, and
the byte at `0x14A70` is the reserved byte, not part of the ID.

### 1.2 Proof that the table is (id, flags, name) and not something else

* **`0x63B4`** (`nv_get_permissions`): scans the table with `puVar2 += 0x1C` **halfwords**
  (= `0x38` bytes), compares `*puVar2` (u16 @ +0) to the requested id, and returns
  **`puVar2[1]`** (u16 @ +2) — i.e. the flags word.
* **`0x16D2C`**: linear scan with `iVar2 += 0x38`, `if (*puVar1 == 0xFFFF) → not found`,
  and then **`memcpy(dst, base + i*0x38, 0x38)`** — copies the whole 56-byte descriptor.
* **`0x1641C`** (`nv_is_protected`): `(flags & 1) && <device-secure global>`.
* Live cross-check from §76: `get_permissions(0x2801)` = **0** and `is_protected(0x2801)` = **0**,
  while the table row for `0x2801` has `flags = 0x0000`. ✔

### 1.3 Flags word semantics (as actually tested by the code)

| bit | meaning | tested at |
|---|---|---|
| `0x0001` | **protected**: write *and* delete are refused (returns `0xFFFFFFEF`) while the device-secure global (`0x1B4E8`) is non-zero. Read is still allowed. | `0x9BE0` (`and #1`), `0x9E7C`→`0x1641C`, `0x1641C` |
| `0x0002` | unknown / not tested anywhere in this binary; present only on `NV_OSSTORE_bsn_NUM` (`0x280F`, flags `0x0003`) |
| `0x0004` | **inaccessible**: both read and write refuse — read logs `"Encrypted records not currently supported"` (`0x1727C`) and returns 0; write logs the same and returns `0xFFFFFFF6`. Present only on `NV_CAL_SIM_PIN` (`0x301E`, flags `0x0005`). | `0x18FF0`, `0x9BE0` |

So the resmgr has **no encryption at all** — bit 2 is a hard "unsupported" marker, not a
crypto path.

---

## 2. COMPLETE ID→name table (288 entries, file offsets given per row)

Byte-for-byte extracted from `0x12D54`, 288 × 56 bytes. `flags` is the permissions word
described in §1.3. IDs `0x2000–0x207C` and `0x2800–0x284B` are the two `NV_OSSTORE` banks;
the gaps (`0x27FF`, `0x2FFF`, `0x10FF`, `0x1FFF`, `0x4FFF`, `0x6FFF`) are the per-bank
`…_LAST_NUM` sentinels.

| # | ID | flags | name | table offset |
|---|----|-------|------|--------------|
| 0 | 0x1000 | 0x0000 | `NV_CAL_NUM` | 0x12D54 |
| 1 | 0x10FF | 0x0000 | `NV_CAL_LAST_NUM` | 0x12D8C |
| 2 | 0x1100 | 0x0000 | `NV_GPRS_NUM` | 0x12DC4 |
| 3 | 0x1101 | 0x0000 | `NV_GPRS_PlmnNameOverrideFirstEntry_NUM` | 0x12DFC |
| 4 | 0x1164 | 0x0000 | `NV_GPRS_PlmnNameOverrideLastEntry_NUM` | 0x12E34 |
| 5 | 0x1FFF | 0x0000 | `NV_GPRS_LAST_NUM` | 0x12E6C |
| 6 | 0x2000 | 0x0000 | `NV_OSSTORE_NUM` | 0x12EA4 |
| 7 | 0x2001 | 0x0000 | `NV_OSSTORE_Version_NUM` | 0x12EDC |
| 8 | 0x2002 | 0x0001 | `NV_OSSTORE_PIN_NUM` | 0x12F14 |
| 9 | 0x2003 | 0x0000 | `NV_OSSTORE_LcdCal_NUM` | 0x12F4C |
| 10 | 0x2004 | 0x0000 | `NV_OSSTORE_JvmStart_NUM` | 0x12F84 |
| 11 | 0x2005 | 0x0000 | `NV_OSSTORE_CalSectorChecksum_NUM` | 0x12FBC |
| 12 | 0x2006 | 0x0000 | `NV_OSSTORE_CalTestStatus_NUM` | 0x12FF4 |
| 13 | 0x2007 | 0x0000 | `NV_OSSTORE_LocalInfo_NUM` | 0x1302C |
| 14 | 0x2008 | 0x0001 | `NV_OSSTORE_MepData_NUM` | 0x13064 |
| 15 | 0x2009 | 0x0000 | `NV_OSSTORE_DevRandomNumber_NUM` | 0x1309C |
| 16 | 0x200A | 0x0000 | `NV_OSSTORE_DevProfileTable_NUM` | 0x130D4 |
| 17 | 0x200B | 0x0001 | `NV_OSSTORE_IdenRadioBsn_NUM` | 0x1310C |
| 18 | 0x200C | 0x0001 | `NV_OSSTORE_IdenIMEI_NUM` | 0x13144 |
| 19 | 0x200D | 0x0000 | `NV_OSSTORE_OnTime_NUM` | 0x1317C |
| 20 | 0x200E | 0x0000 | `NV_OSSTORE_GprsRadioFeatures_NUM` | 0x131B4 |
| 21 | 0x200F | 0x0000 | `NV_OSSTORE_WLANMain_NUM` | 0x131EC |
| 22 | 0x2010 | 0x0001 | `NV_OSSTORE_Bluetooth_NUM` | 0x13224 |
| 23 | 0x2011 | 0x0000 | `NV_OSSTORE_TestMode_NUM` | 0x1325C |
| 24 | 0x2012 | 0x0000 | `NV_OSSTORE_BaryonSuperTunnel_NUM` | 0x13294 |
| 25 | 0x2013 | 0x0000 | `NV_OSSTORE_PatriotLoadState_NUM` | 0x132CC |
| 26 | 0x2014 | 0x0000 | `NV_OSSTORE_RtcBackup_NUM` | 0x13304 |
| 27 | 0x2015 | 0x0000 | `NV_OSSTORE_UtcBackup_NUM` | 0x1333C |
| 28 | 0x2016 | 0x0001 | `NV_OSSTORE_SerialCalMode_NUM` | 0x13374 |
| 29 | 0x2017 | 0x0001 | `NV_OSSTORE_Bluetooth_CAL_NUM` | 0x133AC |
| 30 | 0x2018 | 0x0000 | `NV_OSSTORE_LcdAutoDetect_NUM` | 0x133E4 |
| 31 | 0x2019 | 0x0000 | `NV_OSSTORE_BitFlags_NUM` | 0x1341C |
| 32 | 0x201A | 0x0000 | `NV_OSSTORE_RngState_NUM` | 0x13454 |
| 33 | 0x201B | 0x0000 | `NV_OSSTORE_unused2_NUM` | 0x1348C |
| 34 | 0x201C | 0x0000 | `NV_OSSTORE_unused3_NUM` | 0x134C4 |
| 35 | 0x201D | 0x0000 | `NV_OSSTORE_DSVL_DUID_INFO_NUM` | 0x134FC |
| 36 | 0x201E | 0x0000 | `NV_OSSTORE_WLANRCI_NUM` | 0x13534 |
| 37 | 0x201F | 0x0000 | `NV_OSSTORE_WLANNVRADIO_NUM` | 0x1356C |
| 38 | 0x2020 | 0x0001 | `NV_OSSTORE_PM_VAMP_NUM` | 0x135A4 |
| 39 | 0x2021 | 0x0001 | `NV_OSSTORE_PM_1_2VINTREF_NUM` | 0x135DC |
| 40 | 0x2022 | 0x0000 | `NV_OSSTORE_LcdBacklightCaledVal_NUM` | 0x13614 |
| 41 | 0x2023 | 0x0000 | `NV_OSSTORE_KeypadBacklightCaledVal_NUM` | 0x1364C |
| 42 | 0x2024 | 0x0000 | `NV_OSSTORE_LcdBacklightTimeoutCaledVal_NUM` | 0x13684 |
| 43 | 0x2025 | 0x0000 | `NV_OSSTORE_KeypadBacklightTimeoutCaledVal_NUM` | 0x136BC |
| 44 | 0x2026 | 0x0001 | `NV_OSSTORE_CalibrationOSinfo_NUM` | 0x136F4 |
| 45 | 0x2027 | 0x0000 | `NV_OSSTORE_HeadInSand_NUM` | 0x1372C |
| 46 | 0x2028 | 0x0000 | `NV_OSSTORE_BackupMCT_NUM` | 0x13764 |
| 47 | 0x2029 | 0x0000 | `NV_OSSTORE_SWPartsList_NUM` | 0x1379C |
| 48 | 0x202A | 0x0000 | `NV_OSSTORE_IdenFlags_NUM` | 0x137D4 |
| 49 | 0x202B | 0x0000 | `NV_OSSTORE_AVP_NUM` | 0x1380C |
| 50 | 0x202C | 0x0000 | `NV_OSSTORE_MFG_ERROR_NUM` | 0x13844 |
| 51 | 0x202D | 0x0000 | `NV_OSSTORE_InteractiveTestSel_NUM` | 0x1387C |
| 52 | 0x202E | 0x0000 | `NV_OSSTORE_bkltModeD_NUM` | 0x138B4 |
| 53 | 0x202F | 0x0000 | `NV_OSSTORE_LcdWriteReadTestLog_NUM` | 0x138EC |
| 54 | 0x2030 | 0x0000 | `NV_OSSTORE_CommonSTPCommandStatus_NUM` | 0x13924 |
| 55 | 0x2031 | 0x0000 | `NV_OSSTORE_CryptoBatteryInfoCache_NUM` | 0x1395C |
| 56 | 0x2032 | 0x0000 | `NV_OSSTORE_unused5_NUM` | 0x13994 |
| 57 | 0x2033 | 0x0000 | `NV_OSSTORE_CFP_LAL_NUM` | 0x139CC |
| 58 | 0x2034 | 0x0000 | `NV_OSSTORE_USB_NUM` | 0x13A04 |
| 59 | 0x2035 | 0x0000 | `NV_OSSTORE_CHARGER_TEMP_CAL_NUM` | 0x13A3C |
| 60 | 0x2036 | 0x0000 | `NV_OSSTORE_LOG_VERBOSITY_NUM` | 0x13A74 |
| 61 | 0x2037 | 0x0000 | `NV_OSSTORE_SYS_PROFILE_ENABLE_NUM` | 0x13AAC |
| 62 | 0x2038 | 0x0000 | `NV_OSSTORE_InstallerOSSize_NUM` | 0x13AE4 |
| 63 | 0x2039 | 0x0000 | `NV_OSSTORE_DefaultGancInformation` | 0x13B1C |
| 64 | 0x203A | 0x0000 | `NV_OSSTORE_TCXO_TEMP_CAL_NUM` | 0x13B54 |
| 65 | 0x203B | 0x0000 | `NV_OSSTORE_BATTERY_TEMP_CAL_NUM` | 0x13B8C |
| 66 | 0x203C | 0x0000 | `NV_OSSTORE_InteractiveTestResults_NUM` | 0x13BC4 |
| 67 | 0x203D | 0x0000 | `NV_OSSTORE_appstore_NUM` | 0x13BFC |
| 68 | 0x203E | 0x0000 | `NV_OSSTORE_branding_NUM` | 0x13C34 |
| 69 | 0x203F | 0x0000 | `NV_OSSTORE_CamAutoDetect_NUM` | 0x13C6C |
| 70 | 0x2040 | 0x0000 | `NV_OSSTORE_NukeStatus_NUM` | 0x13CA4 |
| 71 | 0x2041 | 0x0000 | `NV_OSSTORE_GpsAlmanac_NUM` | 0x13CDC |
| 72 | 0x2042 | 0x0000 | `NV_OSSTORE_GpsAdc_NUM` | 0x13D14 |
| 73 | 0x2043 | 0x0000 | `NV_OSSTORE_AudioTrim_NUM` | 0x13D4C |
| 74 | 0x2044 | 0x0000 | `NV_OSSTORE_USB_COPY_NUM` | 0x13D84 |
| 75 | 0x2045 | 0x0000 | `NV_OSSTORE_LegacyBacklightSettings_NUM` | 0x13DBC |
| 76 | 0x2046 | 0x0000 | `NV_OSSTORE_AudioLogging_NUM` | 0x13DF4 |
| 77 | 0x2047 | 0x0000 | `NV_ORPHEUS_3G_OVERRIDE` | 0x13E2C |
| 78 | 0x2048 | 0x0000 | `NV_ORPHEUS_3G_NETPAR_NUM` | 0x13E64 |
| 79 | 0x2049 | 0x0000 | `NV_OSSTORE_UtcTimeOffset_NUM` | 0x13E9C |
| 80 | 0x204A | 0x0000 | `NV_OSSTORE_brandingMulti_NUM` | 0x13ED4 |
| 81 | 0x204B | 0x0000 | `NV_OSSTORE_brandingMulti_NUM_2` | 0x13F0C |
| 82 | 0x204C | 0x0000 | `NV_OSSTORE_brandingMulti_NUM_3` | 0x13F44 |
| 83 | 0x204D | 0x0000 | `NV_OSSTORE_brandingMulti_SPARE_1` | 0x13F7C |
| 84 | 0x204E | 0x0000 | `NV_OSSTORE_brandingMulti_SPARE_2` | 0x13FB4 |
| 85 | 0x204F | 0x0000 | `NV_OSSTORE_brandingMulti_SPARE_3` | 0x13FEC |
| 86 | 0x2050 | 0x0000 | `NV_OSSTORE_NVR_CONDITIONAL_WRITE_NUM` | 0x14024 |
| 87 | 0x2051 | 0x0000 | `NV_OSSTORE_CamIspVersionInfo_NUM` | 0x1405C |
| 88 | 0x2052 | 0x0000 | `NV_OSSTORE_OpenVGStatistics_NUM` | 0x14094 |
| 89 | 0x2053 | 0x0000 | `NV_OSSTORE_StartupTime_NUM` | 0x140CC |
| 90 | 0x2054 | 0x0000 | `NV_OSSTORE_WLANApps_NUM` | 0x14104 |
| 91 | 0x2055 | 0x0000 | `NV_OSSTORE_Iden_Data_Off` | 0x1413C |
| 92 | 0x2056 | 0x0000 | `NV_OSSTORE_CfpVersion_NUM` | 0x14174 |
| 93 | 0x2057 | 0x0000 | `NV_OSSTORE_BacklightSettings_NUM` | 0x141AC |
| 94 | 0x2058 | 0x0000 | `NV_OSSTORE_CfpBugDispLog_NUM` | 0x141E4 |
| 95 | 0x2059 | 0x0000 | `NV_OSSTORE_PROFILE_DATA` | 0x1421C |
| 96 | 0x205A | 0x0000 | `NV_OSSTORE_HW_ID_OVERRIDE_NUM` | 0x14254 |
| 97 | 0x205B | 0x0000 | `NV_OSSTORE_VCLICK_TRIM_NUM` | 0x1428C |
| 98 | 0x205C | 0x0000 | `NV_OSSTORE_AccelOrientationOverride_NUM` | 0x142C4 |
| 99 | 0x205D | 0x0000 | `NV_OSSTORE_RimCryptKeyStorage_NUM` | 0x142FC |
| 100 | 0x205E | 0x0000 | `NV_OSSTORE_OMADM_Info_NUM` | 0x14334 |
| 101 | 0x205F | 0x0000 | `NV_OSSTORE_AccelOrientationAlgorithmOverride_NUM` | 0x1436C |
| 102 | 0x2060 | 0x0000 | `NV_OSSTORE_SDBugdispLogMaxLogCount` | 0x143A4 |
| 103 | 0x2061 | 0x0000 | `NV_OSSTORE_MFG_PERSISTENT_DATA` | 0x143DC |
| 104 | 0x2062 | 0x0000 | `NV_OSSTORE_AccelDSPGestureSettings_NUM` | 0x14414 |
| 105 | 0x2063 | 0x0000 | `NV_OSSTORE_BqHugeDefault_NUM` | 0x1444C |
| 106 | 0x2064 | 0x0000 | `NV_OSSTORE_Battery_ID_NUM` | 0x14484 |
| 107 | 0x2065 | 0x0000 | `NV_OSSTORE_ProxSensorCal_NUM` | 0x144BC |
| 108 | 0x2066 | 0x0000 | `NV_OSSTORE_HSJackState_NUM` | 0x144F4 |
| 109 | 0x2067 | 0x0000 | `NV_OSSTORE_WONDERWALL_TRIM_NUM` | 0x1452C |
| 110 | 0x2068 | 0x0000 | `NV_OSSTORE_RADIO_STORED_CELL_INFO` | 0x14564 |
| 111 | 0x2069 | 0x0000 | `NV_OSSTORE_SDBugdispLogMaxFilesize` | 0x1459C |
| 112 | 0x206A | 0x0000 | `NV_OSSTORE_RPLMN_RAT` | 0x145D4 |
| 113 | 0x206B | 0x0000 | `NV_OSSTORE_SIM_DATA` | 0x1460C |
| 114 | 0x206C | 0x0000 | `NV_OSSTORE_OTASL_MCT_BACKUP` | 0x14644 |
| 115 | 0x206D | 0x0000 | `NV_OSSTORE_VOLT_RESIST_LOOKUP_TABLE` | 0x1467C |
| 116 | 0x206E | 0x0000 | `NV_OSSTORE_BATTERY_KILL_SWITCH_COUNT` | 0x146B4 |
| 117 | 0x206F | 0x0000 | `NV_OSSTORE_GPS_INTERNAL` | 0x146EC |
| 118 | 0x2070 | 0x0000 | `NV_OSSTORE_GPS_VENDOR_1` | 0x14724 |
| 119 | 0x2071 | 0x0000 | `NV_OSSTORE_GPS_VENDOR_2` | 0x1475C |
| 120 | 0x2072 | 0x0000 | `NV_OSSTORE_BATTERY_MONITORING_FLAG` | 0x14794 |
| 121 | 0x2073 | 0x0000 | `NV_OSSTORE_RADIO_3GPP_CONFIGURATIONS` | 0x147CC |
| 122 | 0x2074 | 0x0000 | `NV_OSSTORE_OS_REGION` | 0x14804 |
| 123 | 0x2075 | 0x0000 | `NV_OSSTORE_BOOT_MODE_BLOCK` | 0x1483C |
| 124 | 0x2076 | 0x0000 | `NV_OSSTORE_CELL_BROADCAST_SETTING` | 0x14874 |
| 125 | 0x2077 | 0x0000 | `NV_OSSTORE_NETUSB_CONFIG` | 0x148AC |
| 126 | 0x2078 | 0x0000 | `NV_OSSTORE_HDCP_KEYS` | 0x148E4 |
| 127 | 0x2079 | 0x0000 | `NV_OSSTORE_MAC_ADDR` | 0x1491C |
| 128 | 0x207A | 0x0000 | `NV_OSSTORE_MFG_SERIAL` | 0x14954 |
| 129 | 0x207B | 0x0000 | `NV_OSSTORE_CalTestStatus2_NUM` | 0x1498C |
| 130 | 0x207C | 0x0000 | `NV_OSSTORE_EXTRA_REGION` | 0x149C4 |
| 131 | 0x27FF | 0x0000 | `NV_OSSTORE_LAST_NUM` | 0x149FC |
| 132 | 0x2800 | 0x0000 | `NV_OSSTORE_PERSISTENT_FIRST_NUM` | 0x14A34 |
| 133 | 0x2801 | 0x0000 | `NV_OSSTORE_PasswordStruct_NUM` | 0x14A6C |
| 134 | 0x2802 | 0x0000 | `NV_OSSTORE_PasswordStructReserve_NUM` | 0x14AA4 |
| 135 | 0x2803 | 0x0000 | `NV_OSSTORE_WLAN_EEPROM` | 0x14ADC |
| 136 | 0x2804 | 0x0001 | `NV_OSSTORE_JANUS_AFE_NUM` | 0x14B14 |
| 137 | 0x2805 | 0x0000 | `NV_OSSTORE_JANUS_L1CONTROL_NUM` | 0x14B4C |
| 138 | 0x2806 | 0x0000 | `NV_OSSTORE_JANUS_MSR_NUM` | 0x14B84 |
| 139 | 0x2807 | 0x0001 | `NV_OSSTORE_JANUS_RF_CALIB_NUM` | 0x14BBC |
| 140 | 0x2808 | 0x0000 | `NV_OSSTORE_JANUS_RF_TYPE_NUM` | 0x14BF4 |
| 141 | 0x2809 | 0x0001 | `NV_OSSTORE_CryptoBatteryChallenge_NUM` | 0x14C2C |
| 142 | 0x280A | 0x0000 | `NV_OSSTORE_JANUS_AFC_DAC_NUM` | 0x14C64 |
| 143 | 0x280B | 0x0000 | `NV_OSSTORE_JANUS_PRE_PRODUCT_NUM` | 0x14C9C |
| 144 | 0x280C | 0x0001 | `NV_OSSTORE_JANUS_RF_ANALYSIS_NUM` | 0x14CD4 |
| 145 | 0x280D | 0x0001 | `NV_OSSTORE_PcbBarcode_NUM` | 0x14D0C |
| 146 | 0x280E | 0x0001 | `NV_OSSTORE_WLAN_TI_CAL_NUM` | 0x14D44 |
| 147 | 0x280F | 0x0003 | `NV_OSSTORE_bsn_NUM` | 0x14D7C |
| 148 | 0x2810 | 0x0001 | `NV_OSSTORE_AntiCloneData_NUM` | 0x14DB4 |
| 149 | 0x2811 | 0x0000 | `NV_OSSTORE_JANUS_RX_DP_CAL_NUM` | 0x14DEC |
| 150 | 0x2812 | 0x0001 | `NV_OSSTORE_Nvram_Signatures` | 0x14E24 |
| 151 | 0x2813 | 0x0001 | `NV_OSSTORE_JANUS_RF_RIM_NUM` | 0x14E5C |
| 152 | 0x2814 | 0x0000 | `NV_OSSTORE_PersistentBitFlags_NUM` | 0x14E94 |
| 153 | 0x2815 | 0x0000 | `NV_OSSTORE_PatriotDigest_NUM` | 0x14ECC |
| 154 | 0x2816 | 0x0000 | `NV_OSSTORE_NANDEraseCountStats_NUM` | 0x14F04 |
| 155 | 0x2817 | 0x0000 | `NV_OSSTORE_FgUpdateStatus_NUM` | 0x14F3C |
| 156 | 0x2818 | 0x0000 | `NV_OSSTORE_RefurbishedTime_NUM` | 0x14F74 |
| 157 | 0x2819 | 0x0001 | `NV_OSSTORE_NotSupportedOS_NUM` | 0x14FAC |
| 158 | 0x281A | 0x0000 | `NV_OSSTORE_TavorPVUsbCal_NUM` | 0x14FE4 |
| 159 | 0x281B | 0x0000 | `NV_OSSTORE_SecureRecordsHash_NUM` | 0x1501C |
| 160 | 0x281C | 0x0000 | `NV_OSSTORE_AccelZeroGOffset_NUM` | 0x15054 |
| 161 | 0x281D | 0x0000 | `NV_OSSTORE_RimCryptPersistentKeyStorage_NUM` | 0x1508C |
| 162 | 0x281E | 0x0000 | `NV_OSSTORE_L3_Configuration_NUM` | 0x150C4 |
| 163 | 0x281F | 0x0000 | `NV_OSSTORE_RAMDumpConfiguration_NUM` | 0x150FC |
| 164 | 0x2820 | 0x0000 | `NV_OSSTORE_BatterySerial_NUM` | 0x15134 |
| 165 | 0x2821 | 0x0000 | `NV_OSSTORE_ResetInfo_NUM` | 0x1516C |
| 166 | 0x2822 | 0x0000 | `NV_OSSTORE_QC_NV_Checksum_Storage_NUM` | 0x151A4 |
| 167 | 0x2823 | 0x0000 | `NV_OSSTORE_MMCBootInfo_NUM` | 0x151DC |
| 168 | 0x2824 | 0x0000 | `NV_OSSTORE_UMPPagePoolSize_NUM` | 0x15214 |
| 169 | 0x2825 | 0x0000 | `NV_OSSTORE_MMCStats_NUM` | 0x1524C |
| 170 | 0x2826 | 0x0001 | `NV_OSSTORE_SUE2_MICROCODE` | 0x15284 |
| 171 | 0x2827 | 0x0001 | `NV_OSSTORE_SUE2_PREBOOT` | 0x152BC |
| 172 | 0x2828 | 0x0001 | `NV_OSSTORE_SUE2_CALIB` | 0x152F4 |
| 173 | 0x2829 | 0x0001 | `NV_OSSTORE_SUE2_SETUP` | 0x1532C |
| 174 | 0x282A | 0x0001 | `NV_OSSTORE_SUE2_PARAMS` | 0x15364 |
| 175 | 0x282B | 0x0000 | `NV_OSSTORE_MAG_MFG_CAL` | 0x1539C |
| 176 | 0x282C | 0x0000 | `NV_OSSTORE_OndevQXDMConfiguration_NUM` | 0x153D4 |
| 177 | 0x282D | 0x0000 | `NV_OSSTORE_OndevQXDMMaskIdx_NUM` | 0x1540C |
| 178 | 0x282E | 0x0000 | `NV_OSSTORE_JANUS_AFC_DAC_NEW_NUM` | 0x15444 |
| 179 | 0x282F | 0x0000 | `NV_OSSTORE_AccelTransformInfo_NUM` | 0x1547C |
| 180 | 0x2830 | 0x0001 | `NV_OSSTORE_UMTS_AFC_PARAMS` | 0x154B4 |
| 181 | 0x2831 | 0x0000 | `NV_OSSTORE_MAG_DSP_CAL` | 0x154EC |
| 182 | 0x2832 | 0x0001 | `NV_OSSTORE_UMTS_APC_PARAMS` | 0x15524 |
| 183 | 0x2833 | 0x0001 | `NV_OSSTORE_UMTS_LLDR_INIT_PARAMS` | 0x1555C |
| 184 | 0x2834 | 0x0001 | `NV_OSSTORE_CryptoBatteryCrPairs_NUM` | 0x15594 |
| 185 | 0x2835 | 0x0001 | `NV_OSSTORE_SUE2_CAL_TABLE0` | 0x155CC |
| 186 | 0x2836 | 0x0001 | `NV_OSSTORE_SUE2_CAL_TABLE1` | 0x15604 |
| 187 | 0x2837 | 0x0001 | `NV_OSSTORE_SUE2_CAL_TABLE2` | 0x1563C |
| 188 | 0x2838 | 0x0001 | `NV_OSSTORE_SUE2_CAL_TABLE3` | 0x15674 |
| 189 | 0x2839 | 0x0001 | `NV_OSSTORE_SUE2_CAL_TABLE4` | 0x156AC |
| 190 | 0x283A | 0x0000 | `NV_OSSTORE_CryptoBatteryFailedPairIndex_NUM` | 0x156E4 |
| 191 | 0x283B | 0x0001 | `NV_OSSTORE_UMTS_BAND_FILTER` | 0x1571C |
| 192 | 0x283C | 0x0000 | `NV_OSSTORE_MEID` | 0x15754 |
| 193 | 0x283D | 0x0000 | `NV_OSSTORE_ESN` | 0x1578C |
| 194 | 0x283E | 0x0001 | `NV_OSSTORE_RIM_DIGRF3_BAND0` | 0x157C4 |
| 195 | 0x283F | 0x0001 | `NV_OSSTORE_RIM_DIGRF3_BAND1` | 0x157FC |
| 196 | 0x2840 | 0x0001 | `NV_OSSTORE_RIM_DIGRF3_BAND2` | 0x15834 |
| 197 | 0x2841 | 0x0001 | `NV_OSSTORE_RIM_DIGRF3_BAND3` | 0x1586C |
| 198 | 0x2842 | 0x0001 | `NV_OSSTORE_RIM_DIGRF3_BAND4` | 0x158A4 |
| 199 | 0x2843 | 0x0000 | `NV_OSSTORE_WIMAX_ROOTCA2_CERT` | 0x158DC |
| 200 | 0x2844 | 0x0000 | `NV_OSSTORE_WIMAX_SUBCA_CERT` | 0x15914 |
| 201 | 0x2845 | 0x0000 | `NV_OSSTORE_WIMAX_DEV_CERT` | 0x1594C |
| 202 | 0x2846 | 0x0000 | `NV_OSSTORE_WIMAX_DEV_PKEY` | 0x15984 |
| 203 | 0x2849 | 0x0000 | `NV_OSSTORE_STE_CALIBDATA` | 0x159BC |
| 204 | 0x284A | 0x0000 | `NV_OSSTORE_STE_COPS` | 0x159F4 |
| 205 | 0x284B | 0x0000 | `NV_OSSTORE_STE_GDFS` | 0x15A2C |
| 206 | 0x2FFF | 0x0000 | `NV_OSSTORE_PERSISTENT_LAST_NUM` | 0x15A64 |
| 207 | 0x6000 | 0x0000 | `NV_OVERLAY_NUM` | 0x15A9C |
| 208 | 0x6001 | 0x0000 | `NV_OVERLAY_Hourglass_Frame1_NUM` | 0x15AD4 |
| 209 | 0x6002 | 0x0000 | `NV_OVERLAY_Hourglass_Frame2_NUM` | 0x15B0C |
| 210 | 0x6003 | 0x0000 | `NV_OVERLAY_Hourglass_Frame3_NUM` | 0x15B44 |
| 211 | 0x6004 | 0x0000 | `NV_OVERLAY_Hourglass_Frame4_NUM` | 0x15B7C |
| 212 | 0x6005 | 0x0000 | `NV_OVERLAY_Hourglass_Frame5_NUM` | 0x15BB4 |
| 213 | 0x6006 | 0x0000 | `NV_OVERLAY_Hourglass_Frame6_NUM` | 0x15BEC |
| 214 | 0x6007 | 0x0000 | `NV_OVERLAY_Hourglass_Frame7_NUM` | 0x15C24 |
| 215 | 0x6008 | 0x0000 | `NV_OVERLAY_Hourglass_Frame8_NUM` | 0x15C5C |
| 216 | 0x6009 | 0x0000 | `NV_OVERLAY_Hourglass_Frame9_NUM` | 0x15C94 |
| 217 | 0x600A | 0x0000 | `NV_OVERLAY_Hourglass_Frame10_NUM` | 0x15CCC |
| 218 | 0x600B | 0x0000 | `NV_OVERLAY_Hourglass_Frame11_NUM` | 0x15D04 |
| 219 | 0x600C | 0x0000 | `NV_OVERLAY_Hourglass_Frame12_NUM` | 0x15D3C |
| 220 | 0x600D | 0x0000 | `NV_OVERLAY_Hourglass_Frame13_NUM` | 0x15D74 |
| 221 | 0x600E | 0x0000 | `NV_OVERLAY_Hourglass_Frame14_NUM` | 0x15DAC |
| 222 | 0x600F | 0x0000 | `NV_OVERLAY_Hourglass_Frame15_NUM` | 0x15DE4 |
| 223 | 0x6010 | 0x0000 | `NV_OVERLAY_Hourglass_Frame16_NUM` | 0x15E1C |
| 224 | 0x6011 | 0x0000 | `NV_OVERLAY_RxIcon_DEPRECATED_NUM` | 0x15E54 |
| 225 | 0x6012 | 0x0000 | `NV_OVERLAY_TxRxIcon_DEPRECATED_NUM` | 0x15E8C |
| 226 | 0x6013 | 0x0000 | `NV_OVERLAY_NumIcon_NUM` | 0x15EC4 |
| 227 | 0x6014 | 0x0000 | `NV_OVERLAY_AltIcon_NUM` | 0x15EFC |
| 228 | 0x6015 | 0x0000 | `NV_OVERLAY_CapIcon_NUM` | 0x15F34 |
| 229 | 0x6016 | 0x0000 | `NV_OVERLAY_TxIcon_DEPRECATED_NUM` | 0x15F6C |
| 230 | 0x6017 | 0x0000 | `NV_OVERLAY_ThemeableImgAttrib_DEPRECATED_NUM` | 0x15FA4 |
| 231 | 0x6018 | 0x0000 | `NV_OVERLAY_TxRachIcon_DEPRECATED_NUM` | 0x15FDC |
| 232 | 0x6019 | 0x0000 | `NV_OVERLAY_Hourglass_Header_NUM` | 0x16014 |
| 233 | 0x601A | 0x0000 | `NV_OVERLAY_TxIcon_Header_DEPRECATED_NUM` | 0x1604C |
| 234 | 0x601B | 0x0000 | `NV_OVERLAY_TxRachIcon_Header_DEPRECATED_NUM` | 0x16084 |
| 235 | 0x601C | 0x0000 | `NV_OVERLAY_RxIcon_Header_DEPRECATED_NUM` | 0x160BC |
| 236 | 0x601D | 0x0000 | `NV_OVERLAY_TxRxIcon_Header_DEPRECATED_NUM` | 0x160F4 |
| 237 | 0x601E | 0x0000 | `NV_OVERLAY_NumIcon_Header_NUM` | 0x1612C |
| 238 | 0x601F | 0x0000 | `NV_OVERLAY_AltIcon_Header_NUM` | 0x16164 |
| 239 | 0x6020 | 0x0000 | `NV_OVERLAY_CapIcon_Header_NUM` | 0x1619C |
| 240 | 0x6021 | 0x0000 | `NV_OVERLAY_MultiTapIcon_DEPRECATED_NUM` | 0x161D4 |
| 241 | 0x6022 | 0x0000 | `NV_OVERLAY_MultiTapIcon_Header_DEPRECATED_NUM` | 0x1620C |
| 242 | 0x6FFF | 0x0000 | `NV_OVERLAY_LAST_NUM` | 0x16244 |
| 243 | 0x3000 | 0x0000 | `NV_CAL_IMEI` | 0x1627C |
| 244 | 0x3001 | 0x0001 | `NV_CAL_RAMPFACTORTABLE` | 0x162B4 |
| 245 | 0x3002 | 0x0000 | `UNUSED_NV_CAL_GAIN_OFFSETS` | 0x162EC |
| 246 | 0x3003 | 0x0001 | `NV_CAL_RX_COMPENSATION` | 0x16324 |
| 247 | 0x3004 | 0x0000 | `NV_CAL_DSP_AUDIO_DATA` | 0x1635C |
| 248 | 0x3005 | 0x0000 | `NV_CAL_VOLUME_DATA` | 0x16394 |
| 249 | 0x3006 | 0x0001 | `NV_CAL_AFC_DATA` | 0x163CC |
| 250 | 0x3007 | 0x0001 | `NV_CAL_BAND_DATA` | 0x16404 |
| 251 | 0x3008 | 0x0001 | `NV_CAL_RAMPVALUETABLE` | 0x1643C |
| 252 | 0x3009 | 0x0001 | `NV_CAL_GAIN_OFFSETS` | 0x16474 |
| 253 | 0x300A | 0x0001 | `NV_CAL_GAIN_WORDS` | 0x164AC |
| 254 | 0x300B | 0x0001 | `NV_CAL_PREDISTORTION_COEFFICIENTS_850` | 0x164E4 |
| 255 | 0x300C | 0x0001 | `NV_CAL_PREDISTORTION_COEFFICIENTS_900` | 0x1651C |
| 256 | 0x300D | 0x0001 | `NV_CAL_PREDISTORTION_COEFFICIENTS_1800` | 0x16554 |
| 257 | 0x300E | 0x0001 | `NV_CAL_PREDISTORTION_COEFFICIENTS_1900` | 0x1658C |
| 258 | 0x300F | 0x0001 | `NV_CAL_I_Q_IMBALANCE` | 0x165C4 |
| 259 | 0x3010 | 0x0001 | `NV_CAL_RAMPFACTORTABLE_8PSK` | 0x165FC |
| 260 | 0x3011 | 0x0001 | `NV_CAL_FREESCALE_REGS` | 0x16634 |
| 261 | 0x3012 | 0x0001 | `NV_CAL_FREESCALE_PAC_MAX` | 0x1666C |
| 262 | 0x3013 | 0x0001 | `NV_CAL_FREESCALE_RF_PA_REGS` | 0x166A4 |
| 263 | 0x3014 | 0x0001 | `NV_CAL_RENESAS_B6PLD_CAL` | 0x166DC |
| 264 | 0x3015 | 0x0001 | `NV_CAL_INFINEON_RF_REGS` | 0x16714 |
| 265 | 0x3016 | 0x0001 | `NV_CAL_INFINEON_RF_RAMPS` | 0x1674C |
| 266 | 0x3017 | 0x0001 | `NV_CAL_INFINEON_RF_FILTERS` | 0x16784 |
| 267 | 0x3018 | 0x0001 | `NV_CAL_GAIN_WORDS_HIGH` | 0x167BC |
| 268 | 0x3019 | 0x0001 | `NV_CAL_FREESCALE_MARS_1401` | 0x167F4 |
| 269 | 0x301A | 0x0001 | `NV_CAL_RENESAS_B6PLD_INIT` | 0x1682C |
| 270 | 0x301B | 0x0001 | `NV_CAL_GAIN_OFFSETS2` | 0x16864 |
| 271 | 0x301C | 0x0001 | `NV_CAL_RX_COMPENSATION2` | 0x1689C |
| 272 | 0x301D | 0x0001 | `NV_CAL_ORPHEUS_OPTIONS` | 0x168D4 |
| 273 | 0x301E | 0x0005 | `NV_CAL_SIM_PIN` | 0x1690C |
| 274 | 0x301F | 0x0001 | `NV_CAL_IMC_PMB571X_VDET_TABLEB` | 0x16944 |
| 275 | 0x3020 | 0x0001 | `NV_CAL_PRW_FREQ_TBL` | 0x1697C |
| 276 | 0x3021 | 0x0000 | `NV_CAL_ORPHEUS_LAST` | 0x169B4 |
| 277 | 0x4000 | 0x0000 | `NV_SVP_FIRST_NUM` | 0x169EC |
| 278 | 0x4FFF | 0x0000 | `NV_SVP_LAST_NUM` | 0x16A24 |
| 279 | 0x7000 | 0x0000 | `NV_UNITTEST_BOOT_MODE_BLOCK` | 0x16A5C |
| 280 | 0xB000 | 0x0000 | `NV_QNX_USB_Guid` | 0x16A94 |
| 281 | 0xB001 | 0x0000 | `NV_QNX_MAC_Wifi` | 0x16ACC |
| 282 | 0xB002 | 0x0000 | `NV_QNX_MAC_BT` | 0x16B04 |
| 283 | 0xB003 | 0x0000 | `NV_QNX_MAC_USBNET1` | 0x16B3C |
| 284 | 0xB004 | 0x0000 | `NV_QNX_MAC_USBNET2` | 0x16B74 |
| 285 | 0xB005 | 0x0000 | `NV_QNX_CAL_NVS_Map` | 0x16BAC |
| 286 | 0xB006 | 0x0000 | `NV_QNX_BR_Metrics` | 0x16BE4 |
| 287 | 0xB007 | 0x0000 | `NV_QNX_FSEC` | 0x16C1C |

---

## 3. The IDs the task asked about (answers to items 1 and 2)

| name | ID | flags | notes |
|---|---|---|---|
| `NV_OSSTORE_PIN_NUM` | **`0x2002`** | **`0x0001` (protected)** | 4 or 8 bytes on media; read by `0x369C` (`0x36xx`, `case 0x11` / `case 0` / `case 1`) as `FUN_00018FF0(0x2002, buf, 8)`; result 4 → *"found a 32 bit pin"* (`0x11B04`), 8 → *"found a 64 bit pin"* (`0x11B34`), else *"could not find a valid PIN (32 or 64 bit), faking one for PPS object"* (`0x11B64`). Published to the **`deviceinfo`** PPS object (`PIN`, `PIN64`, strings `0x11BF8`,`0x11C08`). **No salt, no digest, no attempt counter.** |
| `NV_OSSTORE_PasswordStruct_NUM` | **`0x2801`** | `0x0000` | see §6/§7 |
| `NV_OSSTORE_PasswordStructReserve_NUM` | **`0x2802`** | `0x0000` | fallback copy; live content is all-zero except a small tail (`nv-0x2802.txt`) |
| `NV_OSSTORE_OS_REGION` | **`0x2074`** | `0x0000` | 0x48-byte boot-region/counter struct — §8 |
| `NV_OSSTORE_BOOT_MODE_BLOCK` | **`0x2075`** | `0x0000` | |
| `NV_OSSTORE_NukeStatus_NUM` | **`0x2040`** | `0x0000` | *absent* on the live unit (0 bytes) — matches §76 |
| `NV_OSSTORE_ResetInfo_NUM` | **`0x2821`** | `0x0000` | reset-cause log |
| `NV_OSSTORE_MMCBootInfo_NUM` | **`0x2823`** | `0x0000` | |
| `NV_OSSTORE_CFP_LAL_NUM` | **`0x2033`** | `0x0000` | |
| `NV_OSSTORE_StartupTime_NUM` | **`0x2053`** | `0x0000` | boot timing record (not a counter gate) |
| `NV_OSSTORE_OnTime_NUM` | **`0x200D`** | `0x0000` | |
| `NV_OSSTORE_UtcBackup_NUM` | **`0x2015`** | `0x0000` | |
| `NV_OSSTORE_RtcBackup_NUM` | **`0x2014`** | `0x0000` | |
| `NV_OSSTORE_DevRandomNumber_NUM` | **`0x2009`** | `0x0000` | |
| `NV_OSSTORE_RimCryptKeyStorage_NUM` | **`0x205D`** | `0x0000` | |
| `NV_OSSTORE_bsn_NUM` | `0x280F` | **`0x0003`** | device serial used by `calculate_hwfp` |
| `NV_OSSTORE_Nvram_Signatures` | `0x2812` | `0x0001` | TLV blob holding the RSA signature(s) — §7.3 |
| `NV_OSSTORE_SecureRecordsHash_NUM` | `0x281B` | `0x0000` | 20-byte SHA-1 digest of the "secure records" set — §7.3 |

**`NV_OSSTORE_PIN_NUM` is `0x2002`, not `0x2801`.** They are not aliases and not
near-duplicates: `0x2002` is a bare number (the device PIN), `0x2801` is a password
verifier structure. The resmgr never mixes them: `0x2002` is only ever read as an
integer for PPS, and `0x2801` is only ever read/written as an opaque 0x70-byte blob.

### 3.1 "Is `0x2801` the PIN, the device password, or another credential store?"

It is **the device-password (boot-password) record**: `NV_OSSTORE_PasswordStruct`. The
name, the structure (salt + iteration count + digest), the 10-attempt budget, and the
resmgr's dedicated *password wipe* devctl (`0xC008050C`, §7) all agree. It is the record
the `hd0` boot stage turns into a boot gate (`75` §3), and the same 5060-iteration
SHA-512 parameters appear in the bootrom/loader password challenge (`69` §3).

There is **no other password record** in the ID space. The only other credential-shaped
entries are `NV_CAL_SIM_PIN` (`0x301E`, flags `0x0005` — the *SIM* PIN, marked
unreadable/unsupported) and `NV_OSSTORE_PIN_NUM` (`0x2002`, the device PIN identifier).

---

## 4. The devctl dispatcher at `0xB840`

### 4.1 Message layout it assumes

`0xB840` is the resource manager's `resmgr_devctl` handler
(`push {r4,r5,r6,r7,r8,sl,lr}; sub sp, sp, #0xC4`, stack-canary at `[sp,#0xBC]`).
Register use: `r7 = ctp` (resmgr context), `r5 = msg` (`io_devctl_t *`), `r8 = msg + 0x10`.

| offset | field | evidence |
|---|---|---|
| `msg+0x00` | `u16 type` | written back as 0 at `0xBCA8` |
| `msg+0x02` | `u16 combine_len` | |
| `msg+0x04` | **`int32 dcmd`** | `ldr r3,[r5,#4]` @ `0xB8A4`, compared against every command |
| `msg+0x08` | **`uint32 nbytes`** | `ldr r2,[r5,#8]`; `subs r2,r2,#8` @ `0xBA70`, `0xBAB0`, `0xB9D4`, `0xBC68` |
| `msg+0x0C` | `uint32 zero` | cleared on reply (`0xBCB0`) |
| `msg+0x10` | **payload / reply area** | `r8 = r5 + 0x10` (via `str r3,[r8,#0x10]!` @ `0xB8A0`) |
| `payload+0x00` | `int32 result` (out) | `str r0,[r8]` after each handler |
| `payload+0x04` | **`u16 record id`** (in) | `ldrh r0,[r8,#4]` — proves IDs are 16-bit |
| `payload+0x06` | `u16 pad` | |
| `payload+0x08` | data buffer (in for write / out for read) | `add r1,r8,#8` |
| data length | **`nbytes - 8`** | `subs r2,r2,#8` then `bl 0x9BE0` with `r2` untouched |

The reply is the standard QNX in-place form: `msg->o.type = 0`, `msg->o.dcmd = <overall
status>` (`str r6,[r5,#4]` @ `0xBCB4`), `ctp->iov[0].iov_base = msg`,
`ctp->iov[0].iov_len = nbytes + 0x10` (`0xBCBC`–`0xBCC4`), handler returns `-1`.

A payload shorter than 8 bytes (`nbytes-8 < 0`) is rejected: the handler leaves
`payload+0x00 = -9` (`mvn r3,#9`) and returns.

### 4.2 Command table (dispatch chain `0xB8A8`–`0xB9B4`, handlers as listed)

The chain uses **signed** comparisons (`bge`/`bgt`, opcodes `0xA`/`0xC`) — which is why
the `0xC00805xx` family (negative as `int32`) and the `0x400850xx` family (positive) can
share one if-tree.

| dcmd | handler | client API (`libnvram.so.1` export → its `movw/movt` site) | what it does |
|---|---|---|---|
| **`0xC0080502`** | `0xB9C8` | `nv_get_record` @ `0xC14` (site `0xCB0`) | **READ**: `payload+0 = 0`; if `nbytes>=8`, `r0 = read_record(id = u16[payload+4], buf = payload+8, len = nbytes-8)` → `0x8FF0`; `payload+0 = bytes` |
| `0xC0080503` | `0xB9F0` | `nv_get_permissions` @ `0xADC` (site `0xB08`) | `payload+0 = 0x63B4(id)` = **flags word** |
| `0xC0080504` | `0xBA00` | `nv_is_protected` @ `0xA8C` (site `0xAB8`) | `payload+0 = 0x641C(id)` = `(flags&1) && secure` |
| `0xC0080505` | `0xBA10` | `nv_record_exists` @ `0xA3C` (site `0xA68`) | `payload+0 = 0x6538(id)` = `(runtime entry && size)` |
| **`0xC0080506`** | `0xBA20` | `nv_delete_secure` @ `0xB74` (site `0xBA0`) | `0x9E7C(id, 1)`; if 0 → `0xB7D4(id)` |
| **`0xC0080507`** | `0xBA44` | `nv_delete_insecure` @ `0xBC4` (site `0xBF0`) | `0x9E7C(id, 0)`; if 0 → `0xB7D4(id)` |
| **`0xC0080508`** | `0xBA68` | `nv_write_secure_record` @ `0x944` (site `0x99C`) | `0x9BE0(id, buf, nbytes-8, **1**)`; if 0 → `0xB7D4(id)` |
| **`0xC0080509`** | `0xBAA4` | `nv_write_record` @ `0x9C0` (site `0xA18`) | `0x9BE0(id, buf, nbytes-8, **0**)`; if 0 → `0xB7D4(id)` |
| **`0xC008050C`** | `0xBB4C` | *(no libnvram export)* | **`0x9CE8()` — the hard-coded `0x2801` password wipe** (§7) |
| `0x40085001` | `0xB9BC` | `nv_get_record_count` @ `0xB2C` (site `0xB50`) | `payload+0 = 0x6AE0()` = number of live records |
| `0x4008500A` | `0xBAE0` | *(none)* | `0x5F98()` device-info block → `memcpy(payload+8, info+0x36, 12)` (MAC-style ASCII) |
| `0x4008500B` | `0xBAE0` | *(none)* | same, `memcpy(payload+8, info+0x4E, 12)` |
| `0x4008500D` | `0xBB58` | `GetBootromMetrics` @ `0x8D0` (site `0x920`) | `0xB55C()` vtable call filling a **0xB4-byte** metrics struct; copies `min(nbytes-8, len)` to `payload+8` |
| `0x4008500E` | `0xBBE8` | *(none)* | same metrics call; `payload+0 = (*(u32*)(&metrics+0x84) != 0)` — a **boolean** (security/verified flag) |
| `0x4008500F` | `0xBC60` | *(none)* | `payload+0 = 0`; `0xE080(sel = payload+4, buf = payload+8, len = nbytes-8)` — signature/version selector helper (`0x1E080`: `param_1 < 2 && *len > 0x13`, indexes an 8-entry u16 table and calls `0x1DB20`) |
| *anything else* | `0xB9B4` | | `payload+0 = 89` (`0x59`) — "no such devctl" |

Two independent confirmations of this naming: (a) `libnvram.so.1`'s export table gives the
function order `GetBootromMetrics(0x8D0) … nv_get_record(0xC14)`, and the `movw/movt` pairs
inside each function are exactly the commands above; (b) the live tool reports
`is_protected(0x2801) = 0`, `get_permissions(0x2801) = 0`, `record_count = 27`, all of
which are produced by the handlers in the table.

### 4.3 The per-ID hook at `0xB7D4`

```c
void 0xB7D4(int id)                                   /* called after write/delete */
{
    if (<global byte at 0x1B8CC> == 0) return;        /* feature gate */
    if (id == 0x2074) { 0xC9D0(); return; }           /* OS_REGION changed  -> republish/refresh */
    if (id == 0x2002) { 0x369C(0); return; }          /* PIN changed        -> republish PPS "deviceinfo" */
    if (id == 0x280F) { 0x369C(2); return; }          /* bsn changed        -> republish */
}
```

* `0x2074` → `0xC9D0` re-reads `0x2074` (`FUN_00018FF0(0x2074,&buf,0x48)`) and rewrites the
  `/pps/.../osregion` object (`Version`, `RequestedBootRegion`, `ActualBootRegion`,
  `RequestedBootCount`, `ActualBootCount`, `MaxBootCount`, `BootStatus`, `InstallTime`),
  then calls `0xC940`.
* `0x2002` / `0x280F` → `0x369C(n)` re-reads the record and rewrites the `deviceinfo` PPS
  attributes (`PIN`/`PIN64`, `HardwareID`, `DeviceSerialNumber`, `IMEI`, `GUID`,
  `InProduction`, `SecureDevice`, …).

**There is no hook for `0x2801`.** Writing or deleting the password record triggers no
resmgr-side recomputation of anything — the resmgr treats it as an opaque blob.

### 4.4 The `0x2801` migration/wipe at `0x9CE8` (dcmd `0xC008050C`)

```c
p = malloc(0x70);
if (0x8FF0(0x2801, p, 0x70) == 0)  { log("could not find password in NVRAM"); return -10; }
switch (*(u32*)p) {
case 0x00010002:                                  /* v1, 0x3C bytes */
    *(u32*)(p+0x28) = *(u32*)(p+0x2C);            /* counter pair, v1 offsets */
    memset(p+0x08, 0xFF, 0x20);                   /* erase 32-byte salt/param block */
    len = 0x3C;  break;
default:
    if (*(u32*)p < 0x20000) { log("password wipe failed, unknown version of password struct: %d"); }
    *(u32*)(p+0x18) = *(u32*)(p+0x1C);            /* A := B  (burn the attempt budget) */
    memset(p+0x30, 0xFF, 0x40);                   /* erase the 64-byte digest */
    len = 0x70;  break;
}
if (0x9BE0(0x2801, p, len, /*secure=*/1) != 0) log("could not write cleared password structure.");
```

Strings: `0x1759C` *"could not find password in NVRAM"*, `0x175DC` *"password wipe failed,
unknown version of password struct: %d"*, `0x17638` *"could not write cleared password
structure."*; version compare `movw r3,#2; movt r3,#1` (= `0x00010002`) at `0x9D78`, and
`cmp r2,#0x20000` at `0x9DAC`.

This is the only place in the whole binary that touches `0x2801` (verified by grepping the
full decompilation: exactly two references, the read at `0x9D3C` and the write at `0x9E0C`).
So the **resmgr never computes or checks the password hash** — see §7.4.

---

## 5. Record plumbing: runtime index, copies, and on-media framing

The 288-entry table of §2 is only a **name/permission catalogue**. The live records are
tracked in a second, runtime table:

* **Runtime index**: 331 slots (`0x14B`), 28-byte (`0x1C`) entries, open-addressed with
  double hashing — `0x16454`: `h1 = id % 331`; probe step `((id % 0x149) * n + n + h1) % 331`.
  Entry fields (from `0x1655C`): `+0x00 u16 id`, `+0x02 u16 copy/chain index`,
  `+0x08 u32 size`, `+0x0C u32 data pointer/offset`, `+0x14 u32 refcount`.
* **Count**: `0x6AE0` returns the number of entries with a non-zero data pointer and size
  (live: **27** records).
* **Copies**: a record may have up to **6** copy streams; each stream is described by six
  12-byte segment descriptors `{dev, offset, length}` (`0x48` bytes per copy index).
  The reader (`0x8FF0`) walks copy indices 0..5 and returns the **first copy that passes
  both CRC checks**; if none passes it returns the last size anyway.
* **Sector framing**: `0x8CC8` translates a record address into
  `dev = addr >> 16`, `sector = (addr & 0xFFFF) >> 9`, `offset = addr & 0x1FF`, reads a
  **512-byte sector** (`0x46D8`), and pulls a **0x20-byte record header** at `sector+offset`
  (records may not cross a block boundary — `0x171A8`).
  Header fields actually used by the reader:
  `+0x08 u32` = payload CRC-32, `+0x14 u32` = payload length, `+0x18 u32` = header CRC-32
  (over `+0x00..+0x17`).
* **Checksums**: `0x6B54` = `crc32(header[0..0x17]) == *(u32*)(header+0x18)`;
  `0x8CC8` = `crc32(payload) == *(u32*)(header+0x08)`. The primitive is the standard
  reflected **CRC-32 (poly 0xEDB88320)**: `0x1D1F0` returns the `0xFFFFFFFF` init,
  `0x1D224` is the 256×32 table-driven update, `0x1D334` returns `~state`; the table itself
  is at file offset **`0x1870C`** (`0`, `0x77073096`, `0xEE0E612C`, …).
  Failures log `"Header checksum verification failed for record at offset 0x%X"` (`0x17168`)
  and `"Data checksum failed for record at offset 0x%X"` (`0x171F4`).
* **Writes** (`0x1913C`, 2656 bytes) reject `len > 0x5FE20`, chunk the payload into
  `0xFFB0`-byte pieces, allocate a free copy/segment, build the 0x20-byte header (record id,
  sequence number from the copy descriptor, chunk index/count, chunk length, CRC slots),
  write it, then re-insert into the runtime hash (`0x1655C`).

**There is no encryption layer**: the only crypto imported by the resmgr is SHA-1 and
RSA-verify from `libhuapi.so.5` (`hu_SHA1Begin/Hash/End`, `hu_RSAPKCS1v15VerPadExponent`,
`hu_InitSbg56`, `hu_RegisterSbg56`, `hu_RSAParamsCreate`), and it is used **only** for the
startup NVRAM-signature check (§7.3) — never for record payloads. A record flagged
`0x0004` is simply refused (`"Encrypted records not currently supported"`).

---

## 6. `0x2801` decoded exactly as the resmgr sees it

The resmgr reads/writes it as an **opaque 0x70-byte blob**; the only structural knowledge it
has is the two format versions in `0x9CE8` (v1 `0x00010002` → 0x3C bytes, v2 ≥ `0x00020000`
→ 0x70 bytes) and the two sub-ranges it erases on a wipe. Those ranges pin the layout:

| off | size | live value (`nv-0x2801.txt`) | interpretation | evidence |
|---|---|---|---|---|
| `+0x00` | u32 | `0x00020000` | format version 2.0 (v1 = `0x00010002`) | `0x9D78`/`0x9DAC` |
| `+0x04` | u32 | `0x00000070` | total struct length, = the write length used by the wipe | `0x9DD0` (`mov r2,#0x70`) |
| `+0x08` | u32 | `0x00000001` | flag (preserved by the wipe) | memset range `+0x08..+0x27` |
| `+0x0C` | u32 | **`0x000013C4` = 5060** | iteration count | see §6.1 |
| `+0x10` | u32 | `0xB9A043B5` | salt word 0 | inside erased range |
| `+0x14` | u32 | `0x222D86B5` | salt word 1 | inside erased range |
| `+0x18` | u32 | **`0x00000000`** | failed-attempt counter **A** | wipe does `A := B` (`0x9DB4–0x9DB8`) |
| `+0x1C` | u32 | **`0x0000000A`** | allowed attempts **B** = 10 | source of `A := B` |
| `+0x20` | u32 | `0xA1E864CE` | nonce / sequence (preserved by the wipe) | outside both erased ranges |
| `+0x24` | u32 | `0` | reserved | outside both erased ranges |
| `+0x28` | u32 | `0x00000030` | **offset of the digest** | digest is erased at `+0x30` |
| `+0x2C` | u32 | `0x00000040` | **length of the digest** (64) | `0x30 + 0x40 = 0x70` = record length ✔ |
| `+0x30` | 0x40 | `91 1d 08 b1 … cd a1` | **64-byte digest** (SHA-512-sized); all-`0xFF` means "no password" | memset `+0x30..0x6F` |

The self-consistency check is decisive: the wipe erases exactly `+0x30 .. +0x6F` and nothing
else in that half of the struct, `+0x28` holds `0x30`, `+0x2C` holds `0x40`, and
`0x30 + 0x40 = 0x70` = the record length at `+0x04` = the wipe's write length.
For v1 the same two fields move (they are at `+0x28`/`+0x2C` there — that is why the v1
branch does `*(u32*)(p+0x28) = *(u32*)(p+0x2C)`), and v1 is 0x3C bytes with an SHA-1-sized
(20-byte) digest; v1 does not occur on this unit.

### 6.1 Is 5060 an iteration count consistent with a SHA-512 password hash / PBKDF2?

* The digest field is **exactly 64 bytes** and the struct carries an **8-byte salt**, a
  **32-bit iteration-like parameter**, and a **10-attempt budget** — the classic shape of an
  iterated salted password hash. 64 bytes is the SHA-512 output size; the v1 format's
  20-byte digest is the SHA-1 output size.
* **5060 matches the password-challenge parameters independently observed on this platform**:
  `69-live-bootrom-loader-usb.md` §3 records the bootrom/loader USB password challenge as
  **5060 SHA-512 iterations, attempts 10/10**, and `HANDOFF.md` names the client as
  `HashPassV2` with a `00 00 40 00` prefix. The same 5060 and the same attempt maximum 10
  appear here.
* **Caveat, stated precisely**: this binary contains **no** implementation of the KDF. There
  is no `5060`/`0x13C4` immediate anywhere in the text (verified by scanning every ARM/Thumb
  instruction for that immediate and by searching the raw file), no SHA-512 code, and no
  SHA-512 import. The resmgr treats `+0x0C` as opaque data. So "5060 = iteration count" is
  supported by the structure, the platform's matching challenge parameters, and the field's
  position — but it is **not** proven by this file. What *is* proven by this file is that the
  resmgr never validates the hash (see §7.4).
* **Resolved in `84` (arithmetic, external to this binary):** the live 64-byte digest is
  reproduced exactly by `state₀ = "0000"; 5060 × state = SHA-512(LE32(counter) ‖ salt ‖ state)`
  with `salt = record+0x10..+0x17`. So 5060 is the exact round count, and the KDF is iterated
  SHA-512, not PBKDF2. The resmgr still treats it as opaque.

### 6.2 Is there any PIN-specific structure or comparison in `0x2801`?

**No.** Nothing in the binary compares `0x2801`'s payload against anything, and nothing
tests its `+0x18`/`+0x1C` fields (`0x9CE8` is the only reader and it only copies `B → A`
while wiping). The attempt-counter *semantics* (gate = `clamp(B,3,10) - A`) live entirely in
the `hd0` boot stage (`75` §3, `FUN_80014F84`), not in the OS resource manager. The PIN
record (`0x2002`) has no structure at all — it is a bare 32- or 64-bit integer.

---

## 7. Protection, checksums, and "secure vs insecure" copies

### 7.1 What the resmgr enforces for `0x2801`

| control | result for `0x2801` | evidence |
|---|---|---|
| permission flags | **`0x0000`** — no bits set | table row `0x14A6C`; live `get_permissions` = 0 |
| `nv_is_protected` | **false** (`flags & 1` is 0) | `0x641C`; live `is_protected` = 0 |
| write/delete gating | **not gated** (the `flags & 1` test at `0x9BE0`/`0x9E7C` never fires) | `0x9BE0`, `0x9E7C` |
| encryption | **none** | `0x18FF0`, `0x9BE0` (`flags & 4` path unused here) |
| integrity | **CRC-32 only** (header CRC + payload CRC) | `0x6B54`, `0x8CC8`, table `0x1870C` |
| MAC / signature over the record | **not in the resmgr** (see §7.3) | — |

So any process that can open `/dev/nvram` can rewrite `0x2801` with an arbitrary 0x70-byte
blob as long as it fixes the two CRCs — and `libnvram`'s `nv_write_record` does that for you.
That is exactly what the live `nvram patch 0x2801 …` path does.

### 7.2 The "secure write" flag (`0xC0080508`) does *not* mean "protected"

`0x508` and `0x509` both land in **the same** function `0x9BE0`, through the same
permission test; the only difference is `param_4` (1 vs 0), which is forwarded to the write
engine `0x1913C` and used in exactly two places:

```c
if (param_4 != 0 && copy_index == 6)  header_secure_byte = 1;     /* 0x19398 */
...
if (param_4 != 0 || copy_index < 6)   { release/refcount handling }  /* 0x19A28 */
```

i.e. a "secure" write stamps a **secure-copy marker byte into the on-media record header**
when it allocates a fresh copy. The resmgr's own reader never tests that byte (the read path
`0x8FF0 → 0x8CC8` validates CRCs only), so the marker exists for **other** consumers.

Consequence worth recording for `74`: because both devctls share `0x9BE0`, the early-return
in `nvram.c` ("if `nv_write_record` fails, don't try `nv_write_secure_record`") **cannot lose
anything** — a record that fails the insecure write fails the secure write identically
(same `flags & 1` / `flags & 4` tests, same address).

### 7.3 The one place records *are* signed: the startup "secure records" digest

`0xE2E8` runs at startup and is the only signature path in the binary:

1. `0x1B3B0` picks a platform variant index **0..7** by `uname()` string matching
   (`strstr` chain over the release string); `0x1B5B4` installs that variant's record-ID list
   and pointer pair into the global state.
2. `hu_SHA1Begin/Hash/End` walks that ID list: for each ID it hashes the 4-byte ID and then
   the record contents (`FUN_00018FF0(id, buf, 0x10000)`).
3. It reads **`0x281B` (`NV_OSSTORE_SecureRecordsHash_NUM`)** and requires exactly **0x14
   (20) bytes**, then `memcmp`s:
   `"Length of NV_OSSTORE_SecureRecordsHash_NUM (%d) does not match %d!"` (`0x19510`),
   `"Digest of secure records does not match stored hash value!"` (`0x19554`).
4. It reads **`0x2812` (`NV_OSSTORE_Nvram_Signatures`, up to 0x10000 bytes)**, walks its TLV
   (`{u16 startOffset; u16 length; …}`), and RSA-verifies with
   `hu_RSAPKCS1v15VerPadExponent` via `0x1DC9C` → `"Nvram record hash signature verified
   successfully."` (`0x19650`).
5. On failure: if the unit is a **secure device** (flag derived from the device-info block,
   `0x1B55C` vtable `+0x14`), it prints `0x19774`, calls **`0x1D680()`** (which bumps the
   `0x2074` region counters) and **`sysmgr_reboot()`**; on an insecure unit it prints
   `"insecure image... proceeding..."` (`0x197A0`) and continues.
   The secure/insecure decision itself is `1 - local_78` from the device-info block, logged
   as `"Secure device: verifying NVRAM signature"` (`0x19444`) vs
   `"Insecure device: testing NVRAM signature"` (`0x19470`).

**Unresolved**: whether `0x2801` is a member of that per-variant ID list. The list is chosen
at runtime and its entries are relocation-time pointers (the slots live in the
`R_ARM_RELATIVE` `.data`/GOT area at `0x1B1xx–0x1B2xx`), so the list cannot be enumerated
from the file image without the loader's relocation base. What can be said without
speculation: `0x2801` carries **no flag** that would put it in the "protected" class, and the
digest set is a *different* mechanism (a static per-platform list, not the flags word).

### 7.4 The resmgr never validates or computes the password digest

Established three ways:
1. `0x2801` appears in exactly **two** instructions in the entire binary — the read at
   `0x9D3C` and the write at `0x9E0C`, both inside the wipe `0x9CE8`.
2. No SHA-512/SHA-256/KDF code or imports exist (only SHA-1 for the NVRAM signature).
3. No comparison is ever made against `+0x18`/`+0x1C`/`+0x30` of the record.

Therefore, on the OS side, `0x2801` is a **write-only credential blob**: the *only* consumer
that interprets it is the `hd0` boot stage (documented in `75`), which is also the only code
that enforces the attempt budget.

### 7.5 Does an "insecure" write suffice for the bootloader, or is the secure copy authoritative?

From this binary alone:
* The resmgr does not distinguish the two for `0x2801` beyond the header marker byte (§7.2);
  both `0x509` and `0x508` are accepted for `0x2801` (it is unprotected).
* The resmgr's reader always returns **the first copy that passes its CRCs** (`0x8FF0`
  loops copy indices 0..5), so whichever copy the writer produced is what a resmgr client
  reads back. It makes **no** guarantee about which copy a *different* consumer (the boot
  stage) will prefer.
* The 0x508 write is the one that marks a fresh copy as "secure", and it is the write the
  resmgr's own password wipe uses (`0x9BE0(0x2801, p, 0x70, 1)` at `0x9E0C`). If the `hd0`
  stage's record layer honours that marker (its accessors are a different build —
  `LDR_01` in `75` §1), the **secure copy is the authoritative one** and an insecure-only
  write could be ignored. That cannot be settled from `nvram_resource_manager`; it must be
  checked in `hd0head.bin`/`LDR_01`.
* Practical note for the live unit: `0x2801` is already unprotected, so the resmgr will
  happily commit an insecure copy; whether the boot gate sees it is a bootloader question.

---

## 8. Where the OS region/update state and factory counters live

> **Superseded by `83`.** This section's `+0x06 u16[5]` reading is wrong. `0x2074` is an
> `OsRegionV3` (`=IBBHHHIQ24s24s`, **72 bytes**): `+0x04` requested_region, `+0x05`
> actual_region, `+0x06` boot_count_0, `+0x08` boot_count_1, `+0x0A` max_boot_count
> (0 = unlimited), `+0x0C` flags (u32), `+0x10` epoch, `+0x18`/`+0x30` version IDs. It is the
> OS A/B region selector and update state, not a factory counter. `0x2801` is the password
> record; the factory counter is the RPMB `BOOTCTR_FCTRY`. On-media region values are 0/1
> (0 = hd1, 1 = hd2); "1 or 2" is drive numbering, and 0 is a valid region.

**`0x2074 NV_OSSTORE_OS_REGION`** (72 bytes) is the OS region/update record. `0xC9D0` (called
by the `0xB7D4` hook whenever `0x2074` is written) republishes it to PPS:

| offset | size | field (PPS attribute) | live |
|---|---|---|---|
| `+0x00` | u32 | `Version` | `3` |
| `+0x04` | u8 | `RequestedBootRegion` (requested_region) | `0` |
| `+0x05` | u8 | `ActualBootRegion` (actual_region) | `0` |
| `+0x06` | u16 | `RequestedBootCount` = boot_count_0 | `0x017B` → `0x017C` |
| `+0x08` | u16 | `ActualBootCount` = boot_count_1 | `0` |
| `+0x0A` | u16 | `MaxBootCount` | `0` = unlimited |
| `+0x0C` | u32 | flags | `0` |
| `+0x10` | u64 | `InstallTime` / epoch | `0` |
| `+0x18` | 24s | version_id_0 | empty |
| `+0x30` | 24s | version_id_1 | empty |
| — | — | `BootStatus` = `"Success."`, `"Load is corrupted."`, `"Boot count exceeded for requested region."` | — |

`boot_count_0` increments once per successful verification of region 0; `max=0` is unlimited.
`switch()` sets requested = actual^1, counts 0, max 4; `sane()` clears to requested = actual,
max 0. The bootloader reads `0x2074` at `0x800142DC` and writes it at `0x80014508`; the gate at
`0x80014998` reads `0x2801`, not this record.

Other counter/telemetry records (all **not** `0x2801`): `0x2053 StartupTime`,
`0x200D OnTime`, `0x2818 RefurbishedTime`, `0x2816 NANDEraseCountStats`, `0x2825 MMCStats`,
`0x206E BATTERY_KILL_SWITCH_COUNT`, `0x2821 ResetInfo`, `0x2823 MMCBootInfo`,
`0x2040 NukeStatus` (absent live).

`0xD680` (invoked from the `0x2074` write path and from the signature-failure reboot path)
rewrites the region limits — it forces the relevant count slots to **5** depending on the
`Version` byte (v1 → `counts[1]`, v2/v3 → `counts[requested]` and the max slot), i.e. it
clamps the boot budget. That is the only "count" the resmgr itself manipulates.

---

## 9. Reconciliation with the earlier documents

| earlier claim | verdict |
|---|---|
| `0x2801` is `NV_OSSTORE_PasswordStruct` (`75`, `76`) | **Correct**, and now proven from the descriptor table itself: entry #133, id `0x2801`, flags `0x0000`, at `0x14A6C`. |
| `0x2801` is "boot-info" | **Wrong.** Boot info/counters are `0x2074 NV_OSSTORE_OS_REGION` (plus `0x2053`/`0x200D`/`0x2818`). `0x2801` is the password verifier. |
| `0x2801` is used as a password-attempt gate by `hd0` (`75`) | **Consistent** with the OS side: the resmgr's only `0x2801` consumer is the wipe, which manipulates exactly the counter pair the boot gate reads (`A := B`). |
| "isn't `0x2801` just the device PIN/password?" | It is the **device password** record — but the **PIN is a separate record (`0x2002`)**. Calling `0x2801` "the PIN" is wrong on both counts: wrong record, and `0x2002` is an identifier, not a credential check. |
| `74`: "if `nv_write_record` fails, `nv_write_secure_record` is never attempted, so patch the tool" | **Not needed.** Both devctls share `0x9BE0` and the same permission gate, so a failure is identical for both; the early return loses nothing. |
| `74`/`75`: "patch `+0x18 = 0`" | Moot (`76`): live `A` is already 0. |
| `76`: `+0x0C = 5060` = SHA-512 iteration count | Consistent with the struct shape and with `69` §3's 5060/10-attempt challenge; **not verifiable inside this binary** (§6.1). |

---

## 10. Answers to the six task items

1. **Complete ID→name table** — §2 (288 entries, IDs, flags, per-entry file offset). The
   requested names resolve as listed in §3.
2. **`PIN` = `0x2002`; `PasswordStruct` = `0x2801`.** `0x2801` is the **device-password**
   verifier store (salted, iterated, 64-byte digest, 10-attempt budget), *not* the hardware
   PIN and *not* a boot/factory counter. The OS A/B region/update state is `0x2074` (`83`).
3. **Dispatcher `0xB840`** — §4: full command table (`0x502` read, `0x503` permissions,
   `0x504` is_protected, `0x505` exists, `0x506/0x507` delete secure/insecure,
   `0x508/0x509` write secure/insecure, `0x50C` password wipe, `0x40085001` record count,
   `0x0A/0x0B/0x0D/0x0E/0x0F` metrics/signature helpers), payload layout
   (`dcmd@+4`, `nbytes@+8`, reply struct at `+0x10` = `{i32 result; u16 id; u16 pad; u8 data[]}`),
   and the `0xB7D4` per-ID hook (`0x2074`→`0xC9D0`, `0x2002`→`0x369C(0)`, `0x280F`→`0x369C(2)`);
   the `0x2801` "migration" is in fact the **password wipe** `0x9CE8` (§4.4).
4. **`0x2801` structure** — §6: 5060 is an iteration-count-shaped parameter consistent with
   a SHA-512/PBKDF2-style hash (64-byte digest, 8-byte salt, 10 attempts) and matches the
   platform's bootrom password challenge, but no KDF exists in this binary; `+0x10/+0x14` are
   salt words; `+0x18`/`+0x1C` are the failed-attempt counter and its maximum; `+0x20` is a
   nonce/sequence; `+0x28/+0x2C` are the digest offset/length and `+0x30..+0x6F` is the
   64-byte digest. **No PIN-specific structure or comparison exists.**
5. **Validation** — records are **CRC-32** protected only (header CRC + payload CRC, §5);
   no encryption. `0x2801` carries **no** permission flags and is freely writable. The
   resmgr never checks its digest; the only signature over records is the startup SHA-1 +
   RSA check of the "secure records" set (`0x281B`/`0x2812`, §7.3), whose membership I could
   not statically resolve. Whether the bootloader prefers the "secure" copy (header marker
   set only by devctl `0x508`) must be answered in `hd0head.bin`, not here.
6. **Boot/factory counter** — `0x2074 NV_OSSTORE_OS_REGION` (§8), not `0x2801`.

---

## 11. Reproducing the key facts

```bash
B=device-binaries/nvram_resource_manager
# descriptor table: 288 entries x 56 bytes at 0x12D54
python3 - <<'EOF'
import struct
d=open('nvram_resource_manager','rb').read()
for i in range(288):
    b=0x12D54+i*0x38
    print(f"0x{b:05X} id=0x{struct.unpack_from('<H',d,b)[0]:04X} "
          f"flags=0x{struct.unpack_from('<H',d,b+2)[0]:04X} "
          f"{d[b+5:b+57].split(b'\\0')[0].decode()}")
EOF
# dispatcher command compares
python3 -c "… capstone, disassemble 0xB8A8-0xB9B4 …"
# CRC-32 table
xxd -s 0x1870c -l 0x20 $B
# password wipe
xxd -s 0x9ce8 -l 0x178 $B
```

Artifacts referenced: `playbook-live-2026-10-21/nv-0x2801.txt`, `nv-0x2074.txt`,
`nv-0x2802.txt`, `nv-0x2004.txt`, `nv-0x2040.txt`; `device-binaries/libnvram.so.1`
(cmd↔name map); `76-live-0x2801-audit.md`, `75-nvram-2801-password-record.md`,
`69-live-bootrom-loader-usb.md`.
