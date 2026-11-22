# VD55G0 zero-frames diagnosis — 2026-11-19 session (kernel v7.3.3-jg-1sp11v1)

Scope: first libcamera capture campaign on Larimar (OLED variant), running
tag `v7.3.3-jg-1sp11v1` (= `52812090bd34`, tip of `sp11/ubuntu-qcom-x1e-7.3.y`).
Companion to `camcc-mclk0-vd55g0-investigation-20261115.md`,
`sp11-vd55g0-mclk-boot-diagnostic.md`, `vd55g0-report.md`,
`vd55g0-hblank-illuminator-report.md`. Two sub-agent reports (branch
archaeology + lane semantics) back this file; all live evidence collected
over SSH on 2026-11-19.

## Symptom

libcamera v0.7 (simple pipeline) enumerates cam0
(`/base/soc@0/cci@ac15000/i2c-bus@0/ir-camera@60`) offering R8 + R10_CSI2P
at 644x604. `cam --capture` configures cleanly ("configuring streams: (0)
644x604-R10_CSI2P/sYCC", "Capture N frames"), sensor runtime-PM goes
**active and stays active**, then **zero frames ever arrive** (timeout, no
file). No kernel errors of any kind. Sensor test_pattern=1 (Diagonal Grey
Scale) changes nothing. Direct v4l2-ctl STREAMON (monitors disabled) fails
EPIPE / `camss: Failed to start media pipeline: -32`, but that path never
powers the sensor — topology artifact, not the core issue.

## What is verified PRESENT and WORKING on the running kernel

| Item | Evidence |
|---|---|
| OIF_CTRL fix (`9f9f1c392c56`) | dmesg `OIF_CTRL configured: 0x0001` = 1 lane, phys lane 0, no polarity inversion — exactly the predicted encoding for `data-lanes = <1>` |
| #98 rail ordering (`046314c65252`) | in v1 lineage (merge-base check) |
| CCI0@0x60 routing (`e772d1ab1e26`) | probe OK, `vd55g1 0-0060` bound |
| #73 hblank erratum fix (`c6836ffb48a3`) | live ctrl: `horizontal_blanking min=738 max=738 READ-ONLY` → line length 644+738 = **1382 ≥ 1200 safe-min**; libcamera cannot lower it |
| MIPI rate programming | `link_frequency` ctrl = 384 MHz (read-only, from DT); `pixel_rate` 153.6 MHz |
| Media topology | mid-capture `media-ctl -p`: `vd55g1→csiphy0→csid0→vfe0_rdi0→video0` all `[ENABLED]`, fmt propagated 644x604 Y10 at every pad |
| Sensor liveness mid-capture | controls readable, exposure responds, TPG write accepted, runtime-PM active — sensor MCU + I2C alive **while frames are missing** |

Branch archaeology verdict: **the device already contains every commit on
`sp11-vd55g0-oif-lanes`** (identical `vd55g1.c` and camera DTS vs
`52812090bd34`). The OIF/lane/frequency-presence theory is exhausted: the
fixes are on-device and the symptom persists.

## Eliminated hypotheses

1. ~~OIF_CTRL lane encoding~~ — 0x0001 correct per reference-driver bitfield.
2. ~~DT lane-count mismatch~~ — both endpoints declare `<1>`; imx681's `<0>`
   is C-PHY trio notation (bus-type CSI2_CPHY), not corruption.
3. ~~#73 line-length erratum missing~~ — hblank 738 read-only proves clamp.
4. ~~VFE/RDI mis-routing~~ — all links ENABLED with formats mid-capture.
5. ~~MCLK0 mid-session death~~ — sensor I2C alive during the hang (dead MCLK
   ⇒ dead MCU ⇒ no control reads); also
   `camcc-mclk0-vd55g0-investigation-20261115.md` itself refuted the
   keepalive premise and recommended dropping PR #99. The
   `sp11/mclk-diag-rescue-20261115` branch stays relevant only for
   probe-time I2C failure forensics, not this symptom.

## Standing hypothesis ( sole survivor ): 768 Mbps link rate

- Windows (geocausa, hardware-observed; Chromatix-corroborated):
  **840 Mbps / 420 MHz**, 1200-px line length.
- Our DT (`212e79fff1f4`): **768 Mbps / 384 MHz** — chosen for the
  "19.2 MHz integer rule" (40×19.2), described in the DTS comment as the
  "only valid in-window value". But Windows runs 840 = 43.75×19.2 — the
  integer-rule premise is not what Windows honors.

### The arithmetic (observed control values, reproduced exactly)

`vd55g1_get_hblank_min()` in u32 integer math at 768 Mbps, 644px Y10,
pixel_clock 153.6 MHz (all read live off the device):

- `line_time = (644×10 + 900)/768 = 9` (truncated from 9.56 µs)
- `mipi_len = 9 × 153.6 = 1382` px  → hblank = 1382−644 = **738** ✓ matches
  the read-only control (min=max=738, so the clamp is enforced)
- Full-precision MIPI requirement: **1468 px = 9.56 µs** per line

So the sensor is programmed to 1382-px lines while the MIPI-side budget
needs 1468 px — every line is ~0.56 µs short. In integer math the driver
*believes* it satisfied the constraint (`max(min_line_length,
mipi_req_line_length)` with the truncated 1382), but the real serializer
can't fit a 644×10bpp line + margin into 9 µs at 768 Mbps. The #73
"1200-px line length" fix is present and enforced, yet 1382 < 1468 —
**the truncation hides a per-line bandwidth underrun**: sensor streams
(IMC/MCU alive, TPG accepted), RX decodes nothing, zero errors. This is
consistent with every observed symptom and with Windows choosing 840 Mbps
where the same line fits with margin (`(644×10+900)/840 = 8.74 µs → 8×…`
truncation still bites, but 840 gives real margin at ~1200-px lines).

Driver-side hard rate check: `VD55G0_MIPI_RATE_MIN/MAX = 750/1200 Mbps` —
**420 MHz (840 Mbps) is legal**, `mipi_div=1`, `sys_clk=840`,
`pixel_div=6`, `pixel_clock=140 MHz`. No table blocks it; the only
validation is the min/max window.

## Experiment 1: 420 MHz link (Windows parity) — RESULT: still zero frames

DTB patched in-place inside `/boot/vmlinuz-7.3.3-jg-1sp11v1-qcom-x1e`
(BE-u64 link-frequencies at file offset 377136: `16e36000`→`1908b100`),
backup at `vmlinuz-...bak-384`. After reboot the change is fully live:
`link_frequency` ctrl = 420000000, `pixel_rate` = 140 MHz (840/6, exactly
as the driver's div chain computes), `hblank` = 556 read-only → 1200-px
line length = byte-for-byte Windows' frame timing. `cam --capture` still
delivers zero frames, no errors. **The rate hypothesis is eliminated** —
which also retires the 1382-vs-1468 truncation concern as the blocker.

## Experiment 2: control camera (imx681, C-PHY) — RESULT: WORKS

`cam -c /base/soc@0/cci@ac16000/i2c-bus@1/camera@10 --capture=3` →
3 × 12.67 MB SRGGB10 raw frames. Same libcamera, same camss, same VFE,
same IOMMU, same `cam` binary. **The whole capture stack is proven good
end-to-end.** The failure is specific to the vd55g0/csiphy0/**D-PHY** path
(imx681 rides csiphy2 via the repo's private C-PHY route).

## Experiment 3: IRQ census during hung capture — RESULT: no CSI data at PHY

During a 15 s hung capture: csid0 IRQ count 8→9 (+1, stream-on handshake
only), vfe0 = 0, csiphy0 has **no IRQ line registered at all**. In the
working imx681 run, frames arrive with matching IRQ traffic. Conclusion:
**zero MIPI symbols cross the D-PHY receiver.** The sensor is streaming
(MCU alive, AE running, TPG accepted) but the RX PHY never locks.

## Standing hypothesis (after experiments 1–3): D-PHY RX init sequence

csiphy0 is served by `phy-qcom-mipi-csi2-core.c` +
`phy-qcom-mipi-csi2-3ph-dphy.c` (generic PHY-API route,
`csiphy_v4l2_ops` → `csiphy_stream_on` → `phy_configure` +
`phy_power_on`). Its register table `lane_regs_x1e80100[]` is headed
`/* 4nm 2PH v 2.1.2 2p5Gbps 4 lane DPHY mode */` — imported wholesale in
`70a5d31427f7` (Bryan O'Donoghue, upstream SAUCE, "imported to save
time"); **never validated by observation on this SoC**. Contrast: the
working C-PHY table in `phy-qcom-mipi-csi2-x1e-cphy.c` was transcribed
from live MMIO on this exact device running Windows
(`d9223c234804`, "Register observations independently transcribed from
runtime MMIO activity on the Surface Pro 11").

Supporting arithmetic: the settle-count formula
(`t_hs_settle/period − 6`, integer-truncated) yields counts 2 (384 MHz) /
3 (420 MHz) → realized settle 83.3 / 85.7 ns, both **below** the D-PHY
spec minimum (92.8 / 92.1 ns resp.) — i.e. even the math is marginal,
and the table around it is unvalidated for X1E80100.

Next step: capture the real D-PHY register sequence the way the C-PHY
table was obtained — MMIO-trace Windows driving the VD55G0 (Windows Hello
IR camera), transcribe the csiphy0 region writes, and build an
observation-based `lane_regs` table + settle handling. Alternative if
Windows tracing is unavailable: obtain the X1E80100 D-PHY settings from
turbineBMW's SP11 camera review series (already cited by d9223c234804).

## Experiments 4–7 (2026-11-19/20): driver brought to downstream parity — still silent

All changes below are in the working tree on branch `sp11-vd55g0-oif-lanes`
(uncommitted at time of writing), file
`drivers/phy/qualcomm/phy-qcom-mipi-csi2-3ph-dphy.c`.

### Experiment 4: table vs downstream GPL 2.1.2 audit — RESULT: table genuine, driver missing pieces

Compared the imported table against Qualcomm's GPL
`cam_csiphy_2_1_2_hwreg.h` (qualcomm-linux/camera-driver, master). The
lane-block values are byte-genuine `csiphy_2ph_v2_1_2_reg`. But downstream
applies more than the table (cam_csiphy_core.c):

| Downstream behavior | Our driver before fixes |
|---|---|
| `csiphy_reset_exit_reg_2_1_2`: **2ph exit = CTRL0(0x1000)=0x02** (3ph=0x0e) | never written a mission exit (0x00 left) |
| `csiphy_common_reg_2_1_2`: {0x1084=0, 0x108C=0 + 1 µs} | never written |
| lane-enable BIT layout: DPHY_LANE_0=BIT(0)…CLK=BIT(7) → 1 lane = **0x81** | table wrote stale 4-lane 0xd5 (clobbered correct value) |
| settle via SETTLE_CNT entries | same, but formula truncated (see #5) |

Also confirmed: downstream applies per-datarate AFE tables
(`datarate_212_*Msps`) **only for C-PHY**
(`cam_csiphy_cphy_data_rate_config` is 3phase-gated) — so D-PHY gets
exactly the 2ph table + common regs + lane-enable + CTRL0 exit. karsies'
independent X1E80100 C-PHY work (issue #74) reports the identical
D-PHY-never-receives result with the byte-identical table, corroborating
that the missing driver-side steps — not the table values — are the
difference.

### Experiment 5: three driver fixes — RESULT: applied live, verified by MMIO, still zero frames

1. **CTRL5 clobber fix**: removed {0x1014,0xd5} and {0x1018,0x01} from
   the imported table; `lanes_enable()` now computes CTRL5 from the
   actual lane count (1 lane → 0x81, verified read-back).
2. **Settle round-up**: `settle_cnt` now rounds up (DIV_ROUND_UP) so
   realized settle ≥ t_hs_settle floor (0x11→0x13 at 420 MHz; lane0
   0x0008 and clock-lane 0x0e08 verified).
3. **Unused-lane skip + mission exit + common regs**: lane blocks for
   data lanes ≥ num_lanes are skipped; after lane programming, write
   CTRL0=0x02 (2ph mission mode, matching
   `csiphy_reset_exit_reg_2_1_2`) and the common pair {0x1084=0,
   0x108C=0 + 1 ms} before IRQ masking.

Module rebuilt/redeployed per reboot (srcversions B7FE97F9, E17C2264…);
MMIO mid-stream confirms CTRL0=0x02, CTRL5=0x81, settle=0x13, lanes 1–3
untouched. **Still zero frames.**

### Experiment 6: settle sweep 0x01–0x3f during live TPG — RESULT: dead wire, not mistuning

Sensor TPG on (test_pattern=1), sensor FSM verified STREAMING by the
driver's own s_stream poll. Mid-stream, settle (0x0008 + 0x0e08) swept
0x01..0x3f via /dev/mem while sampling csid0 `CSI2_RX_IRQ_STATUS`
(0x9c), `CSI2_RDI0_IRQ_STATUS` (0xec), PHY STATUS0–3 (0x10b0..0x10bc),
lane0/clk cal_status (0x20/0x0e20). **Union of all IRQ/status registers
across the entire sweep: 0x00000000.** Only STATUS0 bit4 toggled as a
constant settle-update strobe, value-independent. With a mistuned-but-
alive receiver some settling value would produce ECC/CRC/latch bits;
silence across 63 values means **no MIPI energy reaches the CSID**
under any receiver tuning.

### Experiment 7: cross-checks that narrow the fault domain

- Media topology mid-capture: `vd55g1→csiphy0→csid0→vfe0_rdi0→video0`
  all [ENABLED]; topology/routing correct.
- Stale-process red herring eliminated (a forgotten `v4l2-ctl` had held
  /dev/video0 EBUSY; after cleanup, tests reproducible).
- Sensor-side CCI reads confirm OIF_CTRL=0x0001 (D-PHY 1 lane, phys
  lane 0, no polarity inversion) and streaming state.
- SCFG integers (Windows SCFG_AUX_MSHW0492): front/rear `0x150020` vs
  IR `0x1500C0` — IR routing/config differs in bits 6–7; schema not
  decoded (source-audit.md), parked as corroborating-only.

### Standing conclusion after experiments 4–7

The Linux driver now replicates everything the GPL downstream 2.1.2
stack does for a 2-phase D-PHY stream, verified live by MMIO, and the
wire is still dead across the full settle range while the sensor
transmits. The only surviving hypothesis is that Windows' QTI camera
stack programs csiphy0 D-PHY with additional steps beyond the GPL
cam_csiphy core (proprietary extension or a different PHY-mode path).
**The decisive evidence is an MMIO trace of a Windows Hello IR
session** on this device, restricted to the csiphy0 page, per the
gates in sp11-windows-capture/windbg/32-camera-camss-mmio-dump.txt.
Runbook: `sp11-windows-capture/windbg/33-camera-csiphy0-dphy-hello-trace-runbook.md`.
