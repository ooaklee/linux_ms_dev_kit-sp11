# SP11 camera review

## Net assessment

The Windows captures **do not change Linux integration readiness materially.** They add plausibility that the installed modules deliver advertised formats on this platform (Windows), and they corroborate operator-reported activity — but none of it exercises the Linux CCI/PHY/CSID/DT path, and no register-level lesson was recovered.

- **Measured (Windows, not Linux):** 186 RGB + 155 Hello artifacts hash-verified; 22 negotiated formats streamed ≥5 s (front 7 record, rear 7 preview + 8 record) with matching negotiated formats; cleanly decoded ETLs (zero native/header loss *as processed*; counts 76–146 are lower bounds, not FPS; zero loss ≠ complete circular history). No validated CCI/register transaction was recovered; the WPR payload/schema/register coverage was never decoded. The `+00:59` tracerpt offset means: correlate only with independent ETL metadata (`camss-csiphy.c` untouched by any of this).
- **Operator reports:** `face-success`, VCSEL ≈5 s, privacy indicator ≈2 s — no timing reference, emitter identity, duty cycle, power or eye safety measured; no independent lock/unlock audit (Security4800/4801 read was denied, and 12:57:11.26–12:58:06.96 UTC is not authentication latency). Saved events are a vendor-op completion (1019), secure-component auth/redemption (1605/1606, no modality field) and protector-properties write (5702) — **not** a match/sign-in/enrollment record. Correlated camera/biometric activity, not verified matching or security parity.
- **Implementation requirements (unchanged):** full parity gates stand — per-camera Linux probe/capture, modes/stills/controls/quality, switching/concurrency/power/resume, safe illumination ownership, separate authentication/spoof-resistance validation.

## Code-grounded points (supplied source)

- **C-PHY is out, and the code confirms it:** `camss_parse_endpoint_node` rejects non-D-PHY at camss.c:4733-4736; `csiphy_stream_on` hardcodes `PHY_MODE_MIPI_DPHY` with `dphy_cfg` at camss-csiphy.c:335/345; `phy_qcom_mipi_csi2_configure` only reads/validates `opts->mipi_dphy` (phy-qcom-mipi-csi2-core.c:98-105); the `2 * lanes` divisor at camss.c:4611 is D-PHY-specific. phy-core.c:563/598 confirm `phy_configure` → `-EOPNOTSUPP` without a configure op and that CAMSS never calls `phy_validate` — a C-PHY union arm alone changes nothing. The front C-PHY2/trio0/7552→8704 report is a public/operator lead, not silicon readback; leave it.
- **CCS:** the ident table at ccs-core.c:50-62 has no 0x4260/0x0681 entry; `ccs-sensor-4260-0681-0010.fw` is just the generic firmware-name format (ccs-core.c:3252-3258), not a quirk/identity match. No ident entry without readback.
- **Committed guard is correct and complete:** ccs-reg-access.c:52 `exp <= -64 → 0` removes the u64 shift-by-≥64/negative UB for the smallest/denormal exponents; consistent with the 25 + 1,536 + 1M test results. Nothing further to fix there.
- **RAW10 arithmetic checks out:** width3844 → ceil(3844·10/8) = **4805** payload vs 4816-aligned stride (width3840 → 4800). Receiver layout/error behavior needs hardware capture, not a global forced crop.
- **OV13858 / VD55G0:** ACPI-centric driver, DT-only nodes don't implement the verified ARM power/VCM path; VD55G1 ≠ VD55G0. Identity questions, not guesses.

## Acceptance checklist

1. Per-camera Linux probe with identity from readback; modes enumerated; no forced clocks/PLL bypasses.
2. Linux capture of all advertised preview/record modes with negotiated formats matched (headers; RAW10 4805-vs-4816 proven by buffer/error behavior, not forced crop).
3. Stills/controls (AF, exposure/gain, HDR); OV13858 verified power/VCM path — not DT-only.
4. Switching/concurrency/power/resume; measured VCSEL/privacy timing, not operator estimates.
5. CCI transaction attribution (bus/sensor, addr, width, value, dir, timestamp, completion) before any register-level change.
6. Independent Linux match + spoof-resistance validation. Windows Hello marker window is not an unlock audit.

## Next candidate (advisory, not to apply)

My recommendation is **no production kernel change** — the evidence justifies verification, not code. The single in-scope software-only candidate consistent with the supplied code is a **test-only regression pinning the `float_to_u32_mul_1000000` boundaries** (exp == −64, denormal/sub-MHz zero-rounding, and the >0x4f800000 path) in the exact-function harness that the committed change used — contingent on that harness existing in-tree, which I cannot confirm from the supplied excerpts. Everything else (C-PHY, CCS ident/quirk entry, DT/PHY nodes, illumination) is gated behind the checklist above.

I've made no changes (plan mode). If you want, outside plan mode I can draft that advisory regression-test patch or finalize this as review documentation.