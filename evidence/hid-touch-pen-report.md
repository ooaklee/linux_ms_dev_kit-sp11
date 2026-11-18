# HID / Touch / Pen Driver Reverse-Engineering Report — Surface Pro 11 (ARM64)

Source: `SurfacePro11_ARM_Win11_26100_26.041.12746.0/SurfaceUpdate/`
Date: 2026-11-12
Scope: `cfuoverhid/`, `hidmini/`, `touchpenprocessor0c83update/` (+0c80), `touchg6fwupdateextnpackage0c80/0c83`, `touchpadg7cfuoverhidextnpackage/`, `pen0c0ffirmwareupdate/`, `penblelcaddradaptation/`, `digitizerwotextnpackage/`, `hidfriendlynames/`, `hidclasssettingsextensionpackage/`
Relevance: linux-surface-pro OE issues #57 (mshw0485 touchscreen binding), #69 (pen eraser/lifecycle).

> Method note: the REA MCP namespace was **not registered** in this subagent's
> Code-Mode runtime (`search` found no `rea` tools; `tools.rea` undefined;
> no MCP resources exposed). Analysis fell back to local static extraction:
> `strings`-equivalent scans with file offsets, INF parsing, and hex dumps of
> `.offer/.payload` firmware containers. All offsets below are **file
> offsets** into the named PE binary. Confidence marks: [H]=high, [M]=medium, [L]=low.
> No REA `close_binary` handle existed to close (see Limitations).

---

## 1. Per-driver findings

### 1.1 SurfaceCFUOverHid.dll (cfuoverhid/) — CFU firmware-update transport over HID [H]

PE32+ Aarch64 DLL (UMDF, WDF), DriverVer 06/21/2024, 5.70.15.0.

Built on Microsoft's **DMF (Driver Module Framework) ComponentFirmwareUpdate V2**:
- `0x429c0` `DMF_INTERFACE_PROTOCOL_ComponentFirmwareUpdate_DECLARATION_DATA`
- `0x42bc0` `WdfCustomType_DMF_ComponentFirmwareUpdateV2`
- `0x42b80` `WdfCustomType_DMF_ComponentFirmwareUpdateHidTransportSurface` — a **Surface-specific** HID transport customization on top of the stock `ComponentFirmwareUpdateHidTransport`.

Offer/response state machine strings (protocol constants, `0x44450`–`0x44830`):
`OFFER_INFO_START_ENTIRE_TRANSACTION`, `START_OFFER_LIST`, `END_OFFER_LIST`,
`OFFER_COMMAND_NOTIFY_ON_READY`, `OFFER_SKIP/ACCEPT/REJECT/BUSY`,
`OFFER_COMMAND_READY`, `OFFER_COMMAND_NOT_SUPPORTED`,
`OFFER_REJECT_{OLD_FW, INV_MCU, SWAP_PENDING, MISMATCH, BANK, PLATFORM, MILESTONE, INV_PCOL_REV, VARIANT}`.
These match the public Microsoft CFU protocol (offer → offer-response → payload → payload-response) [H].

Transport plumbing (`0x44ff8`–`0x45060`): `ThreadedBufferQueueInputReport`,
`BufferPoolInputReports`, `ContinuousRequestTargetInputReport`,
`BufferPoolContextHidFeatureGetAsynchronous` — i.e. asynchronous HID **GET Feature** for offer/payload responses, INPUT report reader for notifications. [M]

INF hardware IDs (`SurfaceCFUOverHid.inf`): loads on the CFU vendor TLC
`HID\VID_045E&UP:FF0B_U:0101` and `HID\VID_04F3&UP:FF0B_U:0101` (Elan).
⇒ **Usage Page 0xFF0B, Usage 0x0101 is the Surface CFU top-level collection**, the same TLC Linux `surfacefirmware`/`hid-surfacecfu`-style drivers must match. [H]

Telemetry (`0x45990`+): `CFUProtocolTransactionStarted`, `CFUTransactionTimeStamp`,
`CFUProtocolComponentUpdateStatus`, `numberOfFirmareItems`, `firmwareFilePath`,
`productId`, `instanceId`. Registry-provided firmware files via `HKR,CFU\<Component>,Offer/Payload` (seen in extension INFs below). [H]

### 1.2 TouchPenProcessor0C83.dll (touchpenprocessor0c83update/) — 9.5 MB touch/pen processor (Elan) [H overall]

Targets `HID\MSHW0485&Col02` (INF). The 0C80 sibling targets `HID\MSHW0484&Col02`.

**Pen report synthesis and lifecycle — directly relevant to issue #69.**
Trace-format strings at `0x80a04b`–`0x80a0eb` define the pen HID report fields this DLL emits to the OS:
```
PenProcessingManager_PreparePenReportToOs: ReportID, PenStatus, XPosition,
YPosition, PressureStatus, ScanTime, BatteryStatus, RegionIndex, PenVendorId
```
Pen lifecycle functions (strings):
- `0x80a31e` `PenProcessingManager_ResetOnPenLift`
- `0x80aa53` `ReporterPenLift` / `0x8089ca` `PenLiftReason - by FW ePEN_LIFT_MESSAGE_REASON` (pen lift reason comes **from firmware**)
- `0x80a351`–`0x80a43d` `PenProcessingManager_PenIdFiltering` (Pen Id Received / legacy Id 0 / local id, per-vendor pen database `0x80a236` UpdatePersistentStorage)
- `0x80a107` `PenProcessingManager_UpdatePenStudyStats` (pressure bins)

**Eraser / invert handling [H]** — trace field names at `0x80aabe`–`0x80aaf3`:
```
InRange, Invert (0x80aace), EraserSwitch (0x80aad7), SideSwitch (0x80aae6),
BatteryStatus, MaxLogicalSizeX/Y, MaxPhysicalSizeX/Y
```
plus `0x80e0dd` `PenPostProcess_EraserUsageCount` — a telemetry counter for
eraser usage. The processor explicitly models HID Digitizer (Usage Page 0x0D)
Invert/Eraser/SideSwitch bits and Barrel-Switch semantics: eraser is expected
to be delivered as Invert+Tip (or dedicated EraserSwitch) per the Windows pen
spec. For Linux: this confirms the device-side report contains genuine
eraser/invert state; if eraser doesn't work under `hid-multitouch`/`hid-microsoft`
mshw0485 handling, the bits exist at the firmware report level and a quirk in
the Linux HID parsing path is the likely cause, not missing firmware support.

Touch reports (`0x80ac21`): `Height, Confidence, InRange, MaxLogical/Physical
SizeX/Y, FrameIndex, TimeScan` — standard Windows Precision Touchpad/Touch
report shape. Touchpad reports likewise (`0x80ae3e`). Report descriptor is
read at runtime (`0x81290e aHidReportType`, `0x812af5 SaveDescriptor_GetCollectionInformationFailed`) — **the HID report descriptor is not embedded in the DLL**; it lives in touch-controller firmware/ACPI. [M]

Feedback channel: `SendOsDataByFeedback / SendDataByFeedback` (`0x80a8f4`–`0x80a980`) — OS→device data goes via HID **Feature** reports ("Feedback", the standard PTP/pen feedback usage), including persistent storage and calibration data (`0x7f9598` CalibrationMaps...). [M]

CFU counters present: `0x80b001 CfuFailuresCounters_Incell` — the touch
processor firmware itself is CFU-updatable **in-cell** (see G6 packages below).

### 1.3 SurfaceHidMiniDriver.sys (hidmini/) — SAM/KIP generic HID mini [M]

PE32+ native Aarch64, 3.98.18.0. A DMF-based "GenericHidModule" exposing a
HID mini-driver over the Surface SAM/KIP (embedded controller) transport.
INF matches `ACPI\MSHW0096` (legacy Sam keyboard) and `B2C18583-…\SHMD_DEVICE`,
`{21635d77-…}\Target_KIP&Category_HID`, `Target_SAM&Category_HID`,
`{2DEDC554-…}` (multi-instance). No touch/pen relevance; report descriptor is
queried dynamically (`GenericHidProcessGetDescriptor.ReportDescriptor`,
`0x2a810`), not embedded. Not needed for mshw0485 binding.

### 1.4 Touch G6 firmware packages (MSHW0484 / MSHW0485) — issue #57 relevance [H]

- `touchg6fwupdateextnpackage0c83/SurfaceTouchG6FwUpdateExtnPackage0C83.inf`:
  - HW id: `HID\VEN_MSHW&DEV_0485&Col06` — note **VEN_MSHW** (ACPI HID-style
    enumerated HID device, column/collection 06), *not* USB VID/PID.
  - `HKR,CFU\FW_MSHW0485,Offer → SurfaceTouchG6_FW_MSHW0485.offer.bin`,
    `Payload → …payload.bin`. FW version 63.20.137 (0x3F001489).
- `touchg6fwupdateextnpackage0c80/` analog: `HID\VEN_MSHW&DEV_0484&Col06`, same CFU registry layout.

Offer bin header (MSHW0485, 16 bytes, file offset 0):
```
00 00 12 00 89 14 00 3f ff ff ff ff 04 04 75 00
```
Layout consistent with DMF CFU offer record: component size/offset header,
version 0x3F001489 (little-endian at +4: `89 14 00 3f`), `ff ff ff ff`
hardware-version mask (ignore), flags/component-id trailing bytes
(`04 04 75 00`). Pen offer (MCU-B0): `00 00 10 00 8b 8e 00 05 …` version
0x05008E8B. Touchpad G7 offer: version 0x41003F8B, mask 0x41. [M on exact field mapping]

Implication for Linux: the mshw0485 touch controller is a **multi-collection
HID device**: Col01/Col02 = touch digitizer/pen processor TLCs consumed by
TouchPenProcessor0C83.dll, **Col06 = CFU (FW update) collection**. A Linux
driver can bind per-collection; touch input and CFU update are independent. [H]

### 1.5 Touchpad G7 CFU (touchpadg7cfuoverhidextnpackage/) [H]

INF: `HID\VID_045E&PID_0C8D&Col01&UP:FF0B_U:0101` — a **USB HID** touchpad
(Type Cover) using the same FF0B/0101 CFU TLC as SurfaceCFUOverHid.
Firmware `FW_TargetKIPLsCfu` v65.63.139. Confirms CFU-over-HID usage
page 0xFF0B usage 0x0101 is used across USB (045E:0C8D), Elan (04F3), and
ACPI-VEN_MSHW devices — one protocol, three bus flavors.

### 1.6 Pen firmware (pen0c0ffirmwareupdate/) — Slim Pen over BLE [H]

INF: `HID\{00001812-0000-1000-8000-00805f9b34fb}_Dev_VID&02045e_PID&0C0F&Col03`
(BLE HID-over-GATT, VID 045E PID 0C0F, collection 03). CFU components:
`CFU\BLE-HI`, `CFU\BLE-LO`, `CFU\HAPTIC`, `CFU\MCU-B0` — the pen itself has
updatable MCU, haptic engine, and BLE stacks, each with 16-byte offer +
payload bins. Pen firmware update happens over the pen's BLE HID CFU
collection, entirely separate from the digitizer stack.

### 1.7 penblelcaddradaptation, typecoverv7fprude, digitizerwotextnpackage, hidfriendlynames [brief]

- `SurfacePenBleLcAddrAdaptationDriver.sys`: matches many
  `HID\VID_045E&PID_09xx&Colxx&UP:FFF4_U:0001` BLE pen IDs (096D, 099F, 09AD,
  09B0, 09B1, …) on vendor page **0xFFF4 usage 0x0001** — BLE address adaptation, not input. [M]
- `SurfaceTypeCoverV7FprUdeDriver.sys`: Type Cover fingerprint UDE (USB device emulation); `typecoverv7fwupdate/` carries TypeCoverV7 `.cfu` files (KIP_APP1, WLC2). Not digitizer-related.
- `digitizerwotextnpackage/` (INF only, no binary): matches `ACPI\MSHW0274, 0484, 0485, 0569, 0584, 0585` — device-targeting metadata for the digitizer WoT stack; confirms **MSHW0485 is the SP11 touch/pen processor ACPI id**. [H]
- `hidfriendlynames/` (INF only): friendly names for `ACPI\ELAN9038,
  MSHW0238, 0460, 0468` and `HID\VID_045E/04F3/1FD2/2465`. VID **1FD2** and
  **2465** also appear (Synaptics/other touch vendors used on other Surfaces). [H]

## 2. CFU protocol summary (for a Linux `surface CFU` implementation) [H on structure]

1. TLC: Usage Page **0xFF0B**, Usage **0x0101** (USB/BLE CFU; MSHW devices expose an equivalent collection, e.g. Col06 on mshw0485).
2. Host → device: **Output/Feature reports** containing sequential OFFER records (16 bytes: header, FW version LE32, HW mask, flags/component id) then PAYLOAD blocks.
3. Device → host: responses via **GET Feature** (async) and INPUT reports
   (`BufferPoolContextHidFeatureGetAsynchronous`, `ContinuousRequestTargetInputReport`).
4. Offer response codes: skip/accept/reject/busy + reject reasons (old FW, invalid MCU, swap pending, mismatch, bank, platform, milestone, protocol revision, variant).
5. Offer-list framing: START_ENTIRE_TRANSACTION / START_OFFER_LIST / END_OFFER_LIST / NOTIFY_ON_READY.
6. Firmware payloads ship as `.offer.bin` + `.payload.bin` pairs referenced from registry (`HKR,CFU\<Component>{,Offer,Payload}`), version comments in INFs.

This is the standard Microsoft CFU spec (also implemented upstream in
Linux `hid-microsoft` surface ecosystem work / `surface-aggregator` CFU
userspace tooling), with a Surface transport wrapper
(`…HidTransportSurface`) whose deltas were not visible in strings alone. [M]

## 3. VID/PID / hardware-ID table

| ID | Bus/Enum | Role | Evidence |
|---|---|---|---|
| ACPI MSHW0485 | ACPI VEN | SP11 touch+pen processor (digitizer), CFU in Col06 | touchpenprocessor0c83 INF; touchg6_0c83 INF; digitizerwot INF |
| ACPI MSHW0484 | ACPI | sibling touch processor (SP11 oled variant?) | touchg6_0c80, digitizerwot INFs |
| ACPI MSHW0274/0569/0584/0585 | ACPI | other digitizer targets | digitizerwot INF |
| HID\VEN_MSHW&DEV_0485&Col06 | HID coll. | CFU FW-update collection | touchg6_0c83 INF |
| HID\MSHW0485&Col02 | HID coll. | touch/pen processing (UMDF client) | touchpenprocessor0c83 INF |
| USB 045E:0C8D Col01 | USB HID | Type Cover G7 touchpad, CFU TLC FF0B/0101 | touchpadg7 INF |
| USB/BLE 045E:0C0F Col03 | BLE GATT | Slim Pen CFU (MCU/BLE-HI/LO/HAPTIC) | pen0c0f INF |
| VID 045E / 04F3 / 1FD2 / 2465 | HID | CFU/Elan/friendly-name targets | SurfaceCFUOverHid.inf, hidfriendlynames INF |
| VID_045E PID_09xx UP:FFF4/0001 | BLE HID | pen BLE LC address adaptation | penblelc INF |
| VID_045E&UP:FF0B_U:0101 | HID TLC | generic Surface CFU binding | SurfaceCFUOverHid.inf |

## 4. Linux-support implications

- **#57 (mshw0485 binding)**: mshw0485 enumerates as a HID device with at
  least Col02 (input processing, Windows UMDF client) and Col06 (CFU).
  Linux should bind `hid-multitouch` (or a Surface quirk driver) to the
  digitizer collection while leaving/owning Col06 for firmware update.
  Multi-Touch input reports are produced natively by the device/UMDF shape
  (Confidence/Height/TimeScan/FrameIndex fields); TouchPenProcessor0C83.dll
  is a Windows-side post-processor (ML palm rejection, "Pen study stats",
  calibration), i.e. **basic touch should work without it**, quality
  features (palm rejection tuning, calibration maps) are Windows-side. [M]
- **#69 (pen eraser/lifecycle)**: firmware pen reports contain Invert,
  EraserSwitch, SideSwitch, InRange, Pressure, Battery, ScanTime, and
  pen-lift reasons are firmware-reported (`ePEN_LIFT_MESSAGE_REASON`). Linux
  eraser breakage is therefore a HID-core/report-descriptor or
  hid-multitouch quirk issue, not absent firmware capability. Check that the
  Digitizer collection's Invert/Eraser usages survive hid parsing for this
  descriptor and that pen-lift (MSC scan time / tool reset) is honored. [M]
- Firmware update path fully mapped: FF0B/0101 CFU with offer/payload bins
  that could be replayed from Linux userspace via hidraw feature/output
  reports. [M]

## 5. Limitations

- **REA MCP unavailable**: `tools.rea.*` not registered in this subagent
  runtime; no disassembly/DB handles were opened, so no `close_binary` call
  was needed or possible. All findings come from strings/INF/hex static
  analysis — no ARM64 instruction-level verification of the CFU
  "HidTransportSurface" deltas (e.g., Surface-specific report IDs/headers)
  was performed. Field-exact offer-record mapping is inferred from
  offsets/version constants and the public CFU spec, not decompiled code. [M]
- The actual HID report descriptor for mshw0485 is not in any analyzed
  binary (fetched at runtime from device); it must be captured on-hardware
  (`hid-decode`/`usbhid-dump` equivalent) to finalize Linux quirks.
- TouchPenProcessor0C83.dll is 9.5 MB with embedded ML models (XGBoost
  edge features at `0x80513a`); only its HID-facing interface was analyzed.
- No modification was made to repo sources; only this evidence file was written.
