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
