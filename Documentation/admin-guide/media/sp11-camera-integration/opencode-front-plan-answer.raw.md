# Bounded SP11 front-camera implementation plan

Scope: per-instance IMX681/C-PHY support on branch `sp11/integration-7.2.x-ooaklee-karsies-wq-cams` @ `4c7cf3e7` (base `3fc7c524`), preserving every existing D-PHY consumer and gating rear OV13858 / IR VD55G0 behind evidence. Commits `5954b481` (CSID680 crop/drop disable) and `4e82f0d2` (VFE680 `VFE_BUS_WRITE_CLIENT_CFG_MODE_MIPI_RAW`) are assumed already merged to the tip; they are treated as regression scope, not new work. All references below are to the supplied excerpts.

Guiding constraints: never disguise C-PHY as D-PHY options; no `force_cphy` globals; no guessed GPIO/regulator/CCI/delay/VCSEL values; VD55G1 is not VD55G0; reject unsupported configs per instance; a patch list must not be read as an enabled camera stack.

## 1. Enumerated patch split

**S0 — Regression lock-in of the two merged receiver fixes (no new code).**
Scope: `drivers/media/platform/qcom/camss/camss-csid-680.c` and `camss-vfe-680.c`. Verify the four unconditional horizontal/vertical crop/drop enables are gone while CFG1 ECC/VC fields are untouched, and `vfe_wm_start` sets `BIT(16)|BIT(0)` in `VFE_BUS_WRITE_CLIENT_CFG` with stride/frame-incr/packer/IRQ logic unchanged. Nothing here is sensor-specific; it gates whether the later RAW layout check is meaningful.

**S1 — Generic PHY C-PHY type/options (additive ABI, no behavior).**
Paths: `include/linux/phy/phy.h`. Add `PHY_MODE_MIPI_CPHY` after `PHY_MODE_MIPI_DPHY` (:43), add `struct phy_configure_opts_mipi_cphy` (lanes/trio count, per-trio symbol rate in Hz, line-order map) to `union phy_configure_opts` (:77), and update the union doc. Do **not** create `phy-mipi-cphy.h` or a new generic helper module yet — no upstream helper exists in this tree and no second consumer exists; keep the smallest change. Note `phy_set_mode_ext` (drivers/phy/phy-core.c:379) records `attrs.mode` even with a NULL `set_mode` callback, so the combo PHY's `configure()` can branch on `phy_get_mode()` without adding a callback. Rate units must be explicit: D-PHY uses `hs_clk_rate` (per-lane bit-rate scale); for C-PHY the field must be a per-trio symbol rate and must not be fed into `phy_mipi_dphy_config_validate()` or the `/4` CSIPHY clock-margin derivation.

**S2 — CAMSS endpoint/bus-type plumbing (per-instance state, C-PHY programming stubbed).**
Paths: `camss.c` (`camss_parse_endpoint_node` :4715, `camss_subdev_notifier_bound` :5171), `camss.h` (`struct camss_camera_interface` :144), `camss-csiphy.h`/`camss-csiphy.c` (`csiphy_stream_on` :324, `csiphy_stream_off` :367), `camss-csid.h`/`camss-csid.c` (`struct csid_phy_config` :67, `csid_link_setup` :1249), `camss-csid-680.c` (`__csid_configure_rx` :186).
- Replace the flat `vep.bus_type != V4L2_MBUS_CSI2_DPHY` rejection (:4733) with a switch: DPHY keeps the current parse byte-identical; CPHY stores a bus-type discriminator in the `csiphy_csi2_cfg`, treats `data-lanes` as trio indices, and rejects `clock-lanes` / `line-orders` mismatches; CSI1/CCP2/parallel remain rejected.
- `csiphy_stream_on`: if bus-type is DPHY, take the existing `phy_mipi_dphy_get_default_config_for_hsclk` path untouched. If CPHY, build the cphy opts (`lanes = num_data` trios), `phy_set_mode(PHY_MODE_MIPI_CPHY)`, `phy_configure()`; since S1 leaves the PHY without CPHY handling, `phy_configure` returns `-EOPNOTSUPP` and `stream_on` fails cleanly **before** `phy_power_on` — no MMIO, no PM side-effect. `stream_off` stays `phy_power_off` only.
- `camss_get_link_freq` (:4602) hardcodes `2 * lanes`; leave it untouched for D-PHY and gate the C-PHY multiplier on evidence (Split 3 note).
- `csid_link_setup`: propagate bus type and trio count into `csid->phy` from the linked `csiphy->cfg`; `csid-680.__csid_configure_rx` sets `CSI2_RX_CFG0_PHY_TYPE_SEL` and DL-input mapping from the trio only when that instance is C-PHY, and returns `-EINVAL` for C-PHY on non-680 CSIDs. No `force_cphy`/`camss_dl_map` globals; Karsies' `camss-csid-680.c` CFG0 edits (:215-239 therein) are not imported.

**S3 — Combo PHY C-PHY configure/power (blocked on rate evidence).**
Paths: `drivers/phy/qualcomm/phy-qcom-mipi-csi2-core.c` (`phy_qcom_mipi_csi2_configure` :94, `power_on` :134, `power_off` :168), `phy-qcom-mipi-csi2.h` (`mipi_csi2phy_stream_cfg` :25, `soc_cfg`), `phy-qcom-mipi-csi2-3ph-dphy.c` (`lanes_enable`, `settle_cnt_calc` :257). `configure` branches on `phy_get_mode()`: D-PHY path byte-identical; C-PHY validates `1..3` trios, stores trio mapping and symbol rate in `stream_cfg`. `lanes_enable` gains a C-PHY branch producing the odd-bit trio mask (`BIT(trio*2+1)`) and selects a C-PHY init row from a new provenance-annotated table file — the Karsies `lane_regs_x1e_cphy_*` tables (1p0/1p2/…/2p5 Gsps, `CSIPHY_CDR_LN_SETTINGS` type) may be ported **only** as per-instance, kernel-parameter-free tables keyed by a validated rate entry, with the `settle`/`CDR` overrides (`mipi_csi2phy_cphy_*`) dropped. The `/4` clock-margin and the `min_rate=0` max-clock hack (Karsies core:55) are not portable; the correct CSIPHY clock derivation for C-PHY is a measured gate, not a copy. Everything not currently supportable (unsupported SoC, out-of-table rate, >3 trios) returns `-EINVAL` from `configure` or `lanes_enable` with no register side effects.

**S4 — CCS IMX681 identification/quirk, no global overrides (partially implementable now).**
Paths: `drivers/media/i2c/ccs/ccs-core.c` (`ccs_module_idents`, `ccs_identify_module` :2780, `ccs_get_hwconfig` :3149), `ccs-quirk.h`/`ccs-quirk.c` (`struct ccs_quirk`), new `sensor-data/ccs-sensor-4260-0681-<rev>.fw`. Implementable now: add a `CCS_IDENT_*` entry for Sony model `0x0681` (manufacturer `0x4260` per the Karsies firmware filename), a quirk stub whose `init`/`limits` set only verified values, and the four "read-only broken limit" replacements (`min/max_op_sys_clk_freq_rev`, `min/max_op_pix_clk_freq_rev`, `min_pll_op_clk_freq` as correctly-encoded IEEE‑754) into a rebuilt `.fw`. **Excluded:** Karsies' widened `max_op_pix/min_vt_pix` calculator workarounds, the 4040→4032 and src-format→3844 forward fixes, the `ccs_link_validate` internal-link bypass, and the global analogue-gain/exposure overrides — these remain out until silicon identity, mode negotiation and line-layout are measured.

**S5 — Front DT wiring (blocked).**
Paths: `arch/arm64/boot/dts/qcom/hamoa.dtsi` (camss :5636, csiphy0/1/2/4 :5794-5880 — note: bindings require `vdda-0p8/1p2` which these nodes currently lack), `x1-microsoft-denali.dtsi`, and the two Denali variant `.dts`. Binding `qcom,x1e80100-camss.yaml` and `mipi-ccs.yaml` already accept `bus-type: 1` with `data-lanes`/`link-frequencies`; `video-interfaces.yaml` already defines `line-orders` for C-PHY. Only documentation/threshold validation is actionable now (add a `line-orders` example); every node value (CSIPHY2/trio0, GPIO237/MCLK4, CCI master/address, LDO3_M+7_B steps/order, delays) is blocked on DSTATE-state and CCI evidence.

**S6 — Rear OV13858 and IR VD55G0 (research-only).**
`ov13858.c` exists with ACPI-centric power/probe and a hard 19.2 MHz check (:1658-1679); OF/power/resource work is gated on CCI/rail/DSTATE3 evidence, mode tables, VCM identity and EEPROM association. VD55G0 has no driver here; VD55G1 is a different device and must never be aliased. No slice.

Dependency order: S1 → S2 → S3 → S4; S5/S6 independently gated. Implementable/testable with sensors disabled today: S0, S1, S2, and the identification/limits portion of S4 (plus `.fw` build via the CCS `ccs-yaml-to-bin` tool, which is a build artifact, not hardware work). Blocked on new evidence: S3 C-PHY rows/rates, S4 geometry/quirk content, S5, S6.

## 2. Tests per split

- **S0:** focused `arm64` object rebuild of CAMSS (baseline-before/after object diff), `git diff --check`, `checkpatch --no-signoff`. Keep the inherited-CI caveat: the project’s `echo|grep -q` broken-pipe bug lets the broad script exit 0 falsely and carries 11 pre-existing ERRORs; that exit is not a clean-CI signal. Later hardware: RDI input/drop/buffer-done counters plus `TOTAL_PKTS`/`ECC`/`CRC` (0x240/0x244/0x248) during an actual stream; VFE `IMAGE_VIOLATION` status (lite 0x270 / full 0xc70) and 4816-vs-4805 line-layout check for MIPI_RAW on both full and lite 680 paths.
- **S1:** enum-order/union-size compile assertions; negative check that `phy_validate(PHY_MODE_MIPI_CPHY, cphy_opts)` on the current DPHY-only driver fails rather than misparsing; confirm `phy_set_mode` records mode with no callback.
- **S2:** compile + checkpatch + `dt_binding_check`/`dtbs_check` on a *disabled* draft endpoint (reverted before merge) to exercise the yaml path; pipeline-level negative tests: CPHY endpoint with 0 or >3 trios → `-EINVAL`; CPHY with `clock-lanes` present → parse error; C-PHY on non-680 CSID → `-EINVAL`; a CPHY `stream_on` returns `-EOPNOTSUPP` (or DPHY-identical failure) with zero MMIO/PM writes; repeated start/stop of a DPHY instance leaves stream state clean. Hardware later: mixed CPHY(port2)/DPHY(port0/others) sequential and concurrent streaming; 100+ start/stop cycles; suspend/resume then re-stream; error-unwind check that `power_on` regulator failure still reaches `regulator_bulk_disable` (core :134-165) in both modes.
- **S3:** D-PHY `configure`/`lanes_enable` register stream must be bit-identical pre/post (code-inspection diff); negative: lanes 0 or 4-8 for CPHY, unknown rate index, unsupported SoC → `-EINVAL` with no writes. Hardware later: correct trio/rate/mapping observed via CSID `CFG0_PHY_TYPE_SEL`, PHY lane-enable mask, and RX stats; wrong-trio injection must surface as VC/DT or CRC errors in counters, not as silent garbage; the C-PHY clock-selection formula validated on the 1.0 Gsps row.
- **S4:** `.fw` builds; ident/quirk guard tested so a non-`0x0681` probe gets no quirk and existing D-PHY sensors (e.g. rear) behave unchanged; PLL calculator statically linked at 969.6 MHz with corrected limits. Hardware later: model/revision readback, geometry/blanking and quirk register writes verified against the pipeline; exposure/gain readback and Bayer order confirmed before any control override is accepted.
- **S5/S6:** no tests until evidence; then per-camera frame/layout/FPS/control/quality plus VCM and calibration gates.

## 3. Evidence still needed → decisions unblocked

| Row | Observation needed | Unblocks | Note |
|---|---|---|---|
| Front IMX681 | Real CSIPHY2/trio0 use; CCI master + 7-bit addr; GPIO237/MCLK4 polarity and timed rail/reset order (LDO3_M/7_B, supported PMIC steps); link rate, AFE/CDR rows, settle; raw geometry/blanking; actual exposure/gain writes; line-layout (4805 vs 4816) | S3 rate-table/trio selection and clock formula, S4 geometry/quirk, S5 DT/power | ETW/WinRT streams prove format exposure only; they contain **no** CCI/MMIO write content, so register decisions need a validated driver/PDB-scoped CCI capture, not standard ETW. |
| Rear OV13858 | Sensor ID/address, lane map/polarity/rate, mode tables, DSTATE3 shutdown order, VCM/EEPROM identity and transactions | S6 OF/power/probe path + receiver graph; add nothing to `ov13858.c` until then | 19.2 MHz check already enforced; static DSTATE 0 order is not shutdown order. |
| IR VD55G0 | Real protocol/firmware/modes, raw geometry (advertised 644×604 NV12 ≠ RAW10 mode), trigger/sync, illuminator control path and privacy LED identity | S6 IR slice; none today | A VD55G1 driver proves nothing here. |
| Biometric/Hello | Attributed Hello outcome and indicator/illumination timing | Userspace only: which source feeds auth, LED policy | Driver obligation is frame delivery + metadata + a default-off privacy GPIO; liveness/security is userspace policy and never belongs in the kernel; Windows Hello success cannot validate a Linux stack. |

## 4. Safest next implementable slice

Two-commit slice, zero register guesses, reviewable/compilable before any hardware:

- **Commit A:** `include/linux/phy/phy.h` — `PHY_MODE_MIPI_CPHY` + `phy_configure_opts_mipi_cphy` union member + doc. Purely additive; no behavioral change.
- **Commit B:** CAMSS plumbing — `camss.c`, `camss.h`, `camss-csiphy.c`, `camss-csid.c`/`camss-csid.h`/`camss-csid-680.c`: parse CPHY endpoints per instance, carry bus type through the async subdev → CSIPHY → CSID route, and make CPHY `stream_on`/`configure_rx` fail deterministically (`-EOPNOTSUPP`/`-EINVAL`) until S3 adds PHY support.

Acceptance boundary: the tree compiles (focused CAMSS/PHY object build), `git diff --check` and `checkpatch --no-signoff` clean, the D-PHY code path is unchanged by inspection, no module parameters exist, no DT/sensor enablement is added, and any C-PHY endpoint produces a clean error with zero MMIO/PM side effects. If a fully independent functional test of the stub is required beyond compile/review, it needs a mocked or disabled-at-boot DT fragment that is reverted before merge — otherwise say so rather than claiming runtime proof. I see no correct slice that is richer than this without guessing.

## 5. Ambiguities / defects to flag

- The four crop/drop writes removed by `5954b481` and the full body of `__csid_configure_rdi_stream` are not in the excerpts, so the “CFG1 fields untouched” claim cannot be re-verified here.
- `CSI2_RX_CAPTURE_CTRL_CPHY_PKT_DT GENMASK(20, 25)` (csid-680 :125) has inverted hi/lo arguments (likely `GENMASK(25, 20)`) and `CAPTURE_CTRL` is never written anywhere in the excerpt — do not program it without evidence.
- `lane-polarities` indexing assumes a clock lane at `[0]` (camss.c :4742/4753); C-PHY has no clock lane — confirm fwnode fill order before reusing it.
- The PHY binding requires a static `phy-type` (10/11) but the driver never consumes it; two possible sources of truth for mode. Pick the per-endpoint bus-type as authoritative.
- `hamoa.dtsi` csiphy nodes omit required `vdda-*` supplies; enabling front will need them.
- Port numbering vs `phy-names` (0,1,2,4) and the Karsies README (CSIPHY2/trio0) vs its textual DTS (CSIPHY4/D-PHY) disagree; device-side confirmation is required before S5.
- `camss_get_link_freq`’s `2 * lanes` and the `/4` clock margin are D-PHY semantics; the C-PHY units (symbol rate, `CPHY_CONST=7`/`PHY_CONST_DIV=16` in `ccs-pll.c`) must be reconciled by measurement, not by the reference’s `min_rate=0` fallback.
- The combo PHY’s DPHY `configure` still hardcodes `pos=i`, `clk=7` (core :123-129); the C-PHY trio/line-order mapping must not inherit that silently.
- `BIT(16)` MIPI_RAW semantics on 680 are unproven (VFE-480 precedent only) and the 3844 payload/4816-stride question stays a measurement gate, not a crop license.
- Earlier advisory slips (OV13858 absence, VFE480 expression, 8704-line-length-vs-frame-length conflation) are corrected by the supplied sources; the plan above follows the current excerpts only.
