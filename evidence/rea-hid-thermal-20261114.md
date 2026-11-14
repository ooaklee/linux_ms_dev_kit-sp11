# REA HID & Thermal evidence — SP11 SurfaceUpdate (2026-11-14)

Pipeline: REA (`rea analyze` / `rea search <path> <pat> --provider ghidra --format json`) + Ghidra 12.1.4.
Raw JSON persisted to `/tmp/rea-hid-<name>.json` for: hidmini, button, cfuoverhid, qcsubsysthermalmgr,
qcsensors, powermeter, powertrackercore (plus `/tmp/rea-s-*.json` for `rea search` result sets).
Source: `SurfacePro11_ARM_Win11_26100_26.041.12746.0/SurfaceUpdate/`.

Builds on (does not repeat): `evidence/hid-touch-pen-report.md` (2026-11-12, CFU offer-bin header
decode, touch G6 offer bytes, MSHW0485 binding) and `evidence/battery-thermal-report.md`
(2026-11-12, SurfaceThermalPolicy/PoFx, power-meter DSM adjacency). Method follows
`evidence/rea-audio-conn-20261114.md`. Confidence: **[H]** strings/INF, **[M]** inference, **[L]** low.

---

## 1. HID device IDs and vendor usage pages (touch/pen)

### 1.1 Complete Surface HID usage-page map [H]

`hidfriendlynames/SurfaceHIDFriendlyNames.inf` (2.27.16.0) enumerates the entire VID_045E
collection namespace. Rows most relevant to SP11 touch/pen bring-up (`VID_045E` unless noted):

| HID ID | Meaning | Linux coverage |
|---|---|---|
| `UP:000D_U:0004` | Touch screen (hub/TP) | yes (`hid-multitouch`) |
| `UP:000D_U:0002` | Touch pen (hub/TP) | yes (`hid-multitouch` MT_TOOL_PEN) |
| `UP:000D_U:0005` / `UP:000D_U:000E` | Touchpad / touchpad config | PTP (`hid-multitouch`/`i2c-hid`) / config **no** |
| `UP:FF0F_U:0050`, `U:0051` | **Digitizer utility** (vendor) | **no** — calibration/diagnostics |
| `UP:FF0F_U:0100` | **Ink** (vendor digitizer) | **no** |
| `UP:FF0F_U:0200` | **Color profile** (digitizer) | **no** |
| `UP:FFFF_U:0001` | **Touch utility** (vendor page FFFF!) | **no** |
| `UP:FFB1_U:0060`-style `FFA1_U:0060` | **Surface Pen firmware update** (vendor) | **no** (pen FW update path) |
| `UP:FFF4_U:000A` | **Pen Bluetooth OOB coupling** | **no** (pen BLE pairing helper) |
| `UP:FF0B_U:0101` | **CFU firmware-update TLC** (MS standard CFU vendor page) | **no Linux CFU class driver** |
| `UP:FF0B_U:000B` | SID fw update | no |
| `UP:FF0A_U:0010..0013` | Dock/Hub SMC: generic/CFU/SCP | no (dock only) |
| `UP:FF0A_U:0030..0032`, `U:0039` | Hub audio gen/CFU/comms; hub touch CFU | no (dock only) |
| `UP:FF07_U:0001`, `U:0013` on `0001` | Surface "Nav"/sensor fusion usage | no |
| `UP:0001_U:0080` | System controller | partial |
| `VID_2465 UP:FF00..FF04` | Touch-back (keyboard-side) mouse/kbd/comms/touch/**pen** | no (type-cover class HW) |

External touch controllers seen in the same INF: `ACPI\MSHW0468`, `ACPI\ELAN9038`
(I²C touch), `HID\VID_1FD2&UP:FA00_U:00F5` (touch FW update), `VID_04F3&UP:01FF_U:0001`
(Elan vendor) — none apply to SP11's in-cell MSHW0485 stack but they confirm the
`04F3` (Elan) second source for the CFU transport (below).

**Linux gap summary**: beyond standard PTP/MT collections, the digitizer exposes at least
five vendor collections — FF0F:0050/0051/0100/0200 and FFFF:0001 — with no Linux binding.
FF0F:0100 ("Ink") and FFFF:0001 are the highest-value unknowns for pen latency/feature
parity (likely hover/tilt config, ink streaming, factory calibration). [M]

### 1.2 Where each MSHW device binds [H]

- `ACPI\MSHW0485` (SP11 touch/pen processor, 0C83) / `MSHW0484` (0C80): extension INFs
  `SurfaceDigitizerWoTExtnPackage` (adds `input.inf WakeScreenOnTouch`), class-settings
  extension, and CFU extension `HID\VEN_MSHW&DEV_0485&Col06` (see §2).
- `SurfaceTouchPenProcessor0C83Update.inf`: UMDF updater on **`HID\MSHW0485&Col02`** —
  note this is a *column* of the touch processor's HID device (Col02 = update collection),
  consistent with the report descriptor living in controller firmware
  (`hid-touch-pen-report.md` §1.2). [H]
- USB IDs in the class-settings extension: `USB\VID_045E&PID_096D&MI_01`, `PID_09AD`,
  `PID_09B7&MI_01` — these are dock/type-cover HID bridges; **none are in
  `drivers/hid/hid-ids.h`** (checked tree: no 096D/09AD/09B7) — worth adding for
  type-cover-on-dock support. [H]
- BLE pens: `BTHLEDevice\{00001812-...}_Dev_VID&02045e_PID&0C7A/0C7B` (HID-over-GATT
  pens, class-settings) and `PID_0C0F` pen (FW update, §2). `02045e` is the GATT
  Vendor ID Source=0x2 (USB IF) + VID 045E encoding. [H]

### 1.3 SurfaceHidMiniDriver.sys (REA, stripped, 787 procs / 516 strings) [H]

- SAM/KIP generic HID mini over SSAM/SSH: UTF-16 strings `0x14002fce0`
  `{2DEDC554-...}\Target_SAM&Category_HID`, `0x14002fd60` `{21635d77-...}\Target_KIP&Category_HID`
  (+multi-instance `{2DEDC554-...}` KIP/SAM), `0x14002fde0` `ACPI\VEN_MSHW&DEV_0096`.
- DMF `GenericHid` module: descriptor path strings `GenericHidProcessGetDescriptor.{Device,
  Report}Descriptor` (`0x14002b3c0/0x14002b410`), IOCTL cases
  `IOCTL_HID_{GET_DEVICE_DESCRIPTOR,GET_REPORT_DESCRIPTOR,READ_REPORT,WRITE_REPORT,
  GET_INPUT_REPORT}` (`0x14002c388`–`0x14002c6c8`); report descriptor fetched from the
  EC at runtime — nothing embedded. Confirms Linux `surface-hid` (SAM/KIP TC HID)
  already models this correctly; no new commands needed. [H]

### 1.4 SurfaceButton.sys (REA) [H]

- INF: `ACPI\MSHW0028`, `ACPI\MSHW0040`; LowerFilters=Vhf (virtual HID). Friendlynames:
  `VHF\VID_045E&PID_F001/F002`.
- REA strings: button IDs only Power/VolumeUp/VolumeDown
  (`SurfaceButtonVhfButtonIdTranslate.*`, `0x14002b8c8`–`0x14002b980`); ACPI notify
  codes for POWER/VOLUME up/down plus **BASE_DETACHED** (detach/attach top, form-factor
  GPIO read: `SurfaceButtonGpioReadStateAndSetFormFactor`, `0x14002daf8`); DMF module
  `Dmf_HidPortableDeviceButtons.c` with Get/SetFeature (incl. `DisablePowerButton`).
- Linux: `surface_button` (SAM) + `soc_button_array`/GPIO — for SP11 the ACPI-notify
  path (MSHW0028/0040) with power/volume/base-detach notify codes is what Windows
  uses; base-detach (tablet mode) is not wired in Linux for this device. [M]

---

## 2. CFU-over-HID: components and protocol shape

### 2.1 Which components update through it [H]

`SurfaceCFUOverHid.dll` (UMDF 5.70.15.0) binds `HID\VID_045E&UP:FF0B_U:0101` and
`HID\VID_04F3&UP:FF0B_U:0101` — the **Microsoft CFU vendor usage page 0xFF0B usage
0x0101** TLC, loaded "directly over the HID TLC". INF device-settings keys:
`VendorID=0x45E`, `NumberOfInputReports=2`, `UsageTableIndex=0`,
`SupportProtocolSkipOptimization=0`, `SupportResumeOnConnect=0`.

CFU components discovered across the bundle (offer/payload pairs in `HKR,CFU\<name>`):

| Component | Registry name | Bind ID | FW |
|---|---|---|---|
| Touch processor G6 in-cell (0C83) | `FW_MSHW0485` | `HID\VEN_MSHW&DEV_0485&Col06` | 63.20.137 (see `hid-touch-pen-report.md` §1.4) |
| Touch processor G6 (0C80) | `FW_MSHW0484` | `HID\VEN_MSHW&DEV_0484&Col06` | — |
| Touchpad G7 (KIP touchpad MCU) | `FW_TargetKIPLsCfu` | `HID\VID_045E&PID_0C8D&Col01&UP:FF0B_U:0101` | 5.36.313.90 |
| Pen 0C0F — BLE host hi/lo, pen MCU, haptic | `BLE-HI`, `BLE-LO`, `MCU-B0`, `HAPTIC` | `HID\{00001812-...}_Dev_VID&02045e_PID_0C0F&Col03` (BLE HID-over-GATT!) | 4 banks |
| Touch/pen processor host updater | — | `HID\MSHW0485&Col02` (UMDF `TouchPenProcessor0C83.dll`) | — |

So: **touch and touchpad MCUs update over FF0B:0101 CFU; the pen updates its own
MCU (MCU-B0), both BLE stacks, and the haptic engine over CFU carried on
HID-over-GATT (Col03 of the pen's BLE HID device)** — not over the digitizer's
I²C/USB HID. Dock/hub components (FF0A:0011/0031/0039) use the same transport on
dock hardware. [H]

### 2.2 Protocol shape (REA on SurfaceCFUOverHid.dll) [H]

DMF-based: `DMF_INTERFACE_{PROTOCOL,TRANSPORT}_ComponentFirmwareUpdate_DECLARATION_DATA`
(`0x180044fc0/0x180045000`), `WdfCustomType_DMF_ComponentFirmwareUpdateV2`
(`0x1800451c0`) + **`WdfCustomType_DMF_ComponentFirmwareUpdateHidTransportSurface`**
(`0x180045180`) — a Surface-specific HID transport on top of stock
`ComponentFirmwareUpdateHidTransport`. Context types `CONTEXT_ComponentFirmwareUpdateV2
{Transport,TransportCallback,Transaction,Offer,Payload}` (`0x1800467b0`–`0x180046870`).

Offer/response state machine — enumerated protocol constants (`0x180046a50`–`0x180046e30`):

- Transaction markers: `OFFER_INFO_START_ENTIRE_TRANSACTION`, `START_OFFER_LIST`,
  `END_OFFER_LIST`
- Host→device offer commands: `OFFER_COMMAND_NOTIFY_ON_READY`, `OFFER_COMMAND_READY`,
  `OFFER_COMMAND_NOT_SUPPORTED`
- Device→host responses: `OFFER_{SKIP,ACCEPT,BUSY,REJECT}`, reject reasons:
  `REJECT_OLD_FW, REJECT_INV_MCU, REJECT_SWAP_PENDING, REJECT_MISMATCH,
  REJECT_BANK, REJECT_PLATFORM, REJECT_MILESTONE, REJECT_INV_PCOL_REV, REJECT_VARIANT`
- Offer record log format `Offer:%X%X%X%X` (4×32-bit words — the 16-byte offer header
  already decoded in `hid-touch-pen-report.md` §1.4); `OfferFwVersion`,
  `ResumePayloadBinRecordIndex`, `ResumePayloadBufferBinRecordDataOffset` (payload
  resume mid-stream); `firmwareVersionOfferedToDevice`, `componentOfferResponseReason`,
  `componentPayloadResponse` (ETW).
- HID mechanics: HID **Feature** reports, async pool
  `BufferPoolContextHidFeatureGetAsynchronous` (`0x180047630`), `UsagePage`/`Usage`
  strings at `0x1800464a8/0x1800464c0` — transport addresses the TLC by usage
  (FF0B:0101), with `UsageTableIndex` selecting among usage sets [M].

**Linux implication**: this is the standard Microsoft DMF CFU protocol (open spec in
DMF `Dmf_ComponentFirmwareUpdate.md`: offer/payload Feature-report commands, the
constants above map 1:1). A Linux `hid-cfu` transport driver would: locate the
FF0B:0101 collection, send offer records (4 dwords + component header) and payload
blocks via SET_FEATURE, poll GET_FEATURE for the response codes above. Component
inventory for SP11: touch G6 (MSHW0485 Col06), touchpad G7 (KIP), pen over BLE. [M]

---

## 3. Thermal: sensors and trip mechanisms vs Linux

### 3.1 qcSubsysThermalMgr.sys (REA + INF) [H]

- INF: kernel driver (KMDF 1.33) on **`ACPI\QCOMFFE0`** (Subsys Thermal Mitigation
  Device) and **`ACPI\QCOM06E5`** (Subsys Base Thermal Mitigation Device); SoC-device
  category `{5D078032-...}`. Neither QCOM ID is referenced anywhere in this kernel
  tree (checked: `QCOM[0-9A-F]{4}` grep of evidence + drivers) — Linux binds nothing
  to these ACPI nodes.
- REA: no embedded trip points/thresholds. Transport is **QMI-over-IPCR**:
  `qmi_cci_*` client (source paths `X:\qcdksrc\...\qmi\qcci\km\*`), `ipcr_xport_open`,
  `ipc_router_core_open_with_options`, server discovery
  `ipc_router_core_find_all_servers failed for svc_id %d` (`0x140021760`). I.e.
  Windows talks to a **thermal-mitigation QMI service on a remote subsystem**
  (ADSP/CDSP via the IPC router; Linux equivalent = QRTR/QMI) for subsystem
  throttling decisions. Contexts: `SUBSYS_THERMAL_*`, `MITIGATION_DEVICE_CONTEXT`.

### 3.2 qcSensors.dll (REA + INF) [H]

- INF: UMDF SensorsCx on `ACPI\QCOM0CE7` (+ `MSHW0464/0488/0544/0568` —
  Surface-specific sensor ACPI IDs, also unhandled in Linux).
  `REGISTERED_SENSORS`: rotv, gyro_cal, mag_cal, gravity, accel, gyro, mag, sar,
  proximity, game_rv, color, **hinge_angle**, pressure, transport_ppe.
- REA: talks to the **SSC/SLPI** over fastRPC+QMI (`sns_client.pb.h` protobuf API,
  `/Qualcomm/fastRPC/persist/sensors/registry/registry` sensor registry,
  `\libadsprpc.dll`, "could not find sensors QMI service due to timeout"); per-sensor
  protos (`sns_hinge_angle.proto`, `sns_proximity.proto`). This is Qualcomm's
  Sensorssee/sns-client architecture — Linux counterpart exists for some SoCs
  (`qcom sees`/`fastrpc` + `sns_cm`), not enabled for X1E here.
- Thermal relevance: these are motion/environment sensors, **not temperature**;
  but the same SSC path carries surface/battery-prox sensors Windows blends into
  thermal policy (via `thermalpolicy`/PoFx per `battery-thermal-report.md` §3). [M]

### 3.3 Gaps vs Linux thermal zones

- Windows stack: numeric trip points live in ACPI DSDT/`\_TZ` + PEP
  (`battery-thermal-report.md` §6); subsystem mitigation is a QMI service behind
  IPCR (this report); passive cooling brokered by PoFx. Kernel-side Linux on this
  tree has tsens / adc-tm5 / lmh, which expose zones but have **no client for the
  QCOMFFE0/QCOM06E5 mitigation QMI service** — meaning Linux can read SoC temps
  but cannot ask the ADSP/CDSP to mitigate (modem/camera/DSP thermal). [M]
- Power telemetry (useful as thermal inputs): `SurfacePowerTrackerCore.sys`
  (KMDF, interface `{d69c83f8-...}\SurfacePowerTrackerCore`) subscribes to SAM
  **notifications and polls per-rail power over SSAM**: REA strings
  `SamInterfaceProcessSamSshRequest` plus channel tables `I_RTS_SAM_1..8`
  (rail-current?), `I_VTS_SAM_9..13`, `I_RTS_KIP_1..8`, `I_VTS_KIP_9..12`,
  `PowerIn/PowerOut`, `I/O_LOW_POWER_MODE`, controls `Power{Input,Output,
  DeliveryInput}Control{,Mask}`. `SurfacePowerMeterDriver.sys` (KMDF, Sensor class)
  exposes EMI v1/v2 (`EMI_GET_METADATA`, `EMI_EX_GET_CURRENT/VOLTAGE`) over three
  buses: MAX34407/MAX34417 on SPB, **MeterTypeSAM "HID Power Meter on SAM"**
  (`{5AE6B5A3-26AE-4FCF-B774-6F7EB76FD072}\SurfacePowerMeter`, BusTypeHid), virtual;
  channel metadata from ACPI `_DSM` (rev 0) `DSM_CHANNEL_INFO`. Sample rates
  1024 Hz normal / 8 Hz power-saving. Both are kernel drivers (not user-mode-only),
  so they were analyzed. Linux gap: no surface power-meter/rail-telemetry driver;
  SAM/KIP RTS/VTS channels are unclaimed SSAM request space. [H]

---

## 4. Actionable follow-ups

1. `hid-ids.h`: add 0x045e:0x096D/0x09AD/0x09B7 (dock HID bridges), 0x0C8D (touchpad
   G7), 0x0C0F/0x0C7A/0x0C7B (pen BLE), MSHW0468/ELAN9038 aliases — cheap wins for
   binding coverage.
2. Probe the FF0F:0100 ("Ink") and FFFF:0001 collections of MSHW0485 from Linux
   (hidraw) — decode report descriptors to see whether hover/latency features live
   there.
3. Prototype a Linux CFU HID transport (usage FF0B:0101) targeting the touch G6
   (MSHW0485 Col06) first — firmware `.offer/.payload` bins ship in the G6 packages.
4. Thermal: dump DSDT `\_TZ` for trip points (per `battery-thermal-report.md` §6);
   identify the QMI thermal-mitigation service (svc_id via QRTR on a live device)
   behind QCOMFFE0 to scope a `qcom-subsys-thermal` mitigation client.

---

## 5. Artifact index

| File | Content |
|---|---|
| `/tmp/rea-hid-{hidmini,button,cfuoverhid,qcsubsysthermalmgr,qcsensors,powermeter,powertrackercore}.json` | `rea analyze` inventory JSON |
| `/tmp/rea-s-{cfu,cfu2,button,thermal,qcsensors,powermeter,powertracker}.json` | `rea search` result sets backing each section |
