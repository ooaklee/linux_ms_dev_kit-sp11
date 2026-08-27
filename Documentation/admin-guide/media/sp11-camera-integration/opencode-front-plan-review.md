# Reviewed front-camera implementation plan

This review used kernel commit `4c7cf3e7d96359b02eb7c2780dd32bda5927a6f5`, the existing sanitized report, and Karsies reference commit `b08f76f40b8d7b715bd4da6aef484f86142cc147`. It did not include the new privileged Windows captures or biometric data. The [unedited OpenCode response](opencode-front-plan-answer.raw.md) is advisory and contains the errors corrected below; it is not an approved patch specification.

## What was implemented

Commit `e6fa5cb158b56330b41dda6eb299041b3a3d39f3` adds a three-line underflow guard to `float_to_u32_mul_1000000()` in `drivers/media/i2c/ccs/ccs-reg-access.c`. Independent source inspection found that very small nonzero positive values could shift a 64-bit integer by 64–127 bits. The reference's reported raw limit value `0x044f3000` is one such input when interpreted by the floating-point converter; the patch does not establish that the physical sensor emitted that value in this capture session.

The guard returns zero when the unbiased exponent is at most -64. Such values are below one after scaling by one million. Existing zero, special-value, saturation and other defined conversion paths remain unchanged. No sensor identities, limit replacements, firmware, C-PHY tables, DT nodes or emitter controls were imported.

An exact-function native ARM64 UBSan harness reproduced invalid shifts of 119, 127 and 64 before the change. After it, 25 explicit cases, 1,536 exponent/sign/mantissa combinations and one million deterministic patterns passed; 875,617 sampled previously defined results matched and 125,919 tiny inputs returned zero. Baseline and patched CCS object/composite builds succeeded with no compiler warnings; the existing CAMSS regression target also succeeded and its object hashes were unchanged. These are arithmetic and build checks, not Linux camera or authentication tests. See [validation and test artifacts](ccs-followup-validation/validation.json).

The implemented arithmetic guard was selected and tested independently after the OpenCode architectural review. OpenCode's proposed generic C-PHY and sensor-quirk changes were not applied.

## Corrections to the OpenCode response

| Advisory claim | Source-checked correction |
| --- | --- |
| Adding a C-PHY union member automatically makes the existing provider reject C-PHY configuration. | `drivers/phy/qualcomm/phy-qcom-mipi-csi2-core.c:94` always reads `opts->mipi_dphy`. `phy_configure()` dispatches directly to that callback; it does not call `phy_validate()`. An explicit mode check and correct options dispatch must precede any new C-PHY consumer. No union type-punning is acceptable. |
| `__csid_configure_rx()` can return `-EINVAL` to reject unsupported modes. | This callback in `camss-csid-680.c:186`, and its stream-configuration callback chain, currently return `void`. Reject in an appropriate integer-returning validation/link/start path before relevant side effects, or explicitly redesign the callback chain and its unwind. |
| A new module identity can be inferred from `ccs-sensor-4260-0681-0010.fw`. | `ccs_module_idents` matches module manufacturer/model/revision. The sensor firmware filename is formed from separate sensor identity fields. A filename is not evidence for a module-table match. Read the actual module and silicon identities before choosing a model-specific guard. |
| The proposed “broken limit” replacements can be imported now. | The reference YAML mixes reported limit repairs with deliberate PLL-calculator workarounds. Its chosen limits and PLL configuration have not been independently validated here. No replacement firmware or quirk stub was imported. The generic shift guard does not justify any of those replacements. |
| A rejected C-PHY start would have zero MMIO or PM side effects throughout the pipeline. | A local error before `phy_power_on()` is not proof that earlier pipeline stages never powered hardware. Define the rejection point and verify the actual unwind. Do not claim pipeline-wide absence of side effects from a compile check. |
| ETW/WinRT necessarily contains no CCI or MMIO write content. | Existing supplied metadata does not establish decoded CCI/MMIO transactions. Standard metadata does not guarantee them; a driver-specific provider could include relevant records. Inspect the new traces before drawing a stronger conclusion. |
| The driver requires a default-off privacy GPIO. | The indicator's actual controller, interface and polarity are not established. Do not invent a GPIO. Separate camera-frame delivery, indicator/emitter safety, and userspace authentication requirements. Windows Hello success would not validate Linux authentication or camera integration. |
| Generic PHY changes can be accepted with enum/union-size assertions and no second consumer. | The supplied excerpts do not establish every consumer or an appropriate generic API. Inspect the whole tree and review units, lifecycle and provider contracts. Hard-coding structure sizes is not meaningful behavioral coverage. |
| Negative hardware testing should inject wrong trio settings. | Exercise invalid mappings in software validation first. Do not program unverified receiver or emitter settings merely to manufacture a failure. |

One distinction matters: `phy_validate()` without a provider callback returns `-EOPNOTSUPP` in this tree. That does **not** make `phy_configure()` reject a new options type. `phy_set_mode_ext()` records the mode only on success, including success when the optional `set_mode` callback is absent.

## Next patch split, not yet implemented

1. **Check existing PHY setup errors.** In `camss-csiphy.c:324`, check the return values of `phy_mipi_dphy_get_default_config_for_hsclk()` and `phy_set_mode()` before configuration or power-on. This is a small error-contract improvement, not evidence that present valid D-PHY streams fail. Test each injected failure, the unchanged successful call sequence and the absence of subsequent configure/power calls. Verify ordinary D-PHY behavior and build CAMSS.

2. **Design explicit per-instance C-PHY options and rejection.** Review `include/linux/phy/phy.h`, the generic PHY helpers and all affected consumers before selecting a symbol-rate/trio/line-order API. The Qualcomm provider needs explicit mode validation before reading a union member, with unsupported modes rejected. Keep D-PHY units and behavior unchanged. Do not add an options type plus a consumer while the provider still interprets it as D-PHY.

3. **Carry endpoint type through the actual route.** Relevant paths are `camss.c` endpoint parsing/notifier binding/link-frequency calculation; `camss-csiphy.h` and `camss-csiphy.c`; and `camss-csid.h`, `camss-csid.c` and `camss-csid-680.c`. Preserve the D-PHY clock-lane/polarity layout, and handle C-PHY trio/line-order semantics separately. Include test-pattern-generator state and non-680 receivers in validation. The existing `2 * lanes` link-frequency fallback and PHY clock-margin calculation are D-PHY assumptions, not C-PHY formulas.

4. **Add verified C-PHY backend behavior.** Only after rate, mapping and lifecycle decisions are established, implement the per-instance Qualcomm PHY path and the relevant CSID680 mode setting. Audit reference tables with provenance; do not import global `force_cphy`, forced clocks or register overrides. Test invalid configurations without hardware writes, D-PHY regressions, configure/power error unwinds, repeated starts and mixed C-PHY/D-PHY routes. Actual sensor operation remains a separate hardware gate.

5. **Add sensor-specific CCS support and board wiring from evidence.** Module and sensor identities must select the correct scope. Limit/PLL/geometry/control fixes require independent evidence and non-IMX681 regression tests. The existing `mipi-ccs.yaml` and `qcom,x1e80100-camss.yaml` accept C-PHY; `video-interfaces.yaml` already defines `line-orders`. Reconcile required Qualcomm PHY supplies and static `phy-type` with the provider's endpoint mode. Do not fill missing board values with guesses.

The first item is an independently reviewable candidate. Items 2–5 are a dependency plan, not authorization to merge a partially implemented C-PHY interface. No additional code from this list has been applied.

## Evidence gates across all cameras

| Camera or feature | Still needed before enabling or claiming parity |
| --- | --- |
| Front IMX681 | Actual module/sensor IDs, CCI route/address, powered PHY/trio/line order, rate and clock units, rail/reset/MCLK sequencing and shutdown, stream register sequence, raw geometry and buffer layout, exposure/gain and indicator behavior. RAW10 at width 3844 has 4805 payload bytes while the current aligned buffer stride is 4816; a successful build does not resolve that mismatch. |
| Rear OV13858 | CCI identity, power/reset/clock and shutdown evidence, lane mapping/rate, mode register tables, VCM and EEPROM associations, controls and capture quality. `ov13858.c` exists and requires 19.2 MHz; it currently has an ACPI-oriented probe rather than a ready Denali OF integration. |
| IR VD55G0 | Correct sensor protocol/firmware and raw modes, trigger/synchronization, safe illumination/indicator control and timing, and observed capture behavior. The VD55G1 driver is a different device and must not be aliased. Advertised Windows NV12 geometry is not proof of the raw sensor mode. |
| Hello/authentication | An attributed Windows Hello observation is a Windows result. Linux frame delivery, indicator/illuminator safety, userspace authentication and liveness/security need separate implementations and validation. No biometric material was sent to OpenCode. |

Receiver addresses in the Linux DTS describe static resources. They do not prove a Windows route is active, powered or safe to access. Any debugger work needs independently validated address provenance and its own safety gates.

## Review provenance

OpenCode 1.18.23 ran with the `plan` agent, `--pure`, all tools denied, MCP disabled and no plugins. The existing provider/model was reused without modifying global configuration or authentication. It completed with exit 0, zero tool events and no stderr. The [metadata](opencode-front-plan-metadata.json) records exact answer/prompt hashes and the reviewed pins. The raw answer remains byte-for-byte unchanged so its limitations remain auditable.
