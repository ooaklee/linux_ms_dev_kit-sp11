# Privileged camera and Windows Hello follow-up — 2026-08-27

Capture evidence, final collector regressions and the OpenCode advisory review are complete within the scopes below. This follow-up is separate from the earlier unprivileged capture and does not rewrite its results or manifests.

## What completed

- **22 of 22 RGB source-mode checks delivered frames** with the requested format for at least five seconds: seven front recording, seven rear preview and eight rear recording modes. The polling counts range from 76 to 146 frame references; they are lower bounds, not measured sensor FPS or image-quality results.
- The completed elevated runs saved **five ETLs**: separate front/rear WPR Video and provider traces, and one provider-only Windows Hello trace. All **186 RGB artifacts and 155 Hello artifacts** match their recorded lengths and SHA-256 hashes. No unlisted files or separate image/audio samples were found.
- The fresh Hello attempt recorded **operator-reported face success** after the user set up face unlock. The user subsequently confirmed the observations as estimates of **5 seconds for the VCSEL and 2 seconds for the privacy indicator**. Whether these describe duration or delay, their reference points and their overlap remain unspecified; these are not emitter-control or safety measurements.
- Both completed captures retain `partial-inventory`: two registry exports for a software camera component and its Device Parameters failed. Their capture errors are null and their owned recorders stopped successfully. A separate follow-up recovered both exports at shorter private paths; it does not change the historical capture status.

**This does not establish Linux camera support, independent biometric verification, or full Windows parity.** No CCI transaction sequence, active CAMSS/PHY register dump, sensor identity readback, Linux camera capture, or Linux authentication implementation has been verified by these observations.

## Capture provenance and cleanup

| Capture | Scope | Manifest entries | Manifest SHA-256 |
| --- | --- | ---: | --- |
| `20260827-133214832-camera-trace-privileged-rgb` | Elevated; RGB `AllModes`; five seconds per mode; WPR Memory plus circular provider ETW | 186 | `976E6D1D3C80402420413D86C68968ED8DF54DB0492A9F93284181569EAB43E3` |
| `20260827-135431422-camera-trace-privileged-hello-retry` | Elevated; one interactive Hello observation; provider ETW only, WPR skipped | 155 | `68A5610A7902709FAE059263CB254260D9A005A74337AC60B255FB74485B4D2F` |

The source-mode results are in [windows-privileged-rgb-stream-results.csv](windows-privileged-rgb-stream-results.csv). [privileged-evidence-verification.json](privileged-evidence-verification.json) records independent complete-manifest verification and exact ETL sizes/hashes. Raw traces, registry exports, driver/package paths and biometric payloads remain private in `output/`.

The initial combined suite stopped at its free-space guard before starting Hello. WPR Memory was selected because of limited disk space; saving and merging its circular buffers still produced large files. The 128 MiB provider cap does not cap WPR Video. Circular retention and reported event loss must be assessed separately.

A first, separate Hello controller exited before finalization. Its authentication result remains unknown and it has no final summary or manifest. Before the fresh retry, the elevated wrapper verified that the old controller had exited and stopped only its exact recorded ETW session. The stop returned zero and a successful subsequent session query confirmed its absence. Its recovered 9,371,648-byte ETL is preserved separately and is not counted among the five completed-run ETLs. No summary was fabricated for it.

The successful RGB and fresh Hello runs have successful native stop records and no remaining-owned-recorder flags. Watchdogs handle ordinary stalls; forcibly closing the controller can also terminate its PowerShell jobs. The interrupted run demonstrates why exact-session recovery remains necessary after host termination.

### Registry export follow-up

Both failed exports reported an inability to write the output file. Their destination paths were 268 and 286 characters long, whereas shorter destinations succeeded, including one of 259 characters. Retrying the exact two keys at new 104- and 115-character paths succeeded with a non-admin token: exit zero, valid registry-export headers, 3,088 and 960 bytes. This supports native destination-path length as the cause, not an access denial. No registry value was changed. The original captures remain immutable; [registry-export-followup.json](registry-export-followup.json) records the separate exports and hashes. The collector now uses compact ordinal filenames, retains full device identities in its private registry index, and explicitly rejects overlong native destinations before querying or exporting the key.

## Windows Hello observation

The authentication marker window was **12:57:11.2582075–12:58:06.9594963 UTC**. The lock API returned success at 12:57:11.6040342 UTC; that return is not an independent completed-lock or unlock audit. This captured revision placed the end marker after optional observation responses, so the window includes operator response time.

The immutable saved event logs contain the following records in that window:

| Provider / event | UTC | Supported interpretation |
| --- | --- | --- |
| Biometrics 1019 | 12:57:19.3646085 | Privileged vendor operation completed with status `0x0`; the sensor name identifies a face virtual sensor. This is not an explicit biometric match. |
| Biometrics 1605 | 12:57:20.1067138 | Secure-component user authorization; no sensor/modality field. |
| Biometrics 1606 | 12:57:20.1224936 | Authorization redemption; no sensor/modality field. |
| HelloForBusiness 5702 | 12:57:20.1361446 | Protector properties written, `Hr=0x0`; not a sign-in result. |

No explicit Biometrics verification/identification success (1001/1004), HelloForBusiness sign-in success (5001), or enrollment event appears in the reviewed saved window. Absence of those records does not show that matching did not occur. A separate read-only, exact-window Security 4800/4801 query was denied to the current non-admin token; independent completed-unlock evidence is unknown.

The proper result remains **operator-reported face success with correlated camera/biometric activity**. Event presence, a successful vendor operation and authorization redemption do not independently identify the authentication method or establish its security properties. No numerical credential/gesture enumeration was guessed to distinguish face from PIN.

[windows-hello-observation.json](windows-hello-observation.json) preserves the sanitized evidence, query statuses, timing limitations and the later confirmation of seconds. It does not contain accounts, sensor paths, database identifiers, face templates or image data.

### Event counter defect and correction

The captured collector's text parser reported zero Biometrics and HelloForBusiness events incorrectly. The saved `wevtutil /f:text` headers lacked the colon expected by the regex, and its rendered timestamps used local time with a misleading `Z` suffix, one hour ahead of the saved XML UTC timestamps. The independently queried EVTX files contain three and one events respectively. Neither defect is evidence of absent biometric activity.

The corrected collector queries discovered Operational channels as XML with exact UTC bounds, a 20-second per-channel timeout, at most 128 retained records and one lookahead record to detect truncation. Invalid XML, missing fields, query failures and timeouts remain unknown instead of becoming zero counts. Its operator-result timestamp ends the query before optional observation answers. The prompts now request units and a timing reference point.

Independent replay of the saved XML under both Windows PowerShell 5.1 and PowerShell 7.6.4 recovered the correct **3/1 event counts**, IDs and exact UTC timestamps without changing the inputs. Original captures, source hashes and text outputs remain unchanged; the final collector was not used for another live Hello attempt.

### Final Windows validation

**30 tracing tests, 49 static resource-decoder checks and 10 ETL audit/projection tests passed in each PowerShell version.** The tracing tests include bounded XML queries, failure/unknown semantics, truncation, the earlier query end, compact registry mappings, native path-length boundaries and recorder cleanup. The recorder ownership fix retains the exact owned session for cleanup even if native start succeeds but writing its log throws.

The ETL projection preserves ISO 8601 UTC with seven fractional digits across PowerShell's JSON type conversions and locales. All 42 time-bound cells for the 21 emitting requested providers independently match the saved audit metadata. [windows-privileged-validation.json](windows-privileged-validation.json) records current source hashes, the older revisions actually used for capture, all eight final test/replay invocations, exact source/recording cleanup checks and the separate validation manifest.

## ETL interpretation

All five ETLs matched their capture manifests before and after read-only analysis. The separate sealed RGB audit has 16 verified payloads; the Hello audit has seven.

| ETL | Native events processed | Reported events lost | Header buffer loss | Requested providers emitting |
| --- | ---: | ---: | --- | --- |
| Front provider | 129,782 | 0 | 0 | 7/9 |
| Front WPR Video | 5,445,172 | 0 | unknown: summary only | WPR profile |
| Rear provider | 52,818 | 0 | 0 | 6/9 |
| Rear WPR Video | 6,963,080 | 0 | unknown: summary only | WPR profile |
| Hello provider | 3,037 | 0 | 0 | 8/11 |

All three provider XML event totals match both their native processed counts and independent element counts. Each has one classic infrastructure schema error and zero reported camera-provider schema errors. WPR payload/schema/register coverage was not decoded. Zero reported event loss does not establish full circular history, lossless camera frames or sensor-register coverage.

The saved Hello provider trace contains **3,037 native processed events**, matching both the independent XML element count and parser count. Its decoded header reports **EventsLost=0 and BuffersLost=0**. Eight of eleven requested providers emitted events, including three Biometrics, one HelloForBusiness and 690 FrameServer events.

The audit remains partial: one classic trace-metadata record, GUID `9e814aad-3204-11d2-9a82-006008a86939`, event 0/opcode 80, has `ProcessingErrorData`. Also, this build's `tracerpt` serialized local timestamps with offset `+00:59`; interpreting that supplied offset yields apparent UTC bounds 60 seconds later than independent `Get-WinEvent` ETL metadata. Both are retained and compared without silently changing timestamps. All eight emitting provider counts match between the readers; their differing classic infrastructure totals are not camera-event loss.

Independent ETL metadata places FrameServer activity at **12:57:18.8754066–12:57:20.4501493 UTC** and Biometrics activity at **12:57:19.3646104–12:57:20.1224944 UTC**, within the operator window. These ranges show retained event activity, not illumination duration, continuous phase coverage, or measured authentication latency.

RGB XML mixes `+00:59` and `+01:00` offsets, so its first-bound discrepancies vary; no uniform time shift is valid for those ranges. The [ETL summary](etl-summary.sanitized.json), [requested-provider table](etl-providers.sanitized.csv) and [audit verification](etl-projection-verification.json) retain the distinct reader counts, reported losses and time comparisons. Full phase continuity remains unproven.

[etl-validation-workflow.md](etl-validation-workflow.md) documents strict file-based decoding, bounded native decoder processes and metadata privacy. No register-transaction field-name lead was found in the three decoded provider traces. That does not establish the contents of untraced, opaque or proprietary instrumentation, nor the undecoded WPR payloads. No attributable CCI transaction sequence has been validated.

## Kernel changes and OpenCode

The requested branch remains `sp11/integration-7.2.x-ooaklee-karsies-wq-cams`, based on Linux 7.2.0 commit `3fc7c5249f4aa9ef70d1f63e82201c8a21b33827`, in an isolated Mac worktree. The original checkout's 13 pre-existing modified files are preserved.

In addition to the previously recorded CSID-680 and VFE-680 receiver changes, commit **`e6fa5cb158b56330b41dda6eb299041b3a3d39f3`** adds a three-line CCS floating-point conversion guard. Exponents at or below -64 now return zero before an oversized 64-bit shift. This is a generic arithmetic correction; it does not identify an IMX681 module or change firmware selection, PLL limits, sensor nodes, C-PHY, power sequencing or emitters.

The original function reproduced undefined-shift failures for three explicit inputs. The committed function passed UBSan checks comprising 25 explicit cases, 1,536 sign/exponent/mantissa cases and one million deterministic patterns. Native ARM64 CCS and CAMSS object/composite builds passed; the unchanged CAMSS objects retained their hashes. Strict commit checkpatch with the sign-off requirement excluded and the code diff whitespace check passed. [The validation bundle](ccs-followup-validation/validation.json) records sources, configuration, logs and object hashes; all 24 portable payloads and 42 private payloads were independently hash-verified.

OpenCode's earlier [front-port review](opencode-front-plan-review.md) used pinned sources and sanitized prior evidence. A separate final review received only the [approved privileged findings](opencode-privileged-review-input.md) and seven exact current kernel source excerpts. It completed once in 86.727 seconds with zero tool calls, no stderr and unchanged global configuration/authentication files. Raw ETLs, EVTX/XML, biometric payloads, Windows binaries, credentials and the CSV projection were not review inputs.

The [reviewed assessment](opencode-privileged-review-assessment.md) corrects the raw answer's broad loss-counter wording and numerical claims against the actual evidence/code. Its proposed boundary test duplicates the existing harness; no additional patch was applied from that recommendation. Low-level sensor/PHY and authentication gates remain open.

These are scoped builds, not a complete kernel-image build, Linux hardware validation or clean broad project CI. The inherited broad-CI diagnostics in the earlier report remain unresolved. No branch push or device deployment has occurred.

## Remaining integration gates

| Work item | Next evidence or implementation needed |
| --- | --- |
| Front, [issue 43](https://github.com/ooaklee/linux-surface-pro-11-oe/issues/43) | Per-instance C-PHY API/driver support, validated sensor identity/power/modes, receiver layout and error checks; do not import global C-PHY or permissive PLL overrides. |
| Rear, [issue 41](https://github.com/ooaklee/linux-surface-pro-11-oe/issues/41) | Actual OV13858 CCI sequences and power/VCM transactions; adapt the existing ACPI-centric driver for the verified ARM topology. |
| IR, [issue 42](https://github.com/ooaklee/linux-surface-pro-11-oe/issues/42) | VD55G0-specific identity, sensor writes, mode/timing evidence and reviewed illumination controls. VD55G1 and Windows output geometry are not substitutes. |
| Privacy and authentication | Identify the real indicator/emitter ownership and fail-safe behavior; validate Linux capture and authentication separately, including protected matching and spoof resistance. No Linux PAM, face database or authentication bypass was installed. |
| All-camera acceptance | Profile-specific modes/stills, controls/AF, image quality, camera switching, supported concurrency, power/suspend recovery and repeated Linux capture tests. |

Elevated read-only checks now confirm boot `debug Yes`, installed ARM64 WinDbg tools and exact driver/PDB identities. The twelve tested public PDB URL variants returned 404; this does not prove symbols are unavailable everywhere. A **separate Windows debugger host, supported transport and live connection** are still unverified. Exact loaded code, CCI call semantics, translated resources, live mappings and powered register access must be established before collecting transaction/MMIO dumps. [debugger-prerequisites-followup.md](debugger-prerequisites-followup.md) records those gates.

No boot/security setting was changed. No guessed breakpoint, physical MMIO address or illuminator command was executed. This branch records verified increments and explicit missing evidence; it is not a full-parity release.
