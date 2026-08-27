# Sanitized privileged SP11 camera findings for OpenCode review

Date: 2026-08-27. This is a reviewed technical projection, not raw ETL/XML, device/account data, biometric templates or a hardware-enablement instruction. Use only this evidence and the separately supplied exact current kernel source packet. Do not run tools or fetch anything.

## Review request

Assess what the completed Windows observations change about integration readiness for front IMX681, rear OV13858 and IR VD55G0 on the existing branch. Distinguish measured evidence, operator reports, implementation requirements and unknowns. Give a concise acceptance checklist and at most one narrowly justified software-only next candidate supported by the supplied code. A proposed patch is advisory, not authorization to apply it. Do not guess register writes, physical mappings, sensor/module IDs, illumination controls, credential/gesture enum meanings, authentication configuration or security parity.

## Pinned kernel and reference

- Branch: `sp11/integration-7.2.x-ooaklee-karsies-wq-cams`.
- Base: Linux 7.2.0, `3fc7c5249f4aa9ef70d1f63e82201c8a21b33827`.
- Current code HEAD: `e6fa5cb158b56330b41dda6eb299041b3a3d39f3`.
- Reference source: Karsies `b08f76f40b8d7b715bd4da6aef484f86142cc147`.
- Earlier branch changes remove four unconfigured CSID680 crop/drop enables and select VFE680 MIPI_RAW mode with a named bit. They do not change sensor nodes, PHY, image geometry/stride or emitters.
- The newest commit adds only a three-line CCS exponent <= -64 underflow guard, avoiding undefined oversized shifts. Baseline UBSan failures reproduced for three inputs; committed exact-function tests passed 25 explicit cases, 1,536 exponent/sign/mantissa cases and one million deterministic patterns. Native ARM64 CCS/CAMSS builds and strict commit checkpatch passed. No full kernel image or Linux hardware run; inherited broader project CI is not clean.
- Original Mac checkout and its 13 pre-existing modified files are preserved. Documentation is staged separately; no push or deployment.

## Completed privileged captures

Windows build 29585, ARM64 target, Windows PowerShell 5.1 collector, administrator token. All source capture files remain unchanged. Independent verification matched every length/hash: 186 RGB artifacts and 155 fresh Hello artifacts. No separate image/audio samples were saved. RGB frames were discarded after obtaining references.

Twenty-two selected RGB source formats delivered frames for at least five seconds with matching negotiated formats: front seven recording modes; rear seven preview and eight recording modes. Polling counts 76–146 are lower bounds, not measured lossless FPS, useful pixels or quality. This is not all advertised video profiles, stills, control-operation tests, AF, HDR, concurrency or resume coverage.

| Completed ETL | Native events processed | Native events lost | Header buffers lost | Requested providers emitting |
| --- | ---: | ---: | --- | --- |
| Front provider | 129,782 | 0 | 0 | 7/9 |
| Front WPR Video | 5,445,172 | 0 | unknown: summary only | WPR profile |
| Rear provider | 52,818 | 0 | 0 | 6/9 |
| Rear WPR Video | 6,963,080 | 0 | unknown: summary only | WPR profile |
| Fresh Hello provider | 3,037 | 0 | 0 | 8/11 |

WPR used circular Memory mode for RGB; provider ETW used a 128 MiB circular cap. WPR save/merge still uses disk and is not constrained by that cap. Zero reported loss does not establish complete circular history, continuous coverage or correct frames.

All three provider ETLs were strictly decoded without best-effort `-lr`; native processed counts equal independently counted XML elements and parsed events. Each has zero reported camera-provider schema errors and one `ProcessingErrorData` for classic infrastructure event0/opcode80. Get-WinEvent exposes one fewer infrastructure record, while all emitting-provider counts agree. This difference is not evidence of camera-event loss.

`tracerpt` serialized some timestamps with offset `+00:59`. For Hello, all eight provider bounds are 60 seconds later in apparent XML UTC than independent Get-WinEvent ETL metadata. RGB mixes `+00:59` and `+01:00`, so first-bound differences vary. Original offsets are retained; no global shift is applied. Use independent ETL metadata for correlation and keep continuity unknown.

No validated CCI/register transaction has been recovered. The selected decoded provider field names contained no register-transaction lead. WPR payload/schema/register coverage was not decoded. These facts do NOT show that all proprietary tracing lacks register information. Bus/sensor identity, address, width, value, direction, timestamp and completion remain necessary for transaction attribution.

## One fresh Windows Hello observation

The user had set up face unlock, then explicitly requested a fresh observation. The operator agreed to a workstation lock and reported `face-success`. The lock API returned success; there is no independent completed-lock/unlock audit from that return.

Authentication marker window: 12:57:11.2582075–12:58:06.9594963 UTC. That captured collector ended its window after optional observation answers; do not treat the entire interval as authentication latency.

The user confirmed estimates of VCSEL 5 seconds and privacy indicator 2 seconds. No timing reference point/overlap, emitter identity, duty cycle, power or eye safety was measured. There was no direct illumination control, IR frame saving or enrollment modification by the collector.

Saved EVTX evidence within the window:

- Biometrics1019: privileged vendor operation completed, status0x0, face virtual sensor name; not an explicit biometric match.
- Biometrics1605 and1606: secure-component authorization and redemption; no sensor/modality field.
- HelloForBusiness5702: protector properties written, Hr0x0; not a sign-in result.
- No explicit Biometrics1001/1004 match success, HelloForBusiness5001 sign-in success or enrollment event in the reviewed saved window. This does not prove matching did not happen.
- An exact-window Security4800/4801 read was denied to the later non-admin audit token; independent completed unlock is unknown.

The provider ETL agrees on three Biometrics events and one HelloForBusiness event, plus 690 FrameServer events. Independent ETL bounds: FrameServer12:57:18.8754066–12:57:20.4501493; Biometrics12:57:19.3646104–12:57:20.1224944 UTC.

Conclusion: operator-reported face success with correlated camera/biometric activity, not independently verified face matching or security parity. Do not equate this with a Linux authentication implementation.

## Collector and inventory follow-up

The saved collector falsely reported zero Hello counts due to a localized text-header regex and text timestamps rendered one hour ahead with a misleading Z. Immutable EVTX query proves three plus one. A separate structured-XML parser correction is being tested; it is not retroactively claimed as the captured revision. It uses actual Operational channels, exact UTC bounds, bounded queries, explicit truncation, unknown counts on errors, and an operator-result timestamp before optional observations.

Two registry exports failed at 268-/286-character destination paths. The exact keys exported successfully to new shorter private filenames under a non-admin token, with valid headers and hashes. No registry values changed; original captures retain partial-inventory. A compact-filename correction is being tested separately.

An earlier Hello controller exited before finalization. Its one owned ETW session was explicitly stopped and confirmed absent before the fresh retry. Its recovered trace is kept separate, with authentication result unknown; it is not a sixth successful capture. All completed-run owned recorder stops succeeded.

## Integration gates and corrections to earlier advice

The installed camera identities/static resources are leads, not silicon readback or measured power timing. Public front-owner evidence reports C-PHY2/trio0, RAW10 3840x2640 and LINE_LENGTH_PCK7552->8704; do not reinterpret that as frame length or universal topology.

- Base CAMSS rejects C-PHY endpoints. The relevant PHY options are D-PHY-only; the existing Qualcomm configure callback reads opts.mipi_dphy. Merely adding a C-PHY union arm does not make that callback reject it. CAMSS does not call phy_validate before configure; absence of a PHY validate callback returns -EOPNOTSUPP. Check actual return handling and call chains in the supplied source.
- CCS module-identification IDs are distinct from names such as ccs-sensor-4260-0681-0010.fw. Do not infer an IMX681 quirk match from that filename.
- The existing OV13858 driver is ACPI-centric. DT nodes alone do not implement the verified ARM power/VCM path. VD55G1 is not a substitute for VD55G0.
- RAW10 width3844 needs4805 payload bytes versus4816 aligned stride; width3840 is4800. Actual full/lite receiver buffer layout and errors need hardware tests, not a global forced crop.
- Boot debug is enabled and ARM64 WinDbg is installed. A separate Windows debugger host/transport/live connection, matching loaded code, CCI call semantics, translated resources and powered mappings are unverified. Twelve exact tested public PDB URL variants returned404, not proof of global symbol absence.
- No sensor nodes, C-PHY enablement, forced clocks/PLL bypasses, guessed privacy GPIO or VCSEL control were added. No Linux PAM/face database/authentication bypass was installed.
- Full parity needs per-camera Linux probe/capture, modes/stills/controls/quality, switching/concurrency/power/resume, safe illumination ownership and separate authentication/spoof-resistance validation. Windows frame or authorization activity alone cannot satisfy these gates.
