# REA camera evidence — SurfaceUpdate 26.041.12746.0 (2026-11-14)

Pipeline: `rea-agents@6.9.0 analyze … --provider ghidra` (Ghidra 12.1.4), JSON persisted at
`/tmp/rea-cam-{camfrontsensor,camrearsensor,camauxsensor,qccammipicsi}.json`; targeted
`rea decompile ram:0x…` + `objdump -d` string-xref recovery; custom Python parsers for the
AeoB `*_RES_*.bin` power-resource blobs and QTI Chromatix `com.surface.sensormodule.*.bin`
schemas.

**Architecture finding (applies to all three sensor .sys files).** Each
`surfacecam*sensor8380.sys` is the *generic* Qualcomm CamZ/CAMX sensor KMD shim
(`WP\src\Camera\CamZ\Core\SensorDriver\…`, PDB strings). It contains **no sensor register
tables**; init sequences live in the Chromatix `com.surface.sensormodule.*.bin` blobs, and
all power/rail/clock/GPIO sequencing lives in the AeoB `*_RES_*.bin` blobs consumed via
`\_SB.CAMF/CAMS/CAMI` ACPI devices. The decompiled `CameraSensorDriver_ProbeImageSensor`
(`ram:0x140009f18` in camfrontsensor) reads I2cMasterIndex/I2cSlaveAddr/I2cFreqMode/
RegAddrType/RegDataType/RegAddr/ExpectedId from ACPI-supplied config, reads the ID register,
compares to ExpectedId, and logs `Sensor probing succeed:0x%x` — sensor identity is data,
not code.

## 1. Sensor identities (INF ACPI HIDs + Chromatix SKUs)

| Driver dir | INF ACPI HID | Sensor | Notes |
|---|---|---|---|
| camfrontsensor | `ACPI\SONY0681` | **Sony IMX681** | FFC, MSHW0490 SKU |
| camfrontsensor | `ACPI\OVTI02C1` | OmniVision OV02C10 | FFC, MSHW0470/0540/0560 SKUs |
| camrearsensor | `ACPI\OVTID858` | **OmniVision OV13858** (confirmed, not guessed) | RFC, MSHW0491/0561 |
| camrearsensor (laser) | — | **ST VL53L5** dToF laser AF-assist | separate `LaserSensorDriver` (`stmvl53l5_*` ioctls) inside camrearsensor.sys |
| camauxsensor | `ACPI\SMO55F0` / `ACPI\SMO55F1` | **ST VD55G0 / VD55G1** global-shutter depth/IR (confirmed) | aux, MSHW0472/0492/0562 |
| qccammipicsi | `ACPI\QCOM0C98` | Qualcomm MIPI CSI receiver (CSID/CSIPHY combo) | see §6 |

SCFG SKU→module mapping (front): MSHW0470→ov02c10, MSHW0490→**imx681**, MSHW0540/0560→ov02c10.
Aux: MSHW0472/0492→vd55g0, MSHW0562→vd55g1. Rear: ov13858 everywhere.

## 2. Power sequences (AeoB `*_RES_*.bin`, DSTATE=0 power-on / DSTATE=3 power-off)

Format: ordered records. TLMMGPIO fields = `[pin, level, …]` — level toggles 0→1 within
power-on = reset/enable released *after* rails+mclk. PMICVREGVOTE = `[enable, uV, …]`.
DELAY units are AeoB ticks (small counts ≈1–10; treat as ~ms-scale, unit not decoded —
**observed values, unit derived**).

### Front — CAMF_RES_MSHW0490.bin (**imx681**)
```
DSTATE=0 (power-on, in order):
  NPARESOURCE /arc/client/rail_mmcx   vote 0x40
  CLOCK gcc_camera_xo_clk on; gcc_camera_ahb_clk on; cam_cc_gdsc_clk on
  FOOTSWITCH cam_cc_titan_top_gdsc on
  CLOCK cam_cc_cpas_ahb_clk 80 MHz
  TLMMGPIO pin=237 → 0            (assert reset, active-low)
  CLOCK cam_cc_mclk4_clk 19.2 MHz  ← MCLK BEFORE LDOs on imx681 SKU
  PMICVREGVOTE LDO3_M 1.80 V       (VDOVDD/IO)
  PMICVREGVOTE LDO7_B 2.80 V       (AVDD)
  DELAY 1
  TLMMGPIO pin=237 → 1            (de-assert reset)
  DELAY 10                         (post-reset settle)
DSTATE=3 (power-off, exact mirror):
  GPIO237→0, DELAY 1, mclk4 off, LDO7_B off, LDO3_M off,
  cpas_ahb off, titan_top_gdsc off, gdsc off, ahb off, xo off, rail_mmcx vote 0
```
Front MSHW0560/0470 (ov02c10) differ slightly: mclk4 = **12 MHz** on MSHW0560, or LDO1_M
1.2 V (DVDD) + LDO5_M 2.8 V on MSHW0470. **MSHW0490 (imx681) has no DVDD rail — 2 rails
only (1.8 V + 2.8 V).**

### Rear — CAMS_RES_MSHW0561.bin (**ov13858** + VL53L5)
```
DSTATE=0:
  rail_mmcx; xo/ahb/gdsc on; titan_top_gdsc on; cpas_ahb 80 MHz
  TLMMGPIO pin=110 → 0              (reset asserted)
  LDO6_M 1.80 V   (VDOVDD)
  LDO1_M 1.20 V   (DVDD)
  LDO5_M 2.80 V   (AVDD)
  DELAY 1
  LDO16_B 2.90 V  (separate 2.9 V — actuator/OIS or VL53L5 laser rail; late-enabled pin B)
  CLOCK cam_cc_mclk1_clk 19.2 MHz
  TLMMGPIO pin=110 → 1; DELAY 10
DSTATE=3: GPIO110→0, DELAY 1, mclk1 off, LDO16_B/LDO5/LDO1/LDO6 off, SoC clocks off (mirror)
```
Rear Linux DTS comparison: in-repo `ov13858_supply_names = {dovdd, avdd, dvdd}` matches
LDO6/LDO5/LDO1. **The late LDO16_B 2.9 V enable+delay between DVDD/AVDD and mclk has no
Linux counterpart** — candidate actuator/VL53L5 supply; on Linux the VL53L5 would need its
own client or shared rail.

### Aux — CAMI_RES_MSHW0562.bin (**vd55g1**; MSHW0472 vd55g0 same shape)
```
DSTATE=0:
  rail_mmcx; xo/ahb/gdsc/titan_top on; cpas_ahb 80 MHz
  TLMMGPIO pin=109 → 0; DELAY 1
  CLOCK cam_cc_mclk0_clk 19.2 MHz; DELAY 2
  LDO4_M 1.80 V  (vddio)
  LDO2_M 1.15 V  (vcore)
  DELAY 1
  LDO7_M 2.80 V  (vana)
  DELAY 1
  TLMMGPIO pin=109 → 1; DELAY 1
DSTATE=3: GPIO109→0, DELAY 5, LDO7 off, DELAY 1, LDO2/LDO4 off, mclk0 off, SoC clocks off
```
Linux `vd55g1_supply_name = {vcore, vddio, vana}` matches LDO2/LDO4/LDO7 with 1.15/1.8/2.8 V.
Note **vcore = 1.15 V** exactly (not 1.05/1.2). Power-off assert→rail delay (5) is longer
than power-on (1), and vana drops *first* on power-down (reverse of bring-up order: vana
last on, first off).

### GPIO summary (TLMM pin numbers, observed)

| Device | pin | role (derived from position in sequence) |
|---|---|---|
| \_SB.CAMF (front) | 237 | reset, active-low (assert 0 → release 1 after rails) |
| \_SB.CAMS (rear) | 110 | reset, active-low |
| \_SB.CAMI (aux) | 109 | reset/PWDN, active-low |

Linux denali DTS already uses `reset-gpios = <&tlmm 237 GPIO_ACTIVE_LOW>` for imx681 —
matches Windows exactly.

## 3. Clocks

| Sensor | MCLK | Linux DTS/expectation |
|---|---|---|
| imx681 (0490) | `cam_cc_mclk4_clk` **19.2 MHz** | DTS `assigned-clock-rates = <19200000>` ✔; `IMX681_XCLK_RATE 19200000` ✔ |
| ov02c10 (0560) | mclk4 **12 MHz** | (0560 blob value 12000000; 0490 uses 19.2) |
| ov13858 | `cam_cc_mclk1_clk` **19.2 MHz** | check DTS when added |
| vd55g0/g1 | `cam_cc_mclk0_clk` **19.2 MHz** | within vd55g1 6–27 MHz window ✔ |

CSI-side: `cam_cc_cpas_ahb_clk` at 80 MHz for all sensors during activity.

## 4. imx681 Windows vs Linux module gaps (sp11-selfcompile-drivers/imx681)

Linux module power-on: `regulator_bulk_enable(dovdd,avdd)` → `xclk prepare_enable` →
`reset_gpio=0` (assert) → `usleep_range(6200,7200)` → return; power_off mirrors.
Identify: `CCI_REG16(0x0016) == 0x0681`; link freq 1203 MHz (CPHY, half of observed
2406 Msym/s).

Differences vs Windows CAMF_RES_MSHW0490:
1. **Order: Windows asserts reset *before* any rail and before MCLK**; Linux asserts reset
   after rails+xclk. Net timing equivalent as long as reset is asserted before rails ramp —
   it is not (Linux release happens 6.2 ms after enable, with assert only at power_on
   start… actually Linux asserts reset *at the end* of power_on then waits 6.2 ms before
   release — see below).
2. **Linux `power_on` ends with reset asserted** (`gpiod_set_value(reset,0)` = asserted for
   ACTIVE_LOW) + 6.2 ms wait, i.e. the 6.2 ms is *t_reset* before the sensor is usable;
   Windows keeps DELAY 10 (≈10 units) between release and first I2C. Verify the Linux probe
   path releases reset (imx681_identify would NACK otherwise) — flag for review, since
   power_on returning with reset asserted is unusual vs the Windows order
   (release-then-settle-then-probe).
3. **No mmcx/SoC clock votes in the Linux module** (rail_mmcx, xo/ahb/gdsc/titan_top/cpas):
   these must come from camss/CCI DTS runtime PM; the selfcompile imx681 does not hold them
   itself — Windows sensor driver *does* vote rpmh arc client per sensor. If Linux camss
   doesn't vote titan_top when only the sensor probes, first identify may fail.
4. Rails match (1.8 + 2.8 V, no DVDD) ✔. GPIO 237 ✔. MCLK4 19.2 MHz ✔.
5. PLL config: not in the .sys; lives in `com.surface.sensormodule.ffc_imx681.bin`
   (Chromatix v3.4 container; `slaveSettings`/`registerData` arrays at schema offsets —
   values section not decoded this pass; **follow-up**: decode instance section for the
   full init register list and compare with `imx681-tables.h` mode registers).

## 5. I2C addresses

The .sys reads the address from ACPI (RegAddr/ExpectedId flow above); the Chromatix
sensormodule blobs contain `sensorSlaveAddress`/`slaveSettings` (schema confirmed; per-SKU
values not decoded this pass). Known-good: Linux DTS uses `camera@10` for imx681 (0x10,
consistent with Sony default). ov13858 mainline default 0x36; vd55g1 0x90/0x62 variants.
**Follow-up**: decode Chromatix instance values to confirm Windows ExpectedId + address per SKU.

## 6. qccammipicsi (ACPI\QCOM0C98) — CSI receiver

- Programs CSID/CSIPHY combo PHY: per-lane `CSIPhyRxLane` laneMask computation, per-data-rate
  `RecommendedSettleCount for CPHY DataRate = %d MSps`, per-lane settle counts ln0–ln6 + clk,
  `CSIPhyWaitforRx` polls Lane0/2/4/6 + Lane1/3/5 + Clk status, `CSIDPhyReset`,
  `CSIDPhyProgramPerFrequency`. Confirms **C-PHY (three-phase) and D-PHY (two-phase) both
  handled** — consistent with imx681 endpoint `MEDIA_BUS_TYPE_CSI2_CPHY`.
- PoFx device power management (`Camera_Register_PowerManager`, PoFxStartDevicePowerManagement).
- No sensor-specific data; bridge config (lane count, rates) comes from CAMX/Chromatix at runtime.
- **No UCSI/CIO2-style bridge table in any camera binary.** UCSI lives in qcusbcucsi (prior
  REA-4, blocked) — nothing new here; camera "bridge" config is pure CAMX runtime data.

## 7. camavs / camplatformext / qccamisp / qccamsecureisp (lower priority, brief)

- `camavs` = AVStream/MFT user-mode glue (`QcDeviceMFT8380.dll`, `eva.dll`) — no hardware
  sequencing.
- `camplatformext` = PCFG/PERF/PRLD/RES per-SKU perf blobs (`\_SB.CAMP`), SoC-level camera
  clock/perf plans.
- `qccamisp` ships `CAMERA_ICP.mbn`/`CAMERA_ICP_AAAAAA.elf` — the ICP/LX7 ISP firmware
  (`ICP_Lx7_HW_VERSION` string in qccammipicsi). ISP bring-up is firmware-side, not driver-side.
- `qccamsecureisp` not analyzed this pass.

## Persisted artifacts
- `/tmp/rea-cam-camfrontsensor.json`, `-camrearsensor.json`, `-camauxsensor.json`,
  `-qccammipicsi.json` (rea analyze JSON)
- `/tmp/rea-power-seq.txt` — full decoded AeoB DSTATE sequences (7 RES blobs)
- `/tmp/rea-cam-front.dis` — camfrontsensor objdump; probe function decompiled at
  `ram:0x140009f18`

## Action items for PR #73 + imx681 selfcompile
1. imx681: audit `imx681_power_on()` reset release ordering vs Windows (§4.2) — the module
   returns with reset asserted and a 6.2 ms wait *before* release appears missing; at minimum
   document that identify() happens after a separate release.
2. ov13858: consider modeling LDO16_B 2.9 V (actuator/VL53L5) — currently unmapped on Linux.
3. vd55g1: vcore=1.15 V exactly; power-off order (vana first, GPIO hold 5 units) vs Linux
   bulk_disable (unordered) — minor parity gap, PR #89 already covers ownership not ordering.
4. Decode Chromatix instance sections (`registerData` arrays) for the three sensors to
   extract Windows init register sequences and PLL configs for mode comparison against
   `imx681-tables.h` / `ov13858` modes.
