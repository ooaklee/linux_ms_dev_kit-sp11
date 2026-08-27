# SP11 camera integration review packet

This is a review request, not an instruction to edit, install, run hardware tests, or access credentials. All tool use is disabled for this review. Review the evidence and proposed changes below and the accompanying `source-audit.md` only. State uncertainties explicitly.

## Objective and current state

The user wants front, rear, and IR integration on branch `sp11/integration-7.2.x-ooaklee-karsies-wq-cams`, based on `sp11/integration-7.2.x` at immutable kernel commit `3fc7c5249f4aa9ef70d1f63e82201c8a21b33827` (Linux 7.2.0). The dedicated branch has a clean isolated sparse worktree. Its original checkout was preserved. No camera patches have yet been applied. Live origin freshness could not be checked because bounded remote queries timed out.

The source reference is `karsies-wq/sp11-imx681-linux` at `b08f76f40b8d7b715bd4da6aef484f86142cc147`. This is an out-of-tree bring-up import against jglathe 7.2-rc5, not a portable patch series. The author's resolved front-camera report describes 3840x2640 RAW10 at about 16 fps in libcamera/PipeWire/browser. It does not prove all-camera or Windows parity.

## Windows evidence available on this device

- Windows identifies Surface Camera Front (SONY 0681/MSHW0490), Surface Camera Rear (OVTI D858/MSHW0491), and Surface IR Camera Front (SMO 55F0/MSHW0492), all with OK PnP status. The installed IR sensormodule is named VD55G0; silicon identification was not read live.
- A new WinRT probe initializes only separate unambiguous RGB groups. It never initializes IR or a mixed RGB/IR group, never saves images/audio/pixels, never changes illuminator controls, and always disposes its own readers/captures. It records actual negotiated formats and frame-reference counts.
- Initial 5-second checks observed frames for front 1920x1080 NV12/nominal30, rear 3840x2160 NV12/nominal30 and rear preview 1920x1080 NV12/nominal30. Polling counts are lower bounds, not measured sensor frame rates. A broader bounded RGB source-format sweep is being collected separately and will be reported without inflating its coverage.
- Default RGB source groups advertise 22 frame-source formats. Controller metadata adds preview/photo formats. All supported profiles were separately enumerated without opening them: front6, rear6, IR2. Front profile metadata includes 3840x2160 video and 4032x3024/3840x2640/3840x2160 still images; rear includes 3840x2160 video; IR advertises 644x604 NV12/nominal60. ISP output formats are not raw sensor modes.
- A fail-closed saved-resource parser decoded all six installed SCFG/CAMx_RES files, with exact byte hashes, argument offsets/order, recursive bounds, and no hardware calls. The source audit contains the raw native GPIO/clock/regulator tuples. Numeric fields and DELAY units are not guessed; Windows nominal regulator numbers must be matched to Linux-supported voltage steps.
- The current execution token is not elevated. No WPR/provider ETL, live CCI register write, CAMSS MMIO capture, Hello authentication, VCSEL timing, or Linux hardware test has been completed. These remain explicit gates. No boot/security changes were made.

## Proposed bounded kernel changes to review

### 1. CSID-680 unconfigured RDI crop/drop

The selected kernel's `drivers/media/platform/qcom/camss/camss-csid-680.c` currently programs:

```c
val = RDI_CFG1_TIMESTAMP_STB_FRAME;
val |= RDI_CFG1_BYTE_CNTR_EN;
val |= RDI_CFG1_TIMESTAMP_EN;
val |= RDI_CFG1_DROP_H_EN;
val |= RDI_CFG1_DROP_V_EN;
val |= RDI_CFG1_CROP_H_EN;
val |= RDI_CFG1_CROP_V_EN;
val |= RDI_CFG1_PACKING_MIPI;
writel(val, csid->base + CSID_RDI_CFG1(port));
```

There is no corresponding crop-window/pixel-drop/line-drop programming in that function. The reference identifies this as dropping all RDI pixels on its SP11. Proposed first change: remove the four unconditional enables and explain that crop/drop requires configured windows/patterns. Do not add the reference's `width == 3844` sensor shortcut. The reference also writes period1/pattern0 to frame/pixel/line-drop registers; assess whether those writes are needed when their enables are clear, or whether they introduce unverified semantics beyond the minimal fix.

### 2. VFE-680 RDI write-master mode

The selected kernel's `camss-vfe-680.c` programs `VFE_BUS_WRITE_CLIENT_CFG_EN` only. The reference adds bit16 (mode1, MIPI_RAW), reporting IMAGE_VIOLATION in mode0. The same tree's VFE-480 path already uses `1 << WM_CFG_EN | MODE_MIPI_RAW << WM_CFG_MODE`, with MIPI_RAW mode1 and mode field shift16.

Proposed change: add a named VFE-680 MIPI_RAW mode constant and OR it into the RDI write-client configuration. Keep the selected tree's stride, frame-increment, buffer-size, IRQ and lifecycle code otherwise unchanged. Assess width/stride/alignment risks, especially a packed RAW10 row of3844pixels (4805bytes) versus a padded userspace stride. This patch cannot by itself prove image layout or loss-free capture, and no hard-coded crop will be imported to conceal that gap.

## Explicitly excluded from this initial branch increment

- No global `force_cphy`, maximum-clock override, writable PHY tuning knobs, forced CCS geometry/exposure/gain, ignored I2C errors, or sensor-agnostic width3844 crop.
- No wholesale DTS/DTB/UKI import. The textual reference DTS is an intermediate with incorrect-for-final-front PHY4/D-PHY assumptions and hard-coded phandles.
- No GPIO polling privacy-LED service, no guessed VCSEL controls, and no automatic PipeWire hardening relaxation.
- No camera nodes enabled from these static resources alone. The base CAMSS rejects C-PHY endpoints and PHY API exposes only D-PHY; a real front integration needs scoped C-PHY plumbing and sensor-specific behavior.
- Existing OV13858 uses a19.2MHz reference but has ACPI-centric power/probe behavior; ARM DT support is not supplied by merely adding a node. VD55G1 is a different sensor and must not be aliased to VD55G0.
- No full-parity or deploy-ready claim. Compilation, actual Linux capture, controls, image quality, repeat starts, all advertised modes, concurrency, suspend/resume, illumination safety and applications remain separate checks.

## Requested response

Give a concise review with: (1) approve/revise/defer each of the two candidate patches; (2) any concrete correction to the intended scope; (3) mandatory compile/static and hardware tests; (4) missing evidence that blocks front/rear/IR device-tree enablement and full parity. Do not treat nominal formats, static resource tuples, an ETL file's existence, or compilation as proof of camera functionality. Do not propose executing unknown hardware register writes.
