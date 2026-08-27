# SP11 Camera Integration Review

> Maintainer verification note: this is an advisory model review, not hardware evidence. Its final checklist incorrectly says no OV13858 driver exists: the selected base contains `drivers/media/i2c/ov13858.c`, with ACPI-centric probing/power handling. Its proposed period/pattern semantics were not independently established and are not adopted. The accepted CSID scope simply clears the four feature enables. The unmodified original answer is retained privately with SHA-256 `AFAD8503E8954A8ABC2BFDFF035E75187139F0AA34B7304B5C594E50A0865CD3`.

> Two further corrections: VFE480 actually writes `1 << WM_CFG_EN | MODE_MIPI_RAW << WM_CFG_MODE`; its macros denote bit positions and the mode value, not the expression quoted below. The reference's value 8704 is `LINE_LENGTH_PCK` only, not `FRAME_LENGTH_LINES`. These statements in the preserved review are not accepted as evidence.

> Review provenance: OpenCode received the evidence packet with SHA-256 `A921F6FB7CD886571008CA95737FE9A34D33F0202B9839EB970096E0C424C09A` and the earlier source audit with SHA-256 `B71490083D45BACE1BE4FD016B9C91E64BC07F5DDC80475A5EC3518A6BDBB7A7`. The current source audit was revised afterward; this review does not attest to its current bytes.

## 1. Candidate patch verdicts

### Patch 1 — CSID-680 unconfigured RDI crop/drop — **Approve (revised)**

Agree that enabling `CROP_H_EN/CROP_V_EN/DROP_H_EN/DROP_V_EN` with no programmed window in `camss-csid-680.c` is a self-inconsistent state and a correct target for removal. On SP11 the reference author observed all RDI pixels dropped, and no stream counter evidence exists to the contrary. Rejecting the reference's `width == 3844` sensor shortcut is the right call: format width alone identifies neither the sensor nor hardware capability, and importing it would leak sensor-specific behavior into a generic receiver path.

The open sub-question is the reference's `period1/pattern0` writes to the frame/pixel/line-drop registers. My assessment: with all four enables cleared those registers are inert (don't-care), so writing `period1/pattern0` is **unnecessary**. They can seem "harmless" but they carry latent semantics — a period/pattern of 1/0 is only meaningful if a future change flips an enable bit, at which point the behavior becomes "drop everything" by accident. The safer scope is:

- Remove only the four unconditional enables; leave `BYTE_CNTR_EN`, `TIMESTAMP_EN`, `TIMESTAMP_STB_FRAME`, `PACKING_MIPI` untouched.
- Do **not** add the pattern writes in this increment. If a future hardware test shows stream framing requires an explicit no-op pattern, add it then, with a named register/pattern definition and a documented reason.
- Do not introduce any crop-window programming here; window/drop values belong with a validated per-sensor pass-through test.

This patch is compile-validatable only. It proves nothing about image layout; the gate is comparing RDI input vs drop vs buffer-done counters during an actual stream on this device.

### Patch 2 — VFE-680 RDI write-master MIPI_RAW mode — **Acceptable as candidate, hardware acceptance deferred**

The change is small, bounded, and correct in shape: a named constant OR'd into the existing write-client configuration, with stride/frame-increment/buffer-size/IRQ/lifecycle left alone. The in-tree VFE-480 precedent uses `WM_CFG_EN | WM_CFG_MODE << 16` and the reference's mode0 → `IMAGE_VIOLATION` report are corroborating but **not proof on this device**. Two uncertainties must be resolved before treating this as more than a compile candidate:

1. **Cross-generation semantics**: VFE-680 and VFE-480 are different blocks. The bit16/mode-field meaning must be confirmed against the VFE-680 register definition in the tree (and, if available, product documentation) before OR-ing it in. The "MIPI_RAW" naming inherited from 480 is an assumption until the 680 field is read in-tree.
2. **Width/stride/alignment**: a packed RAW10 row of 3844 pixels is 4805 bytes — not a multiple of 16/32/64. Combining this patch with Patch 1 (no crop) means the VFE must describe whatever width CSID actually passes. If mode1 changes unpacking/striding semantics, buffer and bytes-per-line calculations must be re-derived from measured output, not assumed from the reference. No hard-coded crop should be imported to paper over this; the audit's requirement to check bytes-per-line, frame boundaries, alignment-padded widths, and `IMAGE_VIOLATION` firmware response is mandatory before claiming capture.

**Both patches together are prerequisites, not enablement.** Even merged, they do not wire a camera node, supply a sensor driver, or produce a frame on this device.

## 2. Corrections to intended scope

- **Front enablement is currently unachievable.** The base tree's CAMSS rejects C-PHY endpoints and the PHY API exposes only D-PHY; the front runs C-PHY on the reference device (CSIPHY2/trio 0). With C-PHY plumbing explicitly excluded, this increment cannot deliver a front camera — at most it prepares receiver bits. That should be stated openly rather than implied.
- **Rear and IR are also excluded by content.** The reference import has no OV13858 driver and no VD55G0 support; VD55G1 must not be aliased. These two patches touch neither. Do not let "integration branch" imply three-sensor scope; the branch increment is receiver-configuration-only.
- **Static resource numbers must not migrate into fixed constraints.** Windows nominal regulator values (1.8V/1.2V/2.8V/1.15V/2.9V, MCLK 19.2 MHz) must be matched to PM8550-supported voltage steps. The reference documents an invalid request breaking the whole regulator bank and needing 2,904,000/1,152,000 µV. Any future power-enablement change validates ranges first; nothing in these two patches touches power, which is correct.
- **Do not interpret decoded SCFG trailing integers as CCI address/sensor ID/routing**; that is resemblance-based guesswork. Do not treat the installed sensormodule binaries as decodable — the reference repo lacks the binary decoder and its schema; the mirrored XSD is explicitly proprietary. No rear/IR register sequence was recovered; none should be invented.
- **Keep the privacy/illuminator surface default-off.** No LED225 or illuminator control exists in the six decoded files; no polling GPIO service, no guessed VCSEL writes. Preserve.

## 3. Mandatory compile/static and hardware tests

**Compile/static (can be done now, proves only code validity):**
- Build the branch against immutable `3fc7c5249f4` with CAMSS enabled; at minimum arm64 defconfig + CAMSS modules and ideally a broader allmodconfig-equivalent; confirm no new warnings.
- checkpatch/sparse clean; confirm named constants exist in the 680 headers and no other call sites are affected by removing the enables.
- Explicitly compare VFE-680 vs VFE-480 mode-field definitions in-tree to substantiate the MIPI_RAW rename.

**Hardware (none in current packet — all remain open gates):**
- Stream on this device with CSID input/drop/buffer-done counters read during an active stream; confirm framing changes pre/post Patch 1.
- Read VFE status for `IMAGE_VIOLATION` with the mode bit toggled; confirm bytes-per-line and buffer byte counts match 3844-px (or post-RDI width) rows; repeat starts.
- Only read camera MMIO with the power/clock domain confirmed active (reference reports bus hangs from cold reads).

**Explicitly not acceptable as evidence:** nominal advertised formats (they are ISP output, not raw sensor modes), Windows profile metadata, static resource tuples, existence of an ETL file without decoded register events, or successful compilation.

## 4. Evidence still blocking front/rear/IR enablement and parity

1. **Instance-to-package mapping** and CCI controller/master/7-bit addresses per camera; recovered from live probe, not from SCFG integers.
2. **PHY routing** per camera (CSIPHY/CSID/VFE, VC/DT), lane/polarity/rate; front C-PHY plumbing absent in-tree; mixed C-PHY/D-PHY coexistence for rear/IR unproven.
3. **Sensor drivers**: IMX681 (verify model/revision, geometry, LLP/FLL 8704, exposure/gain readback), OV13858 (none exists), VD55G0 (none; firmware needs, trigger/stream protocol, raw geometry, sync).
4. **Actual register sequences**: no live CCI writes, no decoded ETL register events, no measured frame rates (poll counts and "nominal 30/60" are lower bounds/ISP-claimed, not sensor measurements).
5. **Power/regulator validation** and latched rail/clock/reset ordering (DSTATE 3 shutdown order is not simple reversal and is unreviewed in this packet).
6. **Illuminator/VCSEL/privacy-LED path** — unestablished; keep default-off.
7. **Origin freshness**: bounded remote queries timed out; all conclusions rest on snapshot refs `3fc7c5249f4` and `b08f76f4`, which should be re-verified against live origin before merging.
8. **Parity matrix** per camera (modes/formats/frame rates), exposure/gain/focus tests, repeated start/stop, simultaneous-sensor constraints, suspend/resume, illumination safety — none present.

**Bottom line:** Approve Patch 1 in revised form (enables only, no pattern writes) and accept Patch 2 only as a named, bounded compile candidate with hardware acceptance deferred pending register-definition check and live stream counters on this device. Neither patch, nor this entire branch increment, constitutes camera functionality, DT enablement, or parity — those remain gated on the evidence above.
