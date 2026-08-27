# SP11 camera source and static-resource audit

Date: 2026-08-27. Scope: public source review and offline reads of installed Windows files. This audit did not activate a camera, evaluate an ACPI method, issue a hardware IOCTL, read MMIO, or change a driver.

## Evidence boundaries

| Evidence | What it establishes | What it does not establish |
| --- | --- | --- |
| Karsies source and issue comment | Another SP11 owner's reported front-camera capture and the implementation they published | Successful capture on this device, rear/IR support, or full Windows parity |
| Installed INF and resource files | Which MSHW049x package selects each binary, and its ordered static resource declarations | Actual timing, native GPIO field meanings, electrical levels, or successful stream transitions |
| Windows advertised profile metadata | Modes exposed by the Windows API | Observed frame rate, raw sensor modes, Bayer order, or successful initialization of every profile |
| Future live capture / Linux validation | Only the operations and modes actually exercised, with their recorded outcomes | Untested sensors, modes, controls, suspend behavior, or illumination safety |

In particular, an advertised IR 644x604@60 NV12 profile is not a captured IR frame and does not independently establish a 644x604 RAW10 sensor mode. Windows ISP output dimensions must not be substituted for raw sensor geometry.

## Public source identity and issue status

Reference repository: [karsies-wq/sp11-imx681-linux at b08f76f40b8d7b715bd4da6aef484f86142cc147](https://github.com/karsies-wq/sp11-imx681-linux/tree/b08f76f40b8d7b715bd4da6aef484f86142cc147), fetched into the ignored `reference/` directory. HEAD is dated 2026-08-15; kernel code entered in initial import `ae8ce1d84841a526644991ceb9b0e98c0bab36d3`. Subsequent commits update README/userspace, not a reviewed series against a kernel tree. Its stated kernel base is `jg/ubuntu-qcom-x1e-7.2-rc5-jg-0`.

Issue #74's title/body describe an earlier blocker. The [author's August 15 resolution comment](https://github.com/jglathe/linux_ms_dev_kit/issues/74#issuecomment-5302651457) reports a functioning front webcam, 3840x2640 RAW10 at about 16 fps, through libcamera/PipeWire and a browser. That report covers the front camera, not all sensors. The three target issues remain open: [rear #41](https://github.com/ooaklee/linux-surface-pro-11-oe/issues/41), [IR #42](https://github.com/ooaklee/linux-surface-pro-11-oe/issues/42), and [front #43](https://github.com/ooaklee/linux-surface-pro-11-oe/issues/43). The [August 27 comment on #43](https://github.com/ooaklee/linux-surface-pro-11-oe/issues/43#issuecomment-5433400173) requests this integration. Older issue statements about available drivers or C-PHY support are not substitutes for inspecting the selected kernel revision.

The [pinned README](https://github.com/karsies-wq/sp11-imx681-linux/blob/b08f76f40b8d7b715bd4da6aef484f86142cc147/README.md) reports CSIPHY2/trio 0, C-PHY, a usable 1.0 Gsps AFE row with CDR around 0x46-0x4e, and the LINE_LENGTH_PCK change from 7552 to 8704. These are reference bring-up parameters, not values measured on this device. Older dossier entries and `summary-en.md` intentionally preserve superseded experiments.

## Migration decision

| Candidate | Source and action | Gate before claiming functionality |
| --- | --- | --- |
| CSID-680 RDI drop/crop initialization | Narrow the change from [camss-csid-680.c, around lines 332-372](https://github.com/karsies-wq/sp11-imx681-linux/blob/b08f76f40b8d7b715bd4da6aef484f86142cc147/camss/camss-csid-680.c#L332) to removing the four unconditional horizontal/vertical drop/crop enables from CFG1. Omit additional frame/pixel/line pattern or period writes; keep sensor-specific crop experiments out. | Compile; compare RDI input/drop/buffer-done counters during an actual stream. The target reconstructs CFG1 each configuration, explicitly disabling these unconfigured features; the reference's simultaneous pattern changes do not independently establish their necessity. |
| VFE-680 RDI MIPI_RAW mode | Port only the write-master mode bit from [camss-vfe-680.c, lines 153-216](https://github.com/karsies-wq/sp11-imx681-linux/blob/b08f76f40b8d7b715bd4da6aef484f86142cc147/camss/camss-vfe-680.c#L153): define a named MIPI_RAW `BIT(16)` and combine it with write-master enable, following the target's VFE-480 precedent. Retain stride, image configuration, packer and frame increment unchanged. | Compile; check buffer sizes, bytes-per-line, frame boundaries, IMAGE_VIOLATION, and widths with/without alignment padding. VFE-480 precedent does not prove VFE-680 padding behavior. |
| C-PHY receiver implementation | Use the [PHY table and sequence](https://github.com/karsies-wq/sp11-imx681-linux/blob/b08f76f40b8d7b715bd4da6aef484f86142cc147/phy/phy-qcom-mipi-csi2-3ph-dphy.c#L373) as bring-up evidence. Rework into per-instance configuration selected from the endpoint/PHY API, with explicit supported-SoC checks. | Verify rate units and lane/trio mapping; preserve D-PHY for rear/IR; validate mixed-camera operation. |
| IMX681 support | Preserve provenance of [the sensor tables](https://github.com/karsies-wq/sp11-imx681-linux/blob/b08f76f40b8d7b715bd4da6aef484f86142cc147/ccs/ccs-imx681-init.h), including LLP 8704 at 0x0342/0x0343. Implement sensor-scoped behavior and truthful control/format negotiation. | Verify model/manufacturer/revision, power resources, geometry, exposure/gain readback, errors, and repeatable captures. |
| Userspace calibration | Review the [sensor tuning YAML](https://github.com/karsies-wq/sp11-imx681-linux/blob/b08f76f40b8d7b715bd4da6aef484f86142cc147/userspace/libcamera/smiapp.yaml) separately from kernel changes, using a sensor-specific name. | Color/exposure tests, control behavior, real debayer backend, CPU/power cost, and application compatibility. |

The two receiver fixes are the first bounded porting candidates. Neither supplies camera device-tree wiring, sensor support, or a parity result by itself. Review of actual remote base `3fc7c5249f4a` confirmed the unconditional CSID drop/crop enables. In that base, `csid_ops_680` and `vfe_ops_680` are selected by X1E80100 resources for both full and lite paths. These are generic 680 RDI changes, not IMX681-only quirks; the reference's front-camera result does not validate every full/lite route or other sensors' D-PHY operation.

**RAW10 alignment remains a deployment gate.** The base's `camss-vfe.c` selects 16-byte RDI alignment for X1E80100, and `camss-video.c` rounds the calculated bytes-per-line accordingly. At width 3844, packed RAW10 carries 4805 bytes per line but the advertised stride is 4816; width 3840 carries exactly 4800 bytes and needs no padding. MIPI_RAW mode may not insert that padding, so actual buffer layout, line boundaries and receiver errors must be checked before accepting an unaligned mode. Keeping frame increment unchanged preserves the existing buffer-spacing calculation but does not establish correct line layout. Do not introduce a global 3844-to-3840 crop or change the advertised stride without separately verified format/selection handling. Test repeated starts/stops and the other full/lite, sensor and PHY paths before deployment or parity claims; no Linux runtime validation was performed in this source audit.

### Do not import these bring-up shortcuts

- [CCS control overrides around lines 800 and 861](https://github.com/karsies-wq/sp11-imx681-linux/blob/b08f76f40b8d7b715bd4da6aef484f86142cc147/ccs/ccs-core.c#L800) change generic analogue-gain handling into digital gain writes at 0x020e/0x020f and exposure into 24-bit writes at 0x0229-0x022b. There is no model guard in those cases, and several I2C return values are ignored. The source-format overrides around lines 2403/3346 similarly apply beyond IMX681. Use a sensor quirk or a dedicated driver, not these global changes.
- [CAMSS source-format handling](https://github.com/karsies-wq/sp11-imx681-linux/blob/b08f76f40b8d7b715bd4da6aef484f86142cc147/camss/camss-csid.c#L863) changes any 3844-wide stream to 3840. A format width alone must not identify a sensor or hardware capability.
- Global `force_cphy`, trio/lane remap, AFE/CDR module parameters affect other routes. The [PHY clock override](https://github.com/karsies-wq/sp11-imx681-linux/blob/b08f76f40b8d7b715bd4da6aef484f86142cc147/phy/phy-qcom-mipi-csi2-core.c#L50) selects maximum clocks by forcing an unavailable-rate fallback. Neither mechanism is an appropriate all-camera default.
- [CCS static data](https://github.com/karsies-wq/sp11-imx681-linux/blob/b08f76f40b8d7b715bd4da6aef484f86142cc147/sensor-data/ccs-imx681.yaml) mixes repairs for reported invalid limits with deliberately widened pixel-clock limits, because later code overwrites the computed PLL. Separate measured corrections from calculator workarounds.
- The [LED helper](https://github.com/karsies-wq/sp11-imx681-linux/blob/b08f76f40b8d7b715bd4da6aef484f86142cc147/userspace/systemd/sp11-camera-led.sh) polls a hard-coded ISP runtime-status path once per second and invokes `gpioset`. It cannot guarantee illumination from the first exposure, failure cleanup, or separation of multiple sensors. Do not treat it as a kernel privacy guarantee.
- The [PipeWire drop-in](https://github.com/karsies-wq/sp11-imx681-linux/blob/b08f76f40b8d7b715bd4da6aef484f86142cc147/userspace/pipewire/sp11-camera.conf) disables MemoryDenyWriteExecute to accommodate llvmpipe JIT. This also contradicts interpreting the README's GPU-debayer wording as proof of Adreno acceleration. Do not apply a global hardening relaxation automatically.
- The [libcamera patch](https://github.com/karsies-wq/sp11-imx681-linux/blob/b08f76f40b8d7b715bd4da6aef484f86142cc147/userspace/libcamera/0001-soft-ipa-agc-target-and-tunable-adjust.patch) hard-codes an AGC target of 3.5 while its comment describes 3.1. Make tuning sensor-specific and reconcile documentation with the actual code.
- The [textual DTS](https://github.com/karsies-wq/sp11-imx681-linux/blob/b08f76f40b8d7b715bd4da6aef484f86142cc147/dtb/sp11-denali-cam.dts#L4139) is a decompiled, hard-coded-phandle intermediate with CSIPHY4/D-PHY settings and an experimental `sp11,cam-probe` helper. It is not the final front-camera source for the reported CSIPHY2 result. Do not replace a board DTS/DTB or patch a UKI from it wholesale.

## Locally verified static resource decoding

New tool: `target/Decode-SP11CameraResources.ps1`. Tests: `target/tests/Test-SP11CameraResources.ps1`.

The SCFG and CAMx_RES files have the [ACPI output-buffer layout](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/acpiioct/ns-acpiioct-_acpi_eval_output_buffer_v1), rather than AML. A 12-byte header precedes variable-length [V1 method arguments](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/acpiioct/ns-acpiioct-_acpi_method_argument_v1). The parser respects the [argument-size rule](https://learn.microsoft.com/en-us/windows-hardware/drivers/acpi/acpi-method-argument-length), with no invented alignment. It checks exact file/declared lengths, root counts, recursive extents, depth/node limits, 4/8-byte integer widths, ASCII termination, and zero padding; unknown types fail closed. JSON integers are decimal strings with width-preserving hex, avoiding 64-bit precision loss.

Private run: `output/camera-static-20260827-121608-270-67c835de/`. The manifest, per-file JSON trees, and sanitized summary were written there; no original binary was copied. All six files parsed completely. Both PowerShell 7.6.4 and Windows PowerShell 5.1 passed 49 synthetic checks, including malformed-input rejection and no output on a failed multi-file batch.

| Installed file | Bytes | Decoded nodes | SHA-256 of bytes actually parsed |
| --- | ---: | ---: | --- |
| CAMF_RES_MSHW0490.bin | 1843 | 169 | 379A03154511922428EA27F56DE625F579F39CA483FB97398953278B6B5F2851 |
| CAMS_RES_MSHW0491.bin | 2293 | 205 | 2D356BBFAF07CED1E5C03014A5C496B12107F5DC489C4333052565D5A5A5DCC2 |
| CAMI_RES_MSHW0492.bin | 2187 | 203 | FB058D3C966F4B48412D48C2DE7F58B861103A8AA06F0BB8C1D198E12431D908 |
| SCFG_FRONT_MSHW0490.bin | 133 | 6 | E6E3D828A1E4F5BC94C545848A091C20BE399A4B22C938ED4A3DF072DD033D99 |
| SCFG_REAR_MSHW0491.bin | 135 | 6 | FA1D4B79AC3305ED822AAE7FB2D1676D28EB130CB651B2B7F84D208AAB64652A |
| SCFG_AUX_MSHW0492.bin | 151 | 6 | 1C848AC4DDBE12948CAE4AFFEE421E3A457CAC61F98DACB30043841BF0BD1BFF |

The installed extension INFs map `ACPI\VEN_SONY&DEV_0681&SUBSYS_MSHW0490`, `ACPI\VEN_OVTI&DEV_D858&SUBSYS_MSHW0491`, and `ACPI\VEN_SMO&DEV_55F0&SUBSYS_MSHW0492` to those respective files. Packages also contain other product variants; selecting the first matching filename would be unsafe.

### Ordered DSTATE 0 declarations

All records below occur under `arguments[0].elements[4]`. The table gives the child index and argument-header byte offset; complete child paths, types, data lengths, payload offsets, and values are retained in JSON. Resource interpretation remains separate: for example, a `TLMMGPIO` tuple is native vendor data, not a Linux GPIO flags cell or a measured pin level. `DELAY` units and all unlabelled control fields need independent confirmation.

Notation: `VREG(name, value)` abbreviates the full raw `PMICVREGVOTE` tuple `[name, 1, value, 1, 7, 0]`; `MCLK(name)` abbreviates raw `CLOCK` `[name, 8, 19200000, 3]`. Numbers resembling microvolts/Hz are retained as declared numbers, not measurements.

| Camera | Ordered records after the shared camera-domain prologue |
| --- | --- |
| Front CAMF | `[8] @505 TLMMGPIO [237,0,0,1,0,0]`; `[9] @598 MCLK(cam_cc_mclk4_clk)`; `[10] @673 VREG(PPP_RESOURCE_ID_LDO3_M,1800000)`; `[11] @785 VREG(PPP_RESOURCE_ID_LDO7_B,2800000)`; `[12] @897 DELAY [1]`; `[13] @927 TLMMGPIO [237,1,0,1,0,0]`; `[14] @1020 DELAY [10]` |
| Rear CAMS | `[8] @505 TLMMGPIO [110,0,0,1,0,0]`; `[9] @598 VREG(PPP_RESOURCE_ID_LDO6_M,1800000)`; `[10] @710 VREG(PPP_RESOURCE_ID_LDO1_M,1200000)`; `[11] @822 VREG(PPP_RESOURCE_ID_LDO5_M,2800000)`; `[12] @934 DELAY [1]`; `[13] @964 VREG(PPP_RESOURCE_ID_LDO16_B,2900000)`; `[14] @1077 MCLK(cam_cc_mclk1_clk)`; `[15] @1152 TLMMGPIO [110,1,0,1,0,0]`; `[16] @1245 DELAY [10]` |
| IR CAMI | `[8] @505 TLMMGPIO [109,0,0,1,0,0]`; `[9] @598 DELAY [1]`; `[10] @628 MCLK(cam_cc_mclk0_clk)`; `[11] @703 DELAY [2]`; `[12] @733 VREG(PPP_RESOURCE_ID_LDO4_M,1800000)`; `[13] @845 VREG(PPP_RESOURCE_ID_LDO2_M,1150000)`; `[14] @957 DELAY [1]`; `[15] @987 VREG(PPP_RESOURCE_ID_LDO7_M,2800000)`; `[16] @1099 DELAY [1]`; `[17] @1129 TLMMGPIO [109,1,0,1,0,0]`; `[18] @1222 DELAY [1]` |

Shared prologue: `/arc/client/rail_mmcx` vote, camera XO/AHB/GDSC clocks, `cam_cc_titan_top_gdsc`, and a `cam_cc_cpas_ahb_clk` tuple containing 80000000. Separate DSTATE 3 packages are decoded too. They must be reviewed directly; shutdown order must not be assumed to be a simple reversal.

The public front reset/MCLK/rail observations broadly match these declarations. However, Linux regulator requests must be checked against supported voltage steps. Karsies' [working notes](https://github.com/karsies-wq/sp11-imx681-linux/blob/b08f76f40b8d7b715bd4da6aef484f86142cc147/docs/werknotities-nl.md#L360) document an invalid PM8550 request breaking the whole regulator bank, and using 2,904,000 rather than 2,900,000 and 1,152,000 rather than 1,150,000 microvolts. Do not copy Windows nominal values into fixed Linux constraints without checking the target driver's ranges.

### SCFG and sensor binary boundaries

SCFG packages name the sensormodule and tuning files. Remaining integers are deliberately uninterpreted:

| SCFG | Raw final three integers | Header offsets of those arguments |
| --- | --- | --- |
| Front | 0x150020, 0x0AFF0004, 0 | 97, 109, 121 |
| Rear | 0x150020, 0xD855300B, 0 | 99, 111, 123 |
| IR | 0x1500C0, 0x30470000, 0 | 115, 127, 139 |

Do not decode these integers as a CCI address, sensor ID, or routing configuration by resemblance alone.

The three installed sensormodules begin with `QTI Chromatix Header`, identify Parameter Parser V3.4.0 (2106301010), and contain named data sections. Their verified names/hashes are:

| Sensormodule | Bytes | SHA-256 |
| --- | ---: | --- |
| com.surface.sensormodule.ffc_imx681.bin | 212272 | F7DD81BE64153FD3F0DA8E6288EE1B9906B7BF51B773A98496934D76DC96A45C |
| com.surface.sensormodule.rfc_ov13858.bin | 148262 | F8F60E79B77BD3D5896CB04167EE428455E1A241F1FF9E50ABEE6B4DACFE6B14 |
| com.surface.sensormodule.aux_vd55g0_MSHW0492.bin | 136411 | E574DB7EB28231D3FA4F5EEE5C1861919125D8EC7A753FC7A0708606E1F1A794 |

The Karsies repository contains extracted front-camera register lists, but **does not contain the binary decoder or its complete schema**. The statement that the same decoder can simply be run for rear/IR is therefore not currently reproducible from that repository. A publicly visible [Qualcomm XSD mirror](https://github.com/comprehensive9/vendor_qcom_proprietary/blob/11se/chi-cdk/api/sensor/camxsensordriver.xsd) is explicitly marked proprietary; it was not imported and does not prove the Windows serialization schema. No sensor mode or rear/IR register sequence was inferred from those files in this audit.

## Required trace and validation gaps

| Area | Evidence still needed |
| --- | --- |
| All cameras | Match actual device instances to packages; recover CCI controller/master and 7-bit addresses; timestamp ordered rail/clock/reset changes; identify CSIPHY/CSID/VFE route and VC/DT; capture mode-specific register writes and driver versions/hashes. |
| Front IMX681 | Confirm CSIPHY2/trio 0 on this device; link/rate/settle/CDR settings; raw geometry and LLP/FLL; actual live exposure/gain behavior; Bayer order; crop/stride and receiver errors. |
| Rear OV13858 | Sensor ID, actual address, CSI lane mapping/polarity/rate, mode tables, actuator identity and focus transactions, EEPROM/calibration association, and every advertised mode's usable stream result. No rear sensor driver or working rear pipeline is present in the Karsies import. |
| IR VD55G0 | Sensor register map, firmware needs, raw mode geometry, trigger/stream protocol, CCI/PHY routing, frame synchronization, and a separately identified illuminator control path. A related VD55G1 driver is not evidence of compatible register programming. |
| Privacy LED / VCSEL | No LED225 or illuminator GPIO/current/duty-cycle control is established by these six static files. Trace normal Windows operation; retain default-off behavior. Do not experiment by driving unknown PMIC pins or enabling unrestricted IR light. |
| Parity | Per-camera mode/format/frame-rate matrix; meaningful exposure/gain/focus tests; repeated starts/stops; simultaneous-camera constraints; suspend/resume and power draw; LED/error cleanup; applications and image quality. A frame or an advertised profile alone does not close this matrix. |

Only read camera MMIO when its power/clock domain is known active; the reference work reports bus hangs from reads with clocks off. A Windows ETL without decoded hardware/register events is useful correlation, not a replacement for the missing register sequence.

## Provenance to preserve

The repository [license](https://github.com/karsies-wq/sp11-imx681-linux/blob/b08f76f40b8d7b715bd4da6aef484f86142cc147/LICENSE) and per-file SPDX headers identify GPL-2.0 / GPL-2.0-only kernel material. Preserve Linux/Linaro/Qualcomm and original sensor-table attribution, plus Jeroen Karsies' published contribution. Do not invent a Signed-off-by line. The root README identifies Qualcomm downstream 2.1.2 tables and AndreGilerson's Intel sensor work as antecedents; exact upstream commits should be recorded before sending patches upstream.

Userspace patch licensing is described as LGPL-2.1+; tuning YAML carries CC0-1.0; the CCS YAML declares BSD-3-Clause for generated static data. Preserve these distinctions. Extracted Windows parameter values are not the same as permission to redistribute complete Windows packages. Keep driver/tuning binaries and raw biometric data in ignored private output, and publish only reviewed code, provenance, and necessary factual findings.
