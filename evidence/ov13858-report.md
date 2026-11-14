# OV13858 rear camera driver analysis — Surface Pro 11 (Snapdragon X Elite / SM8750-8380)

Driver package: `SurfacePro11_ARM_Win11_26100_26.041.12746.0/SurfaceUpdate/`
Repo context: linux PR #73, branch `x/sp11-v25-native-camera-20261016`.
Analysis date: 2026-11-12.

> **Tooling deviation:** the `rea` MCP namespace was not registered in this
> environment (tool catalog enumerated; only browser/chrome-devtools/opencode/t3
> exist). Analysis used `strings`, `objdump` (coff-arm64 disassembly) and custom
> Python binary parsing of the Qualcomm *Chromatix* parameter blobs instead.
> No `close_binary` call was needed/possible.

## Executive summary (what matters for Linux)

The `.sys` binaries are **Qualcomm CamZ generic KMD driver code and contain no
OV13858 register tables**. All hardware-specific data — power sequences, I2C
slave addresses, register init/start/stop arrays, lane and link-frequency
configuration, exposure/gain register addresses — lives in *extension INF*
payload files, which are Qualcomm Chromatix/"AeoB" serialized blobs:

| File (camrearsensorextension/) | Role |
|---|---|
| `com.surface.sensormodule.rfc_ov13858.bin` (148,262 B) | Sensor driver data: slave addr, register settings arrays, resolution/streaming modes, gain/exposure addresses |
| `com.surface.tuned.rfc_ov13858.bin` (2.6 MB) / `..._MSHW0561.bin` (1.2 MB) | Tuning (AEC/awb etc.), module-specific |
| `CAMS_RES_MSHW0491.bin` / `CAMS_RES_MSHW0561.bin` (2,293 B) | **Power sequencing**: clocks, GDSC footswitch, PMIC LDO votes, GPIOs, delays |
| `SCFG_REAR_MSHW0491/0561.bin` | Links sensor module + tuning blob per hardware rev |

The Linux driver should mirror: (a) the CAMS_RES power-on/off sequence via
regulators/clocks, and (b) the Chromatix register arrays from the sensormodule
blob (streaming modes at offsets documented below).

## 1. Power sequences (observed)

From `CAMS_RES_MSHW0561.bin` (AeoB/SCNF serialized TLV; record order =
execution order). Verified identical structure in `CAMS_RES_MSHW0491.bin`.

**Power-on (file order, offsets are file offsets):**
```
0x00db CLOCK          gcc_camera_xo_clk          (enable, val=1)
0x010f CLOCK          gcc_camera_ahb_clk         (enable, val=1)
0x0144 CLOCK          cam_cc_gdsc_clk            (enable, val=1)
0x0176 FOOTSWITCH     cam_cc_titan_top_gdsc      (enable, val=1)
0x01b3 CLOCK          cam_cc_cpas_ahb_clk        (enable, val=1)
0x0201 TLMMGPIO                                  (GPIO block; num=0x6e=110, val=0)
0x025e PMICVREGVOTE   PPP_RESOURCE_ID_LDO6_M    (enable=1, voltage=0x1b7740 µV=1,797,000 µV? see note, mode=7 PMIC_AVAIL_VOTE)
0x02ce PMICVREGVOTE   PPP_RESOURCE_ID_LDO1_M    (enable=1, voltage=0x124f80 µV≈1,200,000 µV, mode=7)
0x033e PMICVREGVOTE   PPP_RESOURCE_ID_LDO5_M    (enable=1, voltage=0x2ab980 µV≈2,800,000 µV, mode=7)
0x03ae DELAY                                     (val=1 ms)
0x03cc PMICVREGVOTE   PPP_RESOURCE_ID_LDO16_B   (enable=1, voltage=0x2c4020 µV≈2,904,000 µV, mode=7)
0x043d CLOCK          cam_cc_mclk1_clk           (enable, rate field=0x1 24f8→ 19,200,000 Hz (0x124F800 = 19.2 MHz))
0x0488 TLMMGPIO                                  (num=0x6e, val=1)  ← sensor reset/enable GPIO released
0x04e5 DELAY
```
Voltage fields are little-endian µV: `40 77 1b` = 0x1B7740 = 1,797,000 µV
(LDO6 ≈ 1.8 V); `80 4f 12` = 0x124F80 = 1,200,000 µV (LDO1 = 1.20 V);
`80 b9 2a` = 0x2AB980 = 2,800,000 µV (LDO5 = 2.8 V AVDD); `20 40 2c` =
0x2C4020 = 2,904,000 µV (LDO16 ≈ 2.9 V, dovdd? observe 2.9 V odd — mark
**derived**, confirm on hardware). MCLK rate `00 f8 24 01` = 0x0124F800 =
19,200,000 Hz = **19.2 MHz**.

**Power-off (reverse order, file offsets):**
```
0x0599 CLOCK          cam_cc_mclk1_clk   (disable, val=0/3)
0x05e4 PMICVREGVOTE   LDO16_B (disable)
0x0655 PMICVREGVOTE   LDO5_M  (disable)
0x06c5 PMICVREGVOTE   LDO1_M  (disable)
0x0735 PMICVREGVOTE   LDO6_M  (disable)
0x07a5 CLOCK          cam_cc_cpas_ahb_clk (disable)
0x07db FOOTSWITCH     cam_cc_titan_top_gdsc (disable)
0x0818 CLOCK          cam_cc_gdsc_clk (disable)
0x084a CLOCK          gcc_camera_ahb_clk (disable)
0x087f CLOCK          gcc_camera_xo_clk (disable)
```

The KMD driver logs corroborate the mechanism: `CameraKMDPowerControl.c`,
`[POWER]` section string at VA `0x140013ae0` (xref `add x3, x8, #0x6f8` at
`0x1400053ec`); "Handling ioctl for power state" at VA `0x1400117c0`.

Linux mapping: LDO5→AVDD 2.8 V, LDO1→DVDD 1.2 V, LDO6→DOVDD 1.8 V (assignment
of names to rails is **inferred** from typical OV13858 + voltage values),
mclk1→19.2 MHz clock, TLMM GPIO 110 → reset (assert low, release after
regulators). Confidence: sequence **observed**, rail-name mapping **inferred**.

## 2. Lane / link config

- The `.sys` (strings, `CameraSensorDriver_CSIPhyConfig()`) validates
  `CSIPHY3Phase` (C-PHY flag), `comboMode`, `cphyDphyComboMode`, `dataRate`,
  `laneCount`, `laneAssign`, `laneMask`, `secureMode`, `settleTimeNS` — all
  taken from packets **inside the Chromatix blob**, not hardcoded.
  `CameraSensorDriver_ProcessMIPICSIPhyPacket()` rejects "Invalid laneCount /
  Datarate / laneMask" (strings at VA 0x140017xxx region). Confidence:
  **observed** (code paths), values **derived** from blob below.
- In `com.surface.sensormodule.rfc_ov13858.bin`, near the module config at
  `0x1d740` ("ov13858"): field block `36 6e 01 0a 00 00 00 ...` =
  slaveAddress **0x6e** (0x37 7-bit), I2C freq mode 10 (fast plus 1 MHz) —
  matching the standard OV13858 7-bit address 0x36/0x37 in 8-bit notation.
  Field `1e 00 00 00` after `01 00 00 00 08 00` = **laneCount-related value
  (cphyDphyComboMode/lane fields)**; the module declares `laneAssign`,
  `isComboMode`, `cphyDphyComboMode` keys (strings at blob offset ~0x1bdxx).
  Confidence: slave addr **observed**; lane count/value decode **derived**.
- Link frequency: not found as a literal in the blob scan (dataRate is a u64
  computed from `resolutionInfo` per mode; the u32 scan found resolution
  dimensions 2112×1560, 1344×768, 1920×1080 groups). The main streaming mode
  block at `0x1d740` shows `62 04 00 00 8e 0c 00 00` = width 1122? — decoded as
  **4224?** no: 0x0462=1122, 0x0c8e=3208 — actually these read as
  `0x462,0xc8e` = 1122×3208 (binned Raw10 with embedded lines?). Marked
  **inferred**; exact per-mode dimensions require full Chromatix parser.
  Repeated per-mode triples (`54 44 1c 01`, `00 3e 40 00` = 0x403e00 =
  4,211,200 → likely dataRate/lane in kbps×…?) appear 3× = 3 streaming modes.
  Confidence: **derived**, needs verification against Chematix spec.

## 3. Register map (offset → purpose, confidence)

From `com.surface.sensormodule.rfc_ov13858.bin` structure (Chromatix
Parameter Parser V3.4.0, 2106301010; header at 0x0, sub-blobs:
`sensorDriverData` @0xd0, `cameraModuleData`, `actuatorDriver` (dw9800),
`EEPROMDriverData` (st_m24c64)):

| Chromatix key (blob offset) | Purpose | Confidence |
|---|---|---|
| `sensorSlaveAddress` (0x1bdf8) | 0x6e 8-bit / 0x37 7-bit | observed |
| `resolutionInfo/resSettings` arrays | per-mode register sequences (`slaveAddr, registerData, delayUs` triples; ~100 arrays) | observed |
| `initSettings`/`initSetting` | power-on init register table | observed (key; data in blob) |
| `streamConfiguration` + `transitionGroups` | stream on/off + mode-switch sequences | observed (key) |
| `middleCoarseIntgTimeAddr`, `shortCoarseIntgTimeAddr` | HDR exposure regs | observed keys |
| `globalGainAddr`, `middle/shortGlobalGainAddr`, `digitalGlobalGainAddr`, `digitalGainRed/GreenRed/Blue/GreenBlueAddr` | analog + digital gain registers | observed keys |
| `strobeStartAddr`, `strobeWidthAddr` | flash strobe regs | observed |
| `isClockDisableNeeded`, `downScaleFactor`, `skipPattern`, `maxDLPSPerSlot`... | mode control | observed keys |

Binary scans of the blob show heavy occurrence of standard OmniVision
register addresses encoded as u16: `0x3500/0x3501/0x3502` (exposure/coarse
integration time), `0x350a` (analog gain), `0x3600` array (analog control),
`0x4800` (frame timing), `0x0100` (streaming on/off, 2049 u16 hits). These
match the public OV13858 datasheet layout. Confidence: **derived** (address
occurrence in blob) — exact per-address *values* require a Chromatix V3.4
deserializer to dump the arrays cleanly.

Known-good Linux reference: drivers/media/i2c/ov13858.c (mainline) uses
0x3500-0x3502 exposure, 0x350a/0x350b gain, 0x0100[1]=stream on — consistent.

## 4. Aux / VS drivers

**camauxsensor (`surfacecamauxsensor8380.sys`, 139 KB)** — same CamZ generic
KMD code specialized for the auxiliary (wide/ToF-assist) sensor. Strings show
dual-probe logic: "AUX camera DC probing failed, starts probing Azurewave
OV9234" and "Probe Main camera failed so going for DC probing", loading
`com.qti.sensormodule.azurewave_ov9234.bin` + tuning. INF matches ACPI
`SMO55F0`/`SMO55F1` ("Washington1" project string). So aux sensor = **OV9234
global-shutter** (Azurewave module). Confidence: observed.

**camavs (`surfacecamavs8380.sys`, 547 KB + QcDeviceMFT8380.dll 24 MB,
eva.dll, no_camera.yuv)** — AVStream user-mode capture stack: KS/AVStream
capture filter, topology nodes (LrmeNode = low-res multi-exposure, BPS),
EIS2/EIS3/EIS3_4K_VIDEO electronic stabilization modes, EVA (Enhanced Video
Analytics) MCTF, registry-driven configs. INF matches `VIDEO\QCOM_AVStream_8380`.
It is the Windows media-layer; **no sensor register data**. Confidence:
observed.

The rear sensor INF also shows the Laser/ToF side car: `LaserSensor` driver
embedded in `surfacecamrearsensor8380.sys` — **ST VL53L5 ToF** (strings:
`stmvl53l5.bin` firmware, `ST_TOF_IOCTL_ROM_BOOT`, `ST_TOF_IOCTL_RW`,
`stmvl53l5_powerstate`, "Register LRF Interrupt for GPIO 12"). I2C addr/regs
for it come from ACPI `_DSM`/resource config ("ToF I2cSlaveAddr = %#04x"
printed at probe). Confidence: observed.

## 5. Limitations

- `rea` MCP unavailable; no decompiler-grade pseudocode. ARM64 objdump was
  used for the .sys, but the .sys carries **no sensor registers**, so
  disassembly depth was intentionally limited (context budget).
- Chromatix blob (`com.surface.sensormodule.rfc_ov13858.bin`) is a
  proprietary serialized format (Parameter Parser V3.4.0). Keys and layout
  were recovered by string + pattern analysis; **per-mode register values
  (init tables, stream on/off, per-mode lane/dataRate) are present but not
  fully decoded**. A dedicated parser (or loading the blob with Qualcomm's
  chromatix tools) would extract exact `regSetting` arrays.
- PMIC rail → sensor pin mapping (AVDD/DVDD/DOVDD names) is inferred from
  voltages; confirm against schematic/telemetry.
- Link frequency values per mode remain derived/inferred pending full blob
  decode; mainline ov13858.c modes (2112×1560@…, link freq 633 MHz×… ) are a
  reasonable starting point.
- LDO16 2.904 V is unusual for DOVDD; possible backlight/other rail sharing —
  verify.

## Appendix: reproducible extraction pointers

- Sections of `surfacecamrearsensor8380.sys`: .text VA 0x140001000 size
  0x1971c; strings table embedded in .text tail (VAs cited inline above).
- `SCFG_REAR_MSHW0561.bin`: AeoB header `{u32 magic 'AeoB', u32 size,
  u32 ver, u16 count, ...}` then TLV records
  `(u16 len, u16 type, u16 ?, u16 namelen, name[], u64 fields...)`.
- Chromatix blob header: magic `QTI Chromatix Header`, parser version string,
  module name `com.surface.sensormodule.rfc_ov13858`, then sub-blob index
  (`sensorDriverData`, `cameraModuleData`, `actuatorDriver`,
  `EEPROMDriverData`) with (id, offset, size) tuples at 0x90-0xd0.
