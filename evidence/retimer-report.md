# Retimer firmware evidence report — Surface Pro 11 (ARM)

Source package: `SurfacePro11_ARM_Win11_26100_26.041.12746.0/SurfaceUpdate/{retimer0,retimer1}/`
Files analyzed: `SurfaceRetimer{0,1}_128.3.6.0.bin` (68,136 bytes each) and `SurfaceRetimer{0,1}.inf`.

Confidence labels: **[observed]** = directly read from bytes; **[derived]** = decoded/parsed from observed structure; **[inferred]** = reasoned, not byte-proven.

Note: the REA MCP namespace was not available in this session; analysis was performed with direct binary inspection (xxd/cmp/strings/entropy). No REA handle was opened, so no `close_binary` was needed.

## 1. Container format

**[observed/derived]** Microsoft Surface firmware capsule (same family as other SurfaceUpdate `.bin` payloads), little-endian, total size 0x10A28 = 68,136 bytes:

| Offset | Size | Value | Meaning |
|---|---|---|---|
| 0x00 | 16 | `edd5cb6d 2de8444c bda17194 199ad92a` | Container signature/GUID (same in both retimer0 and retimer1) |
| 0x10 | 4 | 0x00000020 | header/flags field |
| 0x14 | 4 | 0x00050000 | version-ish field (matches INF DriverVer field encoding pattern) |
| 0x18 | 4 | 0x00010A28 | **total image size** — equals actual file size [derived] |
| 0x1C | 4 | 0 | reserved |
| 0x20 | 4 | 1 | section count / payload index |
| 0x24 | 4 | 0x00010000 | **payload offset** (main firmware blob starts at 0x10000) |
| 0x28 | 4 | 0x10 | alignment/flags |
| 0x2C | 4 | 0 / 2 | reserved |
| 0x34 | 16 | mixed-endian GUID | **FirmwareId** — only difference between the two .bin files |
| 0x50 | ~0x950 | X.509 PKCS#7 DER | Authenticode-style signature (`3082 099c ...` at 0x7C) signed by **"Microsoft Surface Monzino Firmware Signer"** under "Microsoft Surface LFF Firmware CA 2021" [observed via cert strings, valid 2024-02-08→2025-05-08] |
| 0x9A2C | – | ASCII | payload descriptor strings (§2) |
| 0xA1C | 4 | `MSS1` | payload sub-container magic [observed] |
| 0xA20 | ~0x50 | – | register-init table (§5) |
| 0x2A2A–0x3000 | – | code-like | secondary 8051 code block, FF-padded around |
| 0x10000–0x10A20 | 0xA20 | – | main firmware blob (8051 machine code, see §5) |
| 0x10A20 | 4 | `a908` + 0xFFFF fill | trailing checksum/EOF marker [inferred] |

Large 0xFF padding regions at 0x1000–0x2000, 0x3000(+)–0x10000 → flash-image layout with erased sectors. **[derived]**

## 2. Identity / version

**[observed]** Embedded strings (ASCII, single occurrences):
- `0x9A2C`: `PS8830 MS`
- `0x9A47`: `With Bootloader 1.0.3`
- `0x9A65`: `PS8830 retimer only Microsoft project 20240515`

So: silicon is **Parade PS8830** USB4/Thunderbolt retimer (not PS8833 variant string seen), bootloader 1.0.3, image built 2024-05-15 (matches INF DriverVer date 05/15/2024). Firmware version 128.3.6.0; INF `FirmwareVersion` REG_DWORD `0x80030600` = 128.3.6.0 [derived].

**Diff retimer0 vs retimer1 [observed]:** exactly 16 bytes differ, at 0x34–0x43 — the FirmwareId GUID. Everything else byte-identical. Same firmware image, two device instances.

- retimer0 GUID `{05575d61-005d-4bc9-8e90-13ef293aa632}` (stored mixed-LE)
- retimer1 GUID `{59b50971-4c23-41e5-b168-0f1c0896c6ee}`

## 3. Bus addressing

**[observed, from INFs]** Devices enumerate as UEFI firmware resources, not PCI/USB:
- HW IDs: `UEFI\RES_{05575d61-...}` (Port 0) and `UEFI\RES_{59b50971-...}` (Port 1)
- Install: Class=Firmware, NULL driver, AddReg sets `FirmwareId`, `FirmwareVersion=0x80030600`, `FirmwareFilename`; file copied to `%SystemRoot%\Firmware`. `PnpLockdown=1`.
- Descriptions: "Surface USB4 Retimer (Port 0/1)".

**No I2C address, UCSI proxy, or HID instance appears anywhere in the INF or the image** [observed — absence]. The Windows update path is UEFI-mediated (capsule staged to `\Windows\Firmware`, applied by Surface UEFI at reboot); there is no OS-side CFU/UCSI update channel in these files. I2C addressing of the PS8830 on the SP11 must come from DSDT/devicetree, not this package. [inferred]

## 4. Firmware update protocol

**[observed]** No CFU (Intel Component Firmware Update) offer/payload structures, no GUIDs matching CFU, no Microsoft UCSI-vendor markers. Update flow is: UEFI capsule resource (`UEFI\RES_*`) → staged file → Surface UEFI applies it. For Linux, relevance of PR #72: the host-side update protocol is not discoverable from this package; the on-device bootloader ("With Bootloader 1.0.3") handles flashing via a Parade-proprietary scheme not exposed here. [inferred]

## 5. Constants of interest

**[observed]** Register-init table inside `MSS1` block at 0xA20, entry pattern `<tag 02> <reg:16> <mask:32>`:
- regs `0x3003, 0x300B, 0x3013, 0x301B, 0x3023` each masked `0xFFFFFFFF`
- followed by value `0x0000968F` at 0xA58 region and entry `00 02 20 00` — consistent with a PHY/lane register init sequence in the 0x30xx page. [derived]
- The repeated `02 xx xx ff ff ff ff` structure suggests (enable reg, all-bits mask) pairs — candidate PHY lane-enable/orientation registers for PR #24. [inferred]

**[observed/derived]** Payload at 0x10000 (and 0x2A2A) is **8051 machine code** (e.g. `f582 e434 08f5 83` = `MOV DPL,A / MOV A,#... / ORL A,#08 / MOV DPH,A`; `12 xx xx` LCALL pairs; `9003 xx` MOV DPTR) — consistent with PS8830's 8051 core. Numerous `MOV DPTR,#imm16` constants in the 0x2A2A block (targets like `0x0870`, `0x08FC`, `0x08A8`, `0x08AD`, `0x08BB`, `0x0Cxx`) point at an SFR/XDATA register page around `0x08xx` — plausible retimer register interface. [inferred]

No plaintext I2C 7-bit address constant could be proven from static inspection. [observed absence]

## 6. Limitations

- Static container analysis only; no disassembly of the 8051 blob (register semantics, orientation handling, update protocol handlers remain unverified).
- Register-init interpretation (0x30xx page) is pattern-derived; datasheet needed to name registers.
- Bus address/ACPI wiring absent from package; must be sourced from DSDT dump or existing PS8830 Linux driver (`drivers/usb/typec/parade/ps8830*`).
- REA MCP tools unavailable this run; offsets/bytes above are from direct hex inspection and are re-checkable with `xxd -s <off>`.
