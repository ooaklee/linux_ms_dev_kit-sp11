# REA display evidence — SurfaceUpdate SP11 payload analysis (2026-11-14)

Pipeline: REA 6.9.0 + Ghidra 12.1.4 (`rea analyze <file> --provider ghidra --format json`),
string hits + INF cross-referencing (same method as `evidence/rea-audio-conn-20261114.md`).
Raw JSON persisted to `/tmp/rea-display-<name>.json` for: paneldriver, displayhardware, hotplug.
Source package: `SurfacePro11_ARM_Win11_26100_26.041_12746.0/SurfaceUpdate/` — directories
`panel/`, `displayhardware/`, `oempanelcustomizationlcd/`, `oempanelcustomizationoled/`,
`Tconcellularlcd/`, `Tconwifionlylcd/`, `Tconoled/`, `dppsext/`, `hotplug/`.

> Tooling note: binaries are symbol-stripped; `rea analyze` returns segment/procedure inventories
> only (paneldriver: 9 segs / ~1690 procs; displayhardware: 326 KB .text / 1690 procs / 1154 strings;
> hotplug.sys: KMDF+DMF kernel driver). All substantive findings below come from Ghidra-anchored
> string inventories and INF parsing.

## 1. Panel identity — Monitor\HW IDs (EDID-derived)

`panel/SurfacePanelDriver.inf` (DriverVer 09/23/2025, 3.0.247.0, UMDF filter on the Monitor class)
matches these panel PnP IDs — the 3-byte code is the EDID manufacturer code + product ID that
Windows reports as `Monitor\<MFR><PID>`:

| HW ID | Vendor | Variant | Notes (from INF comments/reg keys) |
|---|---|---|---|
| `Monitor\LGD077B` | LG Display | **LCD (SP11 Torrance)** | `; Torrance` in oempanelcustomizationlcd.inf; PreferredScaleFactor 200 |
| `Monitor\LGD0779` | LG Display | **LCD (`_494_DEI`)** | `_494_DEI LCD` comment; shares LGD077B install |
| `Monitor\LGD077A` | LG Display | LCD | scale 200 |
| `Monitor\LGD0780` | LG Display | LCD | scale 200 |
| `Monitor\LGD0719` | LG Display | LCD (legacy/other Surface) | scale 200 |
| `Monitor\LGD0555` | LG Display | legacy (explicit `PANEL_ID_LGD0555` enum in DLL) | scale 150? |
| `Monitor\SHP1587` | Sharp | LCD | scale 150 |
| `Monitor\SHP1572/1577/14B3/14B4` | Sharp | older/LCD pool | |
| **`Monitor\SDC4195`** | **Samsung Display** | **OLED (SP11 OLED SKU)** | `; OLED Panel` in oempanelcustomizationoled.inf |

For SP11 specifically: **LCD SKU = LGD077B "Torrance" / LGD0779 "_494_DEI"** (project codenames in
INF comments), **OLED SKU = SDC4195** (Samsung). The panel driver DLL also contains
`PANEL_ID_LGD0555`, `PANEL_ID_SHP1587`, `PANEL_ID_UNKNOWN` enums — i.e. a small compiled-in panel
ID table keyed off the Monitor HW ID.

No EDID override blob ships in the payload; identity is read from panel EDID at runtime (Monitor
PnP). For a Linux DT bring-up the panel node must therefore match on the DSI panel's EDID/ID read
via DCS (or be hardcoded per SKU) — `LGD077B`, `LGD0779`, `SDC4195` are the SP11-relevant IDs.

## 2. Brightness / backlight control method

`panel/SurfacePanelDriver.dll` (ARM64 PE, source paths `REPOSITORYDIR\SurfacePanelDriver\*.cpp`)
implements the Windows brightness pipeline with **three command destinations**:

- `BRIGHTNESS_DESTINATION_DEFAULT_TARGET`
- `BRIGHTNESS_DESTINATION_SSH` — **SAM via SSH (Surface Serial Hub)**, LCD path
- `BRIGHTNESS_DESTINATION_TCON` — **TCON register writes over I2C**, OLED path

### LCD (LGD077B/0779): backlight current via SAM/SSH, not local PWM

Evidence strings:

- `SET_LCD_BKLT_CURRENT returned I2C comm failure. (SET_BKLT_RESPONSE_FAILED_TO_WRITE_I2C)`
- `SET_LCD_BKLT_CURRENT returned current limit failure. (SET_BKLT_RESPONSE_FAILED_CURRENT_LIMIT)`
- `Backlight Current actually programmed by SAM: %u` / `Backlight luminance actually programmed by SAM: %u`
- `BacklightCurrentInMicroAmpere  = %d` / `BacklightCurrent value  %lld uA`
- `DMF_SamCommunicationViaSsh_SendSynchronously fails` (SAM comms module)
- `SET_LCD_BKLT_BRIGHTNESS returned %s`
- `BrightnessFlag_ConvertNitsToPwm`, `----Nits-PWM table----`, `PwmDutyCycleInMilliPercent`

So the LCD backlight is a **current-controlled LED driver programmed in microamps, written through
the Surface Aggregator Module (SAM) over the serial hub** (`SET_LCD_BKLT_CURRENT` SSH request with
status codes incl. explicit I2C-failure code — SAM does the I2C to the WLED driver internally).
There is NO SoC-side PWM backlight on SP11 LCD; brightness is nits-calibrated (calibration blobs
`DisplayCalibrationConfig.bin` / `DisplayCalibrationMeasurements.bin`, `SdcMath*` nits↔PWM↔current
conversions, `ImprovedLuminanceAlgorithm::GetPwmForNits`). Linux implication: expose backlight via
the Surface Aggregator (`surface_aggregator`/SAM display-blank or a `san`-style brightness
interface), or as `pwms`-less `backlight` with nits→current curve from calibration data.

### OLED (SDC4195): DCS-style DBV via TCON

- `OLED TCON Controlled Brightness (V1) DBV Set to: %u`
- `OLED TCON Controlled Brightness (V2) SDR DBV Set to: %u, and HDR DBV Set to: %u`
- `OLED TCON Controlled Brightness (V3) SDR DBV ... Transition time Set to: %u ms`
- `SDC_COMMAND_CONTROL_EDGEDIMMING`, `SDC_COMMAND_CONTROL_ACLPLC` (ACL/PLC = OLED peak-limit),
  `SDC_COMMAND_SET_BRIGHTNESS`, `SDC_COMMAND_SET_BACKLIGHT_CURRENT`,
  `SDC_COMMAND_SET_COLORSPACE_TRANSFORM`
- `Customer OS calibration is via TCON`

OLED brightness = **DBV (display brightness value) register in the TCON, I2C-programmed**, with
separate SDR/HDR DBV levels, transition time in ms, edge dimming and ACL/PLC controls. On Linux
this maps to a DCS `set_brightness`-style panel command path (MipiDBV) routed to the TCON or panel
DDIC.

### GPU/IHV color path
`GPUHandler` sends `IOCTL_COLORSPACE_TRANSFORM_SET` / `IOCTL_PANEL_SET_BRIGHTNESS` /
`IOCTL_PANEL_QUERY_BRIGHTNESS_CAPS|RANGES` to the QC display stack (dppsext, see §5).

## 3. TCON — separate timing controller over I2C

`displayhardware/SurfaceDisplayHardwareDriver.dll` (INF: `ACPI\MSHW0125`, `ACPI\MSHW0380`, UMDF)
is the **TCON driver**. It talks to the TCON over **I2C (SPB)** — `VIASPB` string, `DMF_CONTEXT_I2cTarget`,
`Created <vendor> I2C Transporter` for: **ANX2166, ANX2403, ANX2423, ANX2684 (Analogix), Anapass,
NVT855/NVT871/NVT875/NVT875V2 (Novatek), Default**. TCON detection is by `VendorId/DeviceId`
read over I2C (`GetDisplayHardwareTconCaps:: ... VendorId=%u, DeviceId=%u, NumberOfCscCoeff=%u,
NumberOfRegammaCoeff=%u, NumberOfDegammaCoeff=%u, TconFlags_UsesColumnPackedCsc`).

TCON responsibilities: CSC + degamma/regamma (3D LUT), **CABC / GD (global dimming) / DBC dynamic
config**, HDR pipeline, OLED DBV, `TCON BO level`, "Swire" register. Panel-size/project tuning:
`Setting 13" Incell/Outcell Initialize TCON HW Delay to %d ms`, `15" Outcell`, `DnT` — i.e. **the
TCON needs a post-init HW delay (ms) that differs between panel types (incell vs outcell touch)**.
Project IDs `_689_GAA` and `_831_WID` appear (SP11 = _831_WID era; "Cannot set I2C resources for
_831_WID", `Incorrect TDM ID - %d for Product _831_WID`).

**TCON firmware updates** are UEFI resource capsules (`UEFI\RES_{guid}` Class=Firmware), i.e.
flashed via UEFI capsule, not a runtime OS device:

- `Tconcellularlcd/SurfaceTCONCellularLCD_161.33.0.18.bin` — RES `{24763B7F-B810-4E3C-88A5-39C7C8722C9F}` (fw ver 0xa1210012)
- `Tconwifionlylcd/SurfaceTCONWifiOnlyLCD_161.33.0.19.bin` — RES `{82E97D6D-62C8-4F05-AE21-459F1F17AF15}` (0xa1210013)
- `Tconoled/SurfaceTCONOLED_176.1.0.22.bin` — RES `{0117BEB9-8E18-4D8E-8CED-5DF120FAAE6F}` (0xb0010016)

Bins are signed LFF firmware ("Microsoft Surface Monzino Firmware Signer", Surface LFF FW CA 2021),
all with magic header `edd5cb6d2de8444cbda17194199ad92a` + payload size at +0x14 (0x7e28 LCD = 32K,
0xca28 OLED = 51K). SKU split is by connectivity: **Cellular-LCD vs WifiOnly-LCD vs OLED** get
different TCON firmware resources — matching the `smf5glcd/smf5goled/smfwifilcd/smfwifioled`
SMF payload dirs also present in SurfaceUpdate.

**Linux implication:** the TCON is an I2C device (7-bit addr discoverable in the DSDT/`MSHW0125`
resources) that must NOT be driven by an i2c driver if unneeded for basic bring-up; but panel
brightness (OLED), CABC and color pipeline go through it. For minimal DPU bring-up the panel
enable path is DSI; TCON programming is an enhancement (color/HDR/CABC), except OLED DBV which is
required for brightness. No Linux kernel TCON firmware loader is needed (capsule updates only).

## 4. Panel power sequencing / platform bits

- Panel driver UMDF is a Monitor-class **lower filter** (`LowerFilters="WUDFRd"`), so it only sees
  monitor power callbacks — `Device in Passthrough mode, skipping MonitorPower Callback processing`
  exists for manufacturing mode. Actual DSI panel power rails (VSP/VSN/AVDD/reset-gpio) are managed
  by the QC DPU/DSI stack + ACPI, not by this payload; no rail/delay table was recoverable from
  these binaries. The only sequencing datum found: **TCON init HW delay** (§3, ms-tuned per panel
  type/size), applied after TCON power-on before first I2C ops.
- Calibration data source: **UEFI variables** (`GetFirmwareEnvironmentVariable`, "Display
  Configuration Calibration UEFI variable detected", "Display Measurement Calibration UEFI variable
  detected", MFCI vs legacy runtime variable, manufacturing-mode gating). On Linux these map to
  efivars (`/sys/firmware/efi/efivars/…`) if per-unit calibration is wanted — or use the shipped
  per-SKU blobs (`DisplayCalibrationConfig.bin`, `DisplayCalibrationMeasurements.bin` in
  oempanelcustomization{lcd,oled}).
- `TdmPowerHandler` / `TDMBoardID` / `BoardRevId` / `SdcComputeObject::GetLcdTdmPowerConsumption` /
  `GetOledTdmPowerConsumption`: TDM (touch display module) board ID feeds a power-budget model —
  relevant to thermal, not bring-up.

## 5. dppsext / hotplug

- **dppsext** (`surface_dppsext_8380.inf`): Qualcomm **DPPS (Display Pixel Processing / LTM)
  extension INF** for `ACPI\VEN_QCOM&DEV_0C36`, ships only `SurfaceLTMConfig.xml` — ambient-light
  (LTM) tuning tables: temporal filter steps, `BacklightReductionTable` (input 0x0–0xFF → output
  0x0–0xD9), `AmbientLightToContrastStrTable` (lux 0–0x7530 → contrast strength 0–0xC8),
  `BacklightToContrastStrTable`, `LuxStartPoint=0x190`, gamma/param knobs. Pure userspace color
  policy for the QC GPU driver — **not kernel-relevant** for Linux (equivalent would be a
  userspace ambient-light→brightness curve; IIO light sensor + backlight driver can consume the
  lux table values as defaults).
- **hotplug** (`SurfaceHotPlug.sys`, KMDF+DMF kernel driver, INF: `ACPI\MSHW0153` "Kip Device" +
  `{9086E8DA-...}\Target_SAM&Category_HostRegister` "Sam Device"): despite the name this is the
  **detachable-keyboard (KIP)/lid/form-factor + dGPU hotplug event driver** —
  `KIP_GET_KIP_LID_STATUS`, `SurfaceHotPlugDeriveFormFactorFromLidStatus` (laptop/canvas/detached),
  dGPU present/power GPIOs, SAM notifications via `Dmf_SamCommunicationViaSsh`. Not display
  hotplug. Linux equivalents already exist (surface Aggregator KIP/lid, Type-C DP HPD from DPU).

## 6. OLED vs LCD SKU summary (SP11)

| | LCD SKU | OLED SKU |
|---|---|---|
| Panel | LG Display LGD077B "Torrance" / LGD0779 "_494_DEI" | Samsung SDC4195 |
| Backlight | WLED current (µA) via SAM SSH `SET_LCD_BKLT_CURRENT`; nits-calibrated | TCON DBV over I2C (V1 single, V2/V3 SDR+HDR split, ms transition); ACL/PLC + edge dimming |
| Color pipeline | TCON CSC/regamma (CABC/GD/DBC), QC DPPS LTM | TCON CSC/regamma + OLED ALC/PLC, Dolby Vision (`PQConfig.dv`, `DolbyVisionExt` software component) |
| TCON fw resource | `{24763B7F-…}` cellular / `{82E97D6D-…}` wifi-only | `{0117BEB9-…}` |
| Customization pkg | `oempanelcustomizationlcd` (HDR off by default, `EnableBoostRefreshRateByDefault=1`) | `oempanelcustomizationoled` |
| Scale factor | 200 | 200 |

Notable registry defaults worth mirroring on Linux: LCD `EnableIntegratedPanelHdrByDefault=0`,
`EnableBoostRefreshRateByDefault=1` (dynamic refresh rate on by default on LCD), HDR-on for OLED
(`DisplayHdrLevel` GUID set).

## 7. Actionable gaps for the SP11 Linux tree

1. Panel drivers needed for `lgd,lgd077b` / `lgd,lgd0779` (LCD) and `samsung,sdc4195` (OLED) on
   X1E80100 DSI — no upstream panel driver matches; needs mode/lanes from a captured EDID/DSI dump
   (not present in this payload; `Monitor\LGD*`/`SDC4195` IDs confirm targets only).
2. LCD brightness: route via Surface Aggregator (SAM `SET_LCD_BKLT_CURRENT`, µA) — check
   `surface_aggregator` display/backlight request IDs against the strings above; nits→current
   curve from `DisplayCalibrationMeasurements.bin` (SdcBlob tables: BLOB_ID_CALIBRATION_SETTINGS /
   CONFIGURATION_DATA / MEASUREMENT_DATA parsers exist in the DLL as reference).
3. OLED brightness: DBV command path (TCON I2C or DCS); SDR/HDR split + transition ms semantics
   per V2/V3 strings.
4. TCON I2C client node (`MSHW0125`) — for CABC/GD toggle and color pipeline; init delay per panel
   type; capsule-only firmware (no kernel loader).
5. LTM: reuse lux→contrast tables from `SurfaceLTMConfig.xml` for userspace adaptive brightness.
