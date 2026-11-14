# VD55G0 (IR/front sensor) — Windows driver evidence report

**Source analyzed**
- Driver: `SurfaceUpdate/camfrontsensor/surfacecamfrontsensor8380.sys` (PE32+ ARM64, 0x22d88 bytes, sections `.text`@VA 0x140001000, `.rdata`@0x140016000, `.data`@0x14001a000)
- INF: `SurfaceUpdate/camfrontsensor/surfacecamfrontsensor8380.inf` (UTF-16LE, DriverVer 04/25/2025, 1.0.4258.7908, Provider %QC% = Qualcomm)
- Companion extension packages (where the real VD55G0 data lives):
  - `SurfaceUpdate/camauxsensor/surfacecamauxsensor8380.inf` (ACPI\SMO55F0, ACPI\SMO55F1)
  - `SurfaceUpdate/camauxsensorextension/com.surface.sensormodule.aux_vd55g0_MSHW0472.bin`, `..._MSHW0492.bin` (QTI Chromatix "Parameter Parser V3.4.0 (2106301010)" container, sensor name string **`vd55g0`** at file offset 0x1b014 in MSHW0472)
  - `SurfaceUpdate/camauxsensorextension/CAMI_RES_MSHW0472.bin` (AeoB ASL resource blob for `\_SB.CAMI`)
  - `SurfaceUpdate/camauxsensorextension/SCFG_AUX_MSHW0472.bin` (AeoB, selects `com.surface.sensormodule.aux_vd55g0_MSHW0472.bin` + `com.surface.tuned.aux_vd55g0_MSHW0472.bin`)

**Method note:** the requested REA MCP tools were not present in this session's tool catalog (verified by enumerating the full Code Mode tool list — only browser/chrome-devtools/opencode/t3-code namespaces exist). Analysis was done with `objdump` (LLVM, coff-arm64 disassembly), `strings`, and a custom Python PE/Chromatix parser (capstone installed for immediate-value scans). No `close_binary` tool exists to call.

---

## 1. Architecture finding: the .sys contains NO VD55G0 register tables

This is the single most important result and changes where the evidence lives.

The `.sys` is the generic **Qualcomm Spectra 695 CAMX sensor KMD shim** ("DriverFrameworks Camera Sensor Driver", Microsoft-signed Qualcomm code, PDB path fragment `Camera\CamZ\Core\...`). It:

- Builds I2C/CSL packets at runtime from *externally supplied* register blobs (strings: `CameraSensorDriver_SubmitSeqCmd() i2cCmd[%d] = (0x%x, 0x%x)`, `CSLPacketOpcodesSensorInitialConfig / SensorConfig / SensorStreamOn / SensorStreamOff`, `CameraSensorDriver_ProbeImageSensor() ... ExpectedId`, `RegAddr/RegAddrType/RegDataType`, `Poll Cmd [address, value]/[0x%04x, 0x%04x] with delay/0x%x`).
- Loads those blobs via `ReadAndEnumerateResourceConfigFromFile` / `RuntimeResourceEnumeration` (strings at `.rdata`, e.g. VA≈0x140016xxx region) and the registry `Device Parameters` key.

The per-sensor data is in the **extension package** directories:

| Windows package | ACPI ID | Sensor |
|---|---|---|
| `camfrontsensor` | `ACPI\OVTI02C1`, `ACPI\SONY0681` | color front (OV02C1 / IMX681) |
| `camrearsensor` | `ACPI\OVTID858` | rear OV13858 |
| `camauxsensor` | `ACPI\SMO55F0`, `ACPI\SMO55F1` | **aux IR = ST `vd55g0`** (and `vd55g1` on other SKUs) |

So for Linux PR #73, the VD55G0 relevant firmware data is `camauxsensorextension/*vd55g0*`, not the camfrontsensor .sys itself. **(observed)**

---

## 2. Power sequences (PMIC + GPIO + clocks)

From `CAMI_RES_MSHW0472.bin` (ASL "AeoB" resource script for `\_SB.CAMI`, the aux/IR sensor device). Entry format: 8-byte-aligned records of {tag, type, name, value-u64}.

Power-ON sequence (file offsets, in order):

| off | action | value |
|---|---|---|
| 0x1f8–0x21c | `NPARESOURCE /arc/client/rail_mmcx` | enable rail mmcx |
| 0x0d0–0x167 | `CLOCK gcc_camera_xo_clk`, `gcc_camera_ahb_clk`, `cam_cc_gdsc_clk` | enable (19.2 MHz XO — see §3) |
| 0x1a0–0x1ea | `FOOTSWITCH cam_cc_titan_top_gdsc`, `CLOCK cam_cc_cpas_ahb_clk` | 0x4c4b400 = **80,000,000 Hz** |
| 0x200–0x250 | `TLMMGPIO` | gpio 0x6d (109), then 1 ms-class DELAYs |
| 0x25c–0x2c8 | `PMICVREGVOTE PPP_RESOURCE_ID_LDO4_M` | 0x1b7740 µV = **1.80 V** |
| 0x2cc–0x330 | `PMICVREGVOTE PPP_RESOURCE_ID_LDO2_M` | 0x124f80 µV = **1.20 V** |
| 0x338+ | `DELAY`, then `PPP_RESOURCE_ID_LDO6_M` | **1.80 V** after delay |

Power-OFF (reverse order, offsets 0x3b0–0x7b8): `DSTATE`→3, TLMMGPIO low, DELAY 0x32 (50), LDO6 off, LDO2 off, LDO4 off, clocks/footswitch disabled, rail_mmcx off.

Interpretation: LDO4=AVDD 1.8 V, LDO2=DVDD 1.2 V, LDO6=VIOIO/MIPI 1.8 V (or IR-board supply), one reset GPIO (TLMM 109 on this SKU) toggled with ~1–5 ms delays between rails. **(observed)** — rail-to-pin mapping is **derived**.

---

## 3. Lane / link-frequency / clock config

- **MIPI lane count/link rate is NOT in the .sys** — it is passed in from the CAMX/CSL `CameraSensorDriver_CSIPhyConfig()` path (string: `pCSIPhyInfo->laneAssign / laneCount / laneMask` at `.rdata`). The .sys logs but does not hardcode them. **(observed)**
- No 840 / 420 MHz constants exist anywhere in the .sys (u32/u16/MOVZ-immediate scans all empty — only false positives inside UTF-16 strings, e.g. "Translation"). **(observed)** geocausa's 840 Mbps / 420 MHz / 19.2 MHz xclk therefore remains **hardware-observed**, corroborated indirectly by the resource blob enabling `gcc_camera_xo_clk` (the 19.2 MHz CXO) for the camera block. **(derived)**
- The Chromatix `vd55g0` blob schema contains `laneAssign`, `mipiFlags`, `sensorI2CFrequencyMode`, `i2cFrequencyMode` fields (names at schema offsets 0x1aab8, 0x19548, 0x1a850), but their per-sku values are stored in the (not fully decoded) Chromatix instance section; raw u32 dumps around the resolution data did not contain 840/420/1128 as clocks. **(observed-absent)**

---

## 4. hblank / line-length evidence (1128 vs 1200)

In `com.surface.sensormodule.aux_vd55g0_MSHW0472.bin` instance-data section (starts at file offset 0x1acec, payload 0x1acfc, flat u32 tag/value stream):

- At **file offset 0x1afe3**: the adjacent pair **`0x4b0, 0x6d6` = (1200, 1750)** sits in the resolution/frame-timing record (surrounded by 0x40s and float gains 2.5/2.25 = 0x40200000/0x40100000, exposureInfo floats 0x3ff40000 etc.).
- **1128 (0x468)** occurs 3× in the same file (offsets 0xf754, 0x1e9cb, 0x20bab) — inside registerData arrays (the 0xf754 hit is within the schema's `registerData` element definitions region), i.e. 1128 appears as a *register value/geometry constant* in the sensor init tables.
- 1200 (0x4b0) occurs 3× (0x10714 — inside a `registerData` schema entry, 0x1afe3 — frame-timing record, 0x1ed8b — registerData array).

This is consistent with geocausa's finding: Windows programs a **1200-px line length** (0x4b0 in the frame-timing record), while **1128 is the sensor's native/minimum active width** appearing in register lists. The 1200-vs-1128 pair in one resolution record is strong corroboration that the 1200-px minimum line length below 900 Mbps is a Windows-driver configuration choice, not a sensor hard limit. **(derived)**

Also present: `0x284, 0x25c` = **(644, 604)** at offsets 0x1ae7c–0x1ae88 — likely a crop/window (e.g. IR ROI). **(inferred)**

## 5. Exposure / gain register addresses

The Chromatix schema for vd55g0 defines the following standard CAMX sensor-driver address fields (names + tags, file offsets in MSHW0472 schema section). The actual numeric register addresses are carried in the same-named fields of the instance data and were **not fully decoded** (the Chromatix instance encoding interleaves variable-length registerData arrays; a proper Chromatix parser is needed):

- `middleCoarseIntgTimeAddr` (tag id 0x9 @0x280), `shortCoarseIntgTimeAddr` (0xa @0x2b8)
- `middleGlobalGainAddr` (0xb @0x2e8), `shortGlobalGainAddr` (0xc @0x328)
- `digitalGlobalGainAddr` (0xd @0x368), `middleDigitalGlobalGainAddr` (0xe @0x398, type 0x2fb), `shortDigitalGlobalGainAddr` (0xf @0x3d8)
- `digitalGainRedAddr` (0x10 @0x408), `digitalGainGreenRedAddr` (0x11 @0x448), `digitalGainBlueAddr` (0x12 @0x488), `digitalGainGreenBlueAddr` (0x13 @0x4c8)
- `strobeStartAddr` (0x14 @0x4f8), `strobeWidthAddr` (0x15 @0x530)

Confidence: field names/semantics **observed**; numeric addresses **undecoded** — do not cite numbers from this file without a Chromatix parser (upstream Linux `camx`-style `sensor_driver_t` is the same struct family).

## 6. Illumination / strobe

- The .sys has **no flash/LED/strobe/GPIO-toggle code path** for the IR illuminator (grep of all 251 `[...]` trace strings and full string table: zero hits for flash/torch/strobe/illumin/LED). Flash handling lives in the separate CAMX flash driver, not the sensor KMD. **(observed)**
- VD55G0 strobe is programmed **through sensor registers**: the Chromatix blob exposes `strobeStartAddr` / `strobeWidthAddr` (see §5) — i.e. the sensor asserts its strobe output for external IR LED drivers. This supports the PR #73 approach of binding a `flash-leds` device (PM8550 flash) for hardware strobe; the sensor-side trigger coordinates are register-based, as expected for ST VD* global-shutter sensors. **(derived)**
- `SCFG_AUX`/`CAMI_RES` show no flash-specific PMIC resource; only LDO2/4/6 + TLMMGPIO 0x6d — if that LDO6 or GPIO feeds the IR illuminator, the Windows stack drives it as a plain rail/GPIO, and strobe timing must come from the sensor's strobe output. **(inferred)**

## 7. Streaming start/stop

- .sys strings show stream control is via archived CSL packets: `CSLPacketOpcodesSensorStreamOn archived` / `SensorStreamOff archived`, applied asynchronously (`SensorAsyncProcessUpdate()`), with `CameraSensorDriver_DeviceTimeout()` re-applying InitialConfig+Config on timeout — matching the Linux symptom that a bad config STREAMs with no frames and a stalled stop. The actual register writes are inside the vd55g0 Chromatix `registerData` arrays (46 such arrays in MSHW0472), not in the .sys. **(observed)**

## 8. Register map

Not extractable at numeric granularity from these files without a Chromatix instance parser (the schema *is* the map of field names; numeric I2C addresses are in the undecoded arrays). See §5 for the field inventory. The Linux driver should keep using ST's public VD55G0 register map; the Windows blobs corroborate structure (init/config/streamon reglists per resolution, probe with `ExpectedId` chip-ID compare).

## 9. Limitations

1. REA MCP tools unavailable in this session; substituted objdump/strings/custom parsers. Decompilation-level control-flow analysis was not performed.
2. The Chromatix `.bin` instance-data encoding (tag/value stream with embedded variable-length registerData arrays) was only partially reverse-engineered — field names and several scalar values (1200/1750, 644/604, gain floats) were recovered; full per-register I2C sequences were not.
3. The `camfrontsensor` .sys/.inf targets the *color* front sensor (OV02C1/IMX681); the VD55G0 IR sensor is the *aux* device (SMO55F0/F1). The brief's premise that the front-sensor package contains VD55G0 data is incorrect on this firmware — use `camauxsensor*` packages for VD55G0.
4. No 840/420 MHz constants found anywhere; geocausa's numbers remain hardware-derived (consistent with, but not proven by, these files).
5. SKUs: MSHW0472/0492 = vd55g0; MSHW0562 uses vd55g1 (different sensor). Confirm target SKU (SP11 = MSHW0472/0492 class) before porting register tables.
