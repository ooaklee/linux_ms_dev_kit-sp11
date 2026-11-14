# QcUsb4Bus8380.sys — reverse-engineering evidence report

Target: `SurfaceUpdate/qcusb4bus/QcUsb4Bus8380.sys` (Qualcomm USB4 host-router *bus* driver, Windows ARM64)
Package: SurfacePro11_ARM_Win11_26100_26.041.12746.0, DriverVer 10/29/2024, 1.0.4160.6000 (INF), file version 1.0.0.1789 (resource)
SHA-256: `1cdf0d84a27fd2d76bdd44b1bf840279225d6014ba4c520c665f4f07746658bf`
Tooling: REA 6.9.0 with Ghidra 12.2 (ARM64 native decompiler built on-host), full-corpus decompile (994 functions), plus PE import/relocation parsing.
PDB reference: `Z:\b\WP\QcUsb4Bus\rel\10.9\ARM64\Release\QcUsb4Bus8380.pdb`, GUID `38d3ad18-af12-485d-8881-989dce886751` (not shipped — names are Ghidra-recovered, `FUN_*`).
Image base `0x140000000`; sections `.text` (VA `0x140001000`), `.rdata` (`0x140028000`), PAGE (`0x140032000`), INIT (`0x140052000`).

## Executive summary (important for PR #76 blockers)

**This driver contains no MMIO, register, ring, interrupt, DMA, or firmware-download logic at all.**
It is a WDF/DMF "dynamic enumeration bus" driver whose entire job is:

1. Attach to `ACPI\QCOM0C6D` (USB4 host-router ACPI device, per INF).
2. Enumerate child PDOs (hwid `USB4\QCOM0CD10001`, instance `USB4_DEVICE_%02d`, friendly "USB4 HR PDO", compatible `ACPI\ACPI0015`).
3. Forward child IRPs down to the ACPI stack with `IoAllocateIrp`/`IoSynchronousCallDriver` (`0x140002d18`).
4. Host Qualcomm DMF diagnostics modules (LiveKernelDump/BranchTrack/WppRecorder telemetry — the LKMDTel WER plumbing).

Consequences for the Linux port's three blockers:
- **MMIO mapping/alignment**: no evidence here. The actual router MMIO is owned by a different module (likely `usb4hostrouter`/Qcom firmware drivers elsewhere in SurfaceUpdate, or the ACPI `_CRS` of QCOM0C6D consumed by another driver). `QcUsb4Bus8380.sys` never calls `MmMapIoSpace`/`WdfCmResourceList*`-MMIO paths in its own code (imports contain no `MmMap*`; WDF usage is via the dynamically bound `WDFLDR.SYS` table for control-device/PDO APIs only).
- **Firmware loading**: none in this binary. No `.bin` filenames, no firmware registry keys, no CFU (ComponentFirmwareUpdate) *instantiation* — only the stock DMF `WdfCustomType_DMF_ComponentFirmwareUpdate*` strings compiled in by the DMF framework library (strings `0x27430`, `0x27470`, `0x275c8`, `0x27d88`), with no corresponding client module in the instantiated DMF tree (DeviceAdd path instantiates LiveKernelDump/BranchTrack/IoctlHandler/RingBuffer/BufferPool only — see `0x14003cef0`/`0x140032f90`).
- **Stop/restart ownership**: no D0/D3, no IRP_MJ_POWER handling of its own. PDO power/PnP IRPs are simply forwarded (possibly with status-defaulting; see `0x140002d18` which copies the current IRP stack location and calls `IoSynchronousCallDriver`, returning `ivar1 = 0xC00000BB` initial status — "not supported" default before completion). The Linux frontend cannot look to this driver for power sequencing; ownership questions must be answered by whichever driver services the `USB4\QCOM0CD10001` PDOs.

## Register map observations

| Offset | Purpose | Confidence | Evidence |
|---|---|---|---|
| — (none) | No device register offsets exist in this driver | observed | Full-corpus decompile: zero occurrences of `MmMapIoSpace`, `READ_REGISTER*`, `WRITE_REGISTER*`, `HalTranslateBusAddress`, or any volatile pointer dereference pattern; import table has no Mm mapping APIs (only `MmGetSystemRoutineAddress`); no CmResource memory-descriptor handling code |

The only "offset-like" values found are DMF/WDF structure offsets (e.g., context fields `+0x18/+0x29/+0x2c` in WPP recorder replay state at `0x1400024f0`) and WDF version-table slots (`*(code **)(DAT_14002f298 + 0x648)` at `0x140022050` — a WDF function-table slot, not hardware).

## Firmware loading

**No firmware loading interface exists in this driver** (observed).

- No `.bin`/`.dfu`/`.fw` strings anywhere in the string table (full ASCII+UTF-16 dump, 610 strings).
- Registry keys referenced: only `HKLM\SYSTEM\CurrentControlSet\Control\LKMDTel` with value `MaxReportSizeInMB` (`0x4df38`, `0x4df10` file offsets) — live-kernel-dump telemetry sizing, not firmware.
- DMF `ComponentFirmwareUpdate` interface *declaration data* strings are present (`0x27430` `DMF_INTERFACE_PROTOCOL_ComponentFirmwareUpdate_DECLARATION_DATA`, `0x27470` transport, `0x275c8` `WdfCustomType_DMF_ComponentFirmwareUpdate`, `0x27d88` `WdfCustomType_DMF_ComponentFirmwareUpdateHidTransport`) — these are part of the statically linked DMF Modules.Library (built from `X:\qcdksrc\rel\10.9\b\src\dmf\...`), not evidence this driver performs firmware update. The instantiated DMF module set in `EvtDeviceAdd` (`0x140032f90` → `0x140039338` → `0x14003aa08`) contains no CFU module: the visible module callbacks are `LiveKernelDump` (per `Dmf_LiveKernelDump.c` assert string at `0x140039338`), `BranchTrack`, `IoctlHandler`, `RingBuffer`/`BufferQueue`/`BufferPool` (diagnostics ring buffers; init at `0x14003cef0`).
- IOCTLs: handled through DMF `IoctlHandler` module; no custom CTL_CODEs with hardware semantics were recovered. The user-mode diagnostics interface GUID exposed by DeviceAdd is `{32291ea8-bbab-82a3-7fbd-ce2f631ed947}` (constants `0x82a3bbab32291ea8`/`0x47d91e632fcebd7f` stored at `0x140032f90` frame, file `.rdata` `0x2c0b0`/`0x2c0a8` region) — derived from immediate pair interpretation; treat as a WDF device-interface GUID for the diagnostics interface.

## Power state machine

**No D0/D3 or IRP_MJ_POWER logic** (observed).

- No `WdfInterruptCreate`, no DPC/ISR registration, no `EvtDeviceD0Entry/Exit` callbacks in the client dispatch table (table at file offset `0x26c80..0x26cdc` lists the driver's WDF/DMF callbacks; entries `0x1500,0x1510` are empty stubs, remaining entries are PDO-creation/removal/query-id/`FUN_140003770`(PDO module add) style callbacks, all traced).
- PDO IRP forwarding: `0x140002d18` (file `.text`) — allocates a synchronous IRP to the attached device (`IoGetAttachedDeviceReference` + `IoAllocateIrp(stacksize)`), copies the caller's stack params (`0x20` bytes of Parameters), calls `IoSynchronousCallDriver`, and maps pending `0xC00000BB`-style initial status. This is the generic "pass everything to ACPI" shim used for the children; power policy itself lives below (ACPI) or in the PDO's function driver.
- PDO lifecycle observed: create → `FUN_140033450` (per-instance, matching `ACPI\ACPI0015` compatible id, friendly name `L"USB4 HR PDO"`), remove → `FUN_140033750` → `FUN_1400057d0` (finds PDO by instance id in the device list, calls module Remove, default `0xC00000BB`/STATUS_NOT_SUPPORTED handling), and `FUN_140004b28` (create PDO: builds `WDF_DEVICE_PNP_CAPABILITIES`-style struct, copies hwid/compat-id `L"USB4\QCOM0CD10001"` / `L"ACPI\ACPI0015"` string arrays, checks for duplicate instance ids).
- `FUN_140002660` is the module dispatch: `param_3==0` → create PDO via `FUN_140033450`; `param_3==1` → remove via `FUN_140033750`; anything else → `0xC000000D` (STATUS_INVALID_PARAMETER).

## Strings/constants of interest

All offsets are file offsets in the 346,848-byte PE.

| Offset | Value | Meaning | Confidence |
|---|---|---|---|
| INF | `ACPI\QCOM0C6D` | HWID this bus driver installs on (USB4 host router ACPI node) | observed |
| 0x26548 | `USB4\QCOM0CD10001` (UTF-16) | Child PDO hardware id (Qualcomm USB4 device, `QCOM0CD1` dev id) | observed |
| 0x4e268 | `ACPI\ACPI0015` (UTF-16) | Child PDO compatible id — ACPI0015 is the USB4 host-router ACPI id | observed |
| 0x26590 | `USB4_DEVICE_%02d` | PDO instance-id format (max ~100 children) | observed |
| 0x4e288 | `USB4 HR PDO` | PDO friendly/description text ("HR" = host router) | observed |
| 0x26570 | `QC USB4 BUS 0` | Bus name | observed |
| 0x4f578 | `Qualcomm(R) Bus Device` | FileDescription | observed |
| 0x4df38 | `\Registry\Machine\SYSTEM\CurrentControlSet\Control\LKMDTel` | Live-kernel-dump telemetry config | observed |
| 0x4df10 | `MaxReportSizeInMB` | LKMDTel value | observed |
| 0x2ad98 | `Z:\b\WP\QcUsb4Bus\rel\10.9\ARM64\Release\QcUsb4Bus8380.pdb` | PDB path | observed |
| 0x27dc0/0x27de0 | `USB4BUS_DMF_DEVICE_CONTEXT` / `USB4BUS_DEVICE_CONTEXT` | DMF context type names | observed |
| 0x2c0a8/b0 | GUID `32291ea8-bbab-82a3-7fbd-ce2f631ed947` | DeviceAdd interface GUID (diagnostics) | derived |
| 0x295a8 | GUID `496b6b80...` area: `496b8280-6f25-11d0-beaf-08002be2092f` | WDF REQUEST_REUSE | derived |
| INF | Service `QcUsb4Bus`, `%13%\QcUsb4Bus8380.sys`, `HKR,Parameters,VerboseOn` | WPP IFR verbose flag | observed |
| imports | `WppRecorder.sys`, `ext-ms-win-ntos-werkernel-l1-1-1.dll` (WerLiveKernel*), `WDFLDR.SYS` (WdfVersionBind*), `ntoskrnl.exe` | No HAL/DMA/MMIO imports | observed |

WPP control GUIDs recovered from `.rdata` (telemetry channels, not hardware): `27aa3c9a-…` (0x28a83), `f580eaf6-…` (0x28e76), `e37fd31c-…` (0x2a75a), `06c988d1-…` (0x2b679), plus `52ca…` "Generic" trace channel at 0x2b8c0. No Thunderbolt/USB4 CM `_DSM` GUIDs (`3706E594-…` etc.) are embedded — so this driver does not evaluate USB4 DSM methods itself.

Driver-entry chain (derived): `entry 0x140022000` → `0x140022050` (WdfVersionBind, sets `DAT_14002f2b8` = WDF fn table) → `0x1400223e0`/`0x1400222e0` (KMDF class bind) → `0x140052000` (INIT, registers DMF driver `0x42465551` 'Q U F B' pool tag reverse "QUBF", callbacks `0x140032f90` DeviceAdd / `0x1400039b0` Unload) → `0x140032f90` DeviceAdd → `0x140035890` (bus context init) → PDO module `0x140003770` → dispatch `0x140002660`.

## Prompt-injection check

No instruction-like content was found in the binary's strings; nothing attempted to steer the analysis.

## Limitations

- WDF calls go through the runtime-bound `WDFLDR.SYS` function table (`extraout_x15` indirect calls), so specific WDF API names (e.g., `WdfPdoMarkMissing` vs `WdfChildListUpdate`) are inferred from callback shape, not resolved symbols. Gaps in per-callback semantics remain.
- No PDB was available; all function names are Ghidra auto-names. Address-level citations above are stable, name-level claims are derived.
- Ghidra decompiler output for the IRP forwarder collapsed some WDF request helpers; the exact set of IRP majors forwarded is inferred from the stack-copy (`+0x38` write, `0x20`-byte parameter copy, initial status `0xC00000BB`) rather than enumerated switch cases.
- This report only covers `QcUsb4Bus8380.sys`. The USB4 controller MMIO/firmware behavior for PR #76 must be evidenced from the *other* driver in the package family (e.g., a `usb4hostrouter`/host-controller function driver servicing `USB4\QCOM0CD10001`) or from ACPI `QCOM0C6D` `_CRS`/`_DSM` on-device. Recommend analyzing sibling SurfaceUpdate packages next.
- Snapshot/analysis retained at `…/T/opencode/rea-usb4/` (Ghidra session was ephemeral; `qcusb4.snapshot` + full decompile corpus `decomp/functions/*.c` are kept for re-verification).
