# SP11 camera integration evidence — 2026-08-27

## Result

Windows delivered frame references in **22 of 22 selected RGB source-mode checks**: 7 front recording modes, 7 rear preview modes, and 8 rear recording modes. Each ran for at least the requested five seconds and negotiated the requested format. No image, video, audio, or IR sample was saved. All 158 capture artifacts passed length/SHA-256 verification; all six decoded static resources still match their installed source bytes.

**This does not establish Linux camera support or full Windows parity.** There are no privileged WPR/provider ETLs, live CCI register writes, active CAMSS register dumps, Hello authentication results, or Linux hardware tests in this run. IR is explicitly `manual-required`. Additional profile modes, still photography, control behavior, image quality, concurrency, resume, and illumination safety remain untested.

The requested kernel branch, `sp11/integration-7.2.x-ooaklee-karsies-wq-cams`, was created at `3fc7c5249f4aa9ef70d1f63e82201c8a21b33827` (Linux 7.2.0). An isolated sparse worktree preserves the original checkout and its pre-existing case-collision changes. The branch's first increment is limited to reviewed receiver configuration fixes; it must not be described as an enabled or deployable camera stack.

## Evidence and reproducibility

| Artifact | Purpose |
| --- | --- |
| [evidence-verification.json](evidence-verification.json) | Verified artifact counts, capture source hashes, static file hashes, and explicit untested gates |
| [windows-rgb-stream-results.csv](windows-rgb-stream-results.csv) | Every tested source format, negotiated result, duration, and frame-reference count |
| [windows-advertised-profiles.csv](windows-advertised-profiles.csv) | Profile descriptions; these rows are advertised, not tested |
| [windows-control-capabilities.json](windows-control-capabilities.json) | Read-only RGB control capabilities/current values, not control-operation tests |
| [windows-validation.json](windows-validation.json) | Final script hashes, both offline test runs, and the corrected helper's independent live smoke result |
| [source-audit.md](source-audit.md) | Pinned source review, native resource tuples/offsets, provenance, and migration hazards |
| [opencode-review-answer.md](opencode-review-answer.md) | Advisory OpenCode review, including a separately documented factual correction |
| [kernel-validation.json](remote-kernel-validation/kernel-validation.json) | Exact code commits, configuration/object hashes, scoped build results and the inherited CI caveat |
| [camera-integration.patch](remote-kernel-validation/camera-integration.patch) | The complete two-file kernel change, without sensor or illuminator enablement |
| [Build-CameraEvidence.ps1](Build-CameraEvidence.ps1) | Recheck private artifacts and reproduce the small analysis exports |

Private complete capture: `output/20260827-122112986-camera-trace-integration-7.2-all-cameras-r2/`, 11:21:13–11:25:07 UTC. Windows build `10.0.29585.0`; Windows PowerShell `5.1.29585.1000`; non-administrator token. The manifest SHA-256 is `3BD9C69AB1123952F6E84E3F71ADDC6D3A36E7A187C5D8A9026F813E5014FA9A`. Metadata records the precise collector/support/probe hashes used in that run; subsequent safety fixes are not retroactively represented as the captured revision.

Private static decode: `output/camera-static-20260827-121608-270-67c835de/`. Detailed JSON retains source paths and byte offsets. Only factual summaries and hashes are included in this analysis directory; Windows binaries and private raw logs are not imported into the kernel repository.

The first broad PnP scan was cancelled before camera phases because it queried properties for every unrelated device. Its separate directory contains `RUN-INCOMPLETE.json` and is excluded from success counts. The completed run selected 12 camera-related candidates from 445 present devices and retained their full parent chains.

### Collector changes and final verification

The updated collector scopes PnP queries to camera candidates and their parents, records package/resource provenance, and separates front/rear mode checks from an operator-authorized Hello phase. WPR and provider ETW use unique owned session names, bounded child processes and watchdogs; cleanup never targets another recorder. Unattended runs never lock Windows, open IR/mixed groups, save frames or write illuminator controls.

The WinRT helper records negotiated formats and frame references, advertises additional profiles separately, and rejects reused or interrupted output directories. A final live check found an overly strict classifier rejecting the rear camera's `Image/Photo` metadata companion. The corrected classifier requires matching camera identity, accepts that metadata source, and still prohibits photo readers and IR/depth/custom sources. The failed smoke is retained separately, not counted as success.

After the fix, **19 tracing regression tests and 49 static decoder checks passed under both Windows PowerShell 5.1 and PowerShell 7.6.4**. A fresh five-second-per-source smoke delivered rear 4K recording, rear 1080p preview and front 1080p recording frames with matching formats, no errors, no saved images and no IR initialization. Its three artifacts passed hash/length checks. This verifies the final helper revision; it is not another all-profile sweep. The [validation record](windows-validation.json) preserves the exact tested source hashes.

## What Windows actually exposed and exercised

| Camera | Observed source-mode checks | Additional advertised information | Not established |
| --- | --- | --- | --- |
| Front, SONY 0681 / MSHW0490 | 7 recording checks: 640×360, 640×480, 1280×720, 1440×1080, 1920×1080, 1920×1440, 2560×1440; NV12, nominal 30 fps | 6 profiles, including 3840×2160 recording and photos up to 4032×3024; exposure, white balance and zoom capabilities; focus unsupported in this API | Profile-specific 4K streaming, still capture, actual sensor frame rate, image quality or Linux support |
| Rear, OVTI D858 / MSHW0491 | The same 7 preview sizes, plus 8 recording checks including 3840×2160; NV12, nominal 30 fps | 6 profiles; still formats include 4076×2806, 4064×2286 and 3736×2802; focus modes Auto/Single/Manual/Continuous and ISO/exposure/WB/zoom capabilities | Autofocus/VCM transactions, still capture, calibration, image quality or Linux support |
| IR, SMO 55F0 / MSHW0492 | None; neither IR nor a mixed RGB/IR group was initialized | 2 profiles advertise 644×604 NV12 at nominal 60 fps; installed sensormodule is named VD55G0 | Silicon identity readback, raw geometry, actual IR frames, Hello success or VCSEL/LED behavior |

`AllModes` means formats exposed by the initialized RGB frame-source groups. It does **not** mean every profile, Windows ISP feature, raw sensor mode, or still-photo mode. Static profile enumeration recovered higher-resolution modes that the default groups omit. A future profile-specific test must select the proper profile explicitly and continue excluding IR from unattended activation.

Polling `TryAcquireLatestFrame` yields a lower-bound count of frame references. Its derived rate must not be compared as a measured sensor FPS against either Windows' nominal rate or another developer's Linux result. No pixels were inspected, so a delivered frame is not proof of useful image quality.

The inventory found 11 matching registered ETW providers, 63 camera-package files, 30 driver-file records and 27 registry-export records. Provider discovery is not event emission or CCI coverage. The completed run contains **zero ETLs** because elevation was unavailable.

## Static resources and upstream findings

All six selected SCFG/CAMx_RES files decoded as saved `ACPI_EVAL_OUTPUT_BUFFER_V1` structures, with checked signature, exact lengths/counts, recursion bounds, types, integer widths, terminators and padding. The decoder never evaluates ACPI or touches hardware. See the [source audit](source-audit.md) for complete ordered tuples and hashes.

The resource declarations reference front GPIO tuple 237 / MCLK4, rear tuple 110 / MCLK1, and IR tuple 109 / MCLK0, with distinct regulator sequences and declared 19.2 MHz clocks. Native tuple fields, GPIO polarity, delay units, actual timing and rail voltages are not inferred. Linux regulator constraints must use supported voltage steps; copying nominal Windows numbers blindly is unsafe.

The [resolved author comment on jglathe #74](https://github.com/jglathe/linux_ms_dev_kit/issues/74#issuecomment-5302651457) supersedes that issue's older SOT-blocker title. The [pinned Karsies source](https://github.com/karsies-wq/sp11-imx681-linux/tree/b08f76f40b8d7b715bd4da6aef484f86142cc147) reports front capture through libcamera/PipeWire/browser. It provides valuable receiver and sensor evidence, but contains global C-PHY/CCS overrides, forced dimensions, permissive PLL workarounds and a polling LED helper that are unsuitable as all-camera defaults.

Its sensormodule binary decoder/schema is **not** present in the repository. The new ACPI resource decoder does not decode Chromatix sensor initialization or mode tables. Rear/IR sensor-write sequences and illuminator controls have not been recovered by this work.

## Kernel migration decision and review

OpenCode 1.18.23 reviewed the sanitized evidence packet with every tool denied, no MCP calls, and no global configuration/authentication changes. The review completed successfully with zero tool events. [Review metadata](opencode-review-metadata.json) pins the exact inputs: the current source audit was refined afterward and was not itself replayed to OpenCode. An independent code review reached the same narrow scope:

1. **CSID-680:** remove the four unconditional horizontal/vertical crop/drop enables. Leave the other CFG1 fields unchanged. Do not add unproven period/pattern writes or a sensor-specific width shortcut.
2. **VFE-680:** name and set the MIPI_RAW mode bit in the RDI write-master configuration, preserving existing stride, image configuration, packing, frame increment and IRQ handling. This remains an experimental 680-family candidate until register semantics and actual buffer layout are confirmed on hardware.

The reviewer incorrectly said that no OV13858 driver exists in one checklist. The selected kernel **does** contain `drivers/media/i2c/ov13858.c`; it has ACPI-centric probing/power handling, so adding ARM DT nodes alone is insufficient. The advisory note also corrects its VFE480 expression and its conflation of the reference's 8704 line length with frame length. The audit adopts neither those errors nor speculative period/pattern semantics as hardware facts.

**Alignment gate:** RAW10 at width 3844 needs 4805 payload bytes per row, whereas the base advertises a 4816-byte aligned stride. At width 3840, 4800 bytes is already aligned. Continuous raw mode may not insert the advertised padding. Do not import a global 3844→3840 crop or change reported stride to hide the discrepancy; inspect actual frame layout and receiver errors. Both full and lite X1E680 RDI paths, other sensors and D-PHY operation need regression tests.

The following are deliberately not enabled: sensor nodes, C-PHY endpoints, rear power/VCM support, VD55G0, a privacy LED GPIO or any VCSEL control. The base rejects C-PHY endpoints and exposes only D-PHY through the relevant PHY API. A complete front port requires per-instance C-PHY support and sensor-specific behavior. VD55G1 is not a substitute for VD55G0.

### Build and branch status

The immutable base was archived into an isolated native arm64 Linux Docker filesystem to avoid the Mac volume's filename case collisions. Native GCC 13.3, make 4.3, arm64 defconfig, CAMSS/module configuration, `modules_prepare`, and the baseline CAMSS composite build passed. The baseline has three pre-existing duplicate-symbol configuration warnings.

| Commit | Change |
| --- | --- |
| `5954b481d83679d36d14f9198c5490df7ef9938d` | Disable the four unconfigured CSID680 crop/drop enables |
| `4e82f0d2e74816b430438f544864233dc7010415` | Name and select VFE680 MIPI_RAW write-client mode |

Both commits credit the pinned Karsies source and preserve the upstream file attribution. The two-code-commit range passes `git diff --check`; each commit's checkpatch run passed with zero errors/warnings/checks. Checkpatch explicitly used `--no-signoff`; no developer certification was invented. Both changed objects and the CAMSS composite rebuilt as AArch64 with zero compiler warnings, matching the baseline. An independent recheck confirmed the container source bytes match the committed worktree, the object hashes and ELF architecture match the validation record, and the CAMSS target remains buildable. This is a focused object build, not a full kernel image, allmodconfig, sparse analysis or Linux hardware test.

The documentation bundle preserves the exact patch and mail-series snapshots. Their blank context lines and mail signature separators contain format-required trailing spaces; default diff-checking of these newly added evidence files reports those lines. Do not rewrite the snapshots merely to remove that diagnostic. Authored documentation and other payloads are checked separately.

**Existing integration-CI failure:** the repository's broad check emitted the same 11 error records on both the base and patched tree. Its intended project-author filter retains three existing audio errors and no errors in either changed CAMSS file. An existing `echo`/`grep -q` broken-pipe bug masks the error branch and lets the script exit zero. That exit is **not a clean full-CI result**. The unrelated CI/audio code was left untouched; raw diagnostics are retained privately and the portable validation record includes the comparison.

The original checkout's branch/status and all 13 pre-existing modified-file hashes were rechecked unchanged. The camera worktree was clean after the two code commits. Remote-origin freshness remains unverified because bounded ref queries timed out; this work is pinned to the recorded immutable base. No branch is pushed, boot image installed, camera hardware programmed or parity issue closed by this work.

## Remaining work before enablement or parity claims

| Gate | Required evidence |
| --- | --- |
| Privileged Windows traces | Separate RGB WPR Video/provider ETLs from the checked script; decode event coverage and loss, rather than trusting file existence |
| IR/Hello | One operator-authorized normal Windows Hello attempt, attributed outcome and indicator/illumination timing; no saved IR image or direct emitter writes |
| Sensor register traffic | Validated driver/PDB-specific CCI capture: addresses, register widths, ordered mode/control writes, delays and completion status |
| Receiver routing | Clock-on, active-camera-only PHY/CSID/VFE observations; actual route, lanes/trios, VC/DT, rates, errors and buffer-done counters |
| Power and indicators | Verified native-to-Linux mapping, supported regulator steps, reset/clock/rail ordering, error cleanup and privacy/illumination limits |
| Front #43 | Scoped IMX681 support, C-PHY transport, geometry/blanking/control fixes, crop/stride verification and userspace ISP/calibration |
| Rear #41 | DT-capable OV13858 power/probe path, verified receiver graph, VCM/autofocus, EEPROM/calibration and mode/control tests |
| IR #42 | Exact VD55G0 protocol/firmware/modes and synchronization; separately reviewed safe illuminator path |
| End-to-end Linux | Real hardware boot; per-camera frame/layout/FPS/control/quality checks; starts/stops, concurrency, suspend/resume, power and application tests |

Use [the camera tracing guide](camera-tracing-guide.md) for the elevated RGB and interactive Hello commands. Existing WinDbg CCI/MMIO files remain intentionally non-runnable until the actual driver symbols, mappings and powered domains have been reviewed. Do not enable KDNET, change BitLocker/Secure Boot, or read clock-gated CAMSS registers as an automatic continuation of these scripts.

Full parity remains open across [front #43](https://github.com/ooaklee/linux-surface-pro-11-oe/issues/43), [rear #41](https://github.com/ooaklee/linux-surface-pro-11-oe/issues/41) and [IR #42](https://github.com/ooaklee/linux-surface-pro-11-oe/issues/42). No result in this report closes those issues.
