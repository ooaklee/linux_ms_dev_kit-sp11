.. SPDX-License-Identifier: GPL-2.0

Surface Pro 11 camera integration evidence
==========================================

This bundle records the camera investigation of 27 August 2026 for the
experimental ``sp11/integration-7.2.x-ooaklee-karsies-wq-cams`` branch.
Two receiver changes disable unconfigured CSID680 RDI crop/drop enables and
select the VFE680 RDI MIPI RAW write mode. A later CCS arithmetic guard
prevents oversized shifts for tiny floating-point limits. No sensor is
enabled by this series. Linux capture, infrared operation, authentication,
and full camera feature parity remain unvalidated. Sensor power, per-endpoint PHY configuration,
buffer layout, privacy indicators, and infrared illumination still need
evidence and hardware tests before camera enablement.

Start with the :download:`privileged camera and Windows Hello follow-up
<sp11-camera-integration/privileged-camera-followup.md>`, the earlier
:download:`integration report
<sp11-camera-integration/camera-integration-report.md>`, and the
:download:`source audit <sp11-camera-integration/source-audit.md>`.
The reports distinguish advertised capabilities, observed Windows stream
checks, operator observations, static resources, and unverified Linux behavior.
Source pins and existing author attributions are preserved in the audit and
patch series.

Initial verification
--------------------

* Windows PowerShell 5.1 and PowerShell 7.6.4 each passed 19 tracing tests and
  49 resource-decoder checks. The selected RGB matrix passed 22 checks; three
  final smoke checks covered the subsequent safety-classifier correction.
  These checks did not read or save image pixels, open the infrared camera,
  or exercise biometric authentication.
* Both changed CAMSS objects and the composite ``qcom-camss.o`` built for
  AArch64 using the recorded configuration. Baseline and patched compiler
  logs contain no warnings. This was a focused build, not a full kernel
  image, allmodconfig, or Linux hardware validation.
* The two code commits passed whitespace checks and strict checkpatch with
  the sign-off requirement explicitly excluded; no sign-off was invented.
  The broader repository check reported 11 errors and 67 warnings at both
  the recorded base and two-code-commit tip, including three project-authored
  errors. It reported 495 CHECK records at the base and 497 at the code tip.
  The two additional CamelCase records name ``SE_GENI_TX_FIFOn`` and
  ``SE_GENI_RX_FIFOn`` in ``drivers/spi/spi-geni-qcom.c``; that file's Git
  blob is identical at both revisions. There are no added CAMSS CHECK
  records. The reason for the different SPI reporting was not established.
  The script's broken-pipe handling can return zero despite errors, so its
  exit status does not establish clean CI.
* The preserved diff and mail artifacts retain blank context lines, context
  prefixes before tabs, mail separators, and an ending blank line. A whole
  documentation whitespace check reports those artifact lines. Authored
  documentation and the other evidence files are checked separately without
  rewriting the original patch bytes or their hashes. Windows evidence
  also retains CRLF line endings; that check recognizes CR at end of line
  using a command-local Git whitespace setting.
  The CCS tool-version snapshot also retains its recorded ending blank
  line; that snapshot and the exact patch artifacts are excluded from
  the separate authored-document whitespace check.

Privileged Windows follow-up
----------------------------

Separate elevated runs delivered frames in all 22 selected RGB source-mode
checks and saved five completed-run ETLs. All 186 RGB and 155 Hello capture
artifacts match their recorded lengths and hashes. Both captures retain
partial-inventory status because two registry exports failed at long output
paths. Separate shorter-path exports succeeded without changing the original
captures or registry values.

The fresh Hello result is operator-reported face success with correlated
camera and biometric activity. Saved events 1019, 1605, 1606 and 5702 do not
independently establish a match or completed sign-in. The operator confirmed
seconds for the reported VCSEL 5 and privacy-indicator 2 observations, but
whether they describe duration or delay, their reference points and overlap
remain unknown. They are not emitter-control or safety measurements.

All five ETLs report zero native events lost. The three provider traces
report zero header buffers lost; WPR buffer loss remains unknown because its
payloads were not decoded. Each provider XML retains one classic
infrastructure processing error. The reported timestamp-offset differences
are preserved without a blanket correction; zero loss does not establish
complete circular history or phase continuity. No attributable CCI
transaction sequence or Linux camera capture has been recovered.

The final Windows sources passed 30 tracing tests, 49 resource-decoder
checks and 10 ETL audit/projection tests in each of Windows PowerShell 5.1
and PowerShell 7.6.4. Saved-XML replay recovered the exact UTC 3/1 event
counts, and all 42 provider time-bound CSV cells match the audit metadata.
These checks validate the corrected parser/projection behavior without
claiming that the final collector revision performed the saved live capture.

CCS and implementation follow-up
--------------------------------

Commit ``e6fa5cb158b56330b41dda6eb299041b3a3d39f3`` adds only the
three-line CCS underflow guard. The exact-function native ARM64 UBSan
harness reproduced invalid shifts of 119, 127 and 64 bits before the fix.
After it, 25 explicit cases, 1,536 exponent/sign/mantissa combinations and
one million deterministic patterns passed. The real CCS object and
composite built without warnings before and after the change; the CAMSS
regression target passed incrementally with unchanged object hashes.
Strict commit checkpatch excluded the sign-off requirement explicitly,
and the code commit passed its whitespace check. These checks do not
validate sensor limits, firmware, C-PHY operation or camera images.

The subsequent OpenCode architectural response remains unedited for audit.
Read its correction note first: the current PHY provider does not
implicitly reject C-PHY options, CSID configuration callbacks return void,
and CCS module matching is distinct from sensor firmware identity.
No generic C-PHY interface, sensor quirk, DT or emitter change from that
response was implemented. The debugger follow-up records installed image
identities and exact public-symbol URL observations; it does not establish
a connected debugger, armed breakpoint, CCI capture or safe MMIO access.

Evidence and reproduction
-------------------------

The files under ``sp11-camera-integration/`` form a portable source-tree
bundle. Keep them together to preserve the Markdown reports' relative links;
the HTML downloads below are individual artifacts. The
:download:`bundle manifest <sp11-camera-integration/bundle-manifest.json>`
records source and bundled SHA256 hashes, including the report's single
relative-link rebasing. The validation provenance records any sanitization
and the hashes of retained private originals. Raw captures, host paths,
credentials, and binary build products are not included.

* :download:`Windows validation summary
  <sp11-camera-integration/windows-validation.json>` and
  :download:`evidence verification
  <sp11-camera-integration/evidence-verification.json>`
* :download:`Selected RGB stream results
  <sp11-camera-integration/windows-rgb-stream-results.csv>`,
  :download:`advertised profiles
  <sp11-camera-integration/windows-advertised-profiles.csv>`, and
  :download:`control capabilities
  <sp11-camera-integration/windows-control-capabilities.json>`
* :download:`Focused kernel validation
  <sp11-camera-integration/remote-kernel-validation/kernel-validation.json>`,
  :download:`build configuration
  <sp11-camera-integration/remote-kernel-validation/arm64-camss.config>`, and
  :download:`inherited diagnostic comparison
  <sp11-camera-integration/remote-kernel-validation/diagnostic-comparison.json>`
* :download:`Exact receiver diff
  <sp11-camera-integration/remote-kernel-validation/camera-integration.patch>`
  and :download:`patch mail series
  <sp11-camera-integration/remote-kernel-validation/camera-integration-series.patch>`
* :download:`OpenCode review and correction notes
  <sp11-camera-integration/opencode-review-answer.md>` and
  :download:`review provenance
  <sp11-camera-integration/opencode-review-metadata.json>`
* :download:`Camera tracing guide
  <sp11-camera-integration/camera-tracing-guide.md>` and
  :download:`evidence-summary generator
  <sp11-camera-integration/Build-CameraEvidence.ps1>`

* :download:`CCS follow-up validation
  <sp11-camera-integration/ccs-followup-validation/validation.json>`,
  :download:`exact CCS patch
  <sp11-camera-integration/ccs-followup-validation/0001-media-ccs-avoid-oversized-shifts.patch>`,
  :download:`converter regression harness
  <sp11-camera-integration/ccs-followup-validation/ccs-float-candidate-harness.c>`,
  and :download:`source attribution and test scope
  <sp11-camera-integration/ccs-followup-validation/source-attribution.md>`
* :download:`Reviewed front-camera implementation plan
  <sp11-camera-integration/opencode-front-plan-review.md>`,
  :download:`unedited advisory response
  <sp11-camera-integration/opencode-front-plan-answer.raw.md>`, and
  :download:`front-plan review provenance
  <sp11-camera-integration/opencode-front-plan-metadata.json>`
* :download:`Debugger prerequisites follow-up
  <sp11-camera-integration/debugger-prerequisites-followup.md>` and
  :download:`its sanitized observations
  <sp11-camera-integration/debugger-prerequisites-followup.json>`
* :download:`Private ETL validation workflow
  <sp11-camera-integration/etl-validation-workflow.md>`

* :download:`Privileged capture verification
  <sp11-camera-integration/privileged-evidence-verification.json>` and
  :download:`privileged RGB stream results
  <sp11-camera-integration/windows-privileged-rgb-stream-results.csv>`
* :download:`Sanitized Hello observation
  <sp11-camera-integration/windows-hello-observation.json>` and
  :download:`registry-export follow-up
  <sp11-camera-integration/registry-export-followup.json>`
* :download:`Final Windows test and replay validation
  <sp11-camera-integration/windows-privileged-validation.json>`
* :download:`Sanitized ETL summary
  <sp11-camera-integration/etl-summary.sanitized.json>`,
  :download:`requested-provider table
  <sp11-camera-integration/etl-providers.sanitized.csv>`,
  :download:`ETL audit note
  <sp11-camera-integration/etl-validation.sanitized.md>`, and
  :download:`projection verification
  <sp11-camera-integration/etl-projection-verification.json>`
* :download:`Reviewed privileged-findings assessment
  <sp11-camera-integration/opencode-privileged-review-assessment.md>`,
  :download:`immutable advisory answer
  <sp11-camera-integration/opencode-privileged-answer.raw.md>`,
  :download:`approved review input
  <sp11-camera-integration/opencode-privileged-review-input.md>`, and
  :download:`privileged-review metadata
  <sp11-camera-integration/opencode-privileged-review-metadata.json>`
* :download:`ETL audit helper source
  <sp11-camera-integration/Audit-CameraEtls.ps1>`,
  :download:`ETL projection helper source
  <sp11-camera-integration/Export-CameraEtlEvidence.ps1>`, and
  :download:`ETL audit/projection test source
  <sp11-camera-integration/Test-CameraEtlAudit.ps1>`

The three PowerShell ETL helpers are reference source copies. They require
the original Windows tracing workspace's
``analysis/camera-integration-20260827/`` layout and its private evidence.
They are not kernel-build executables or a standalone capture kit; do not
run these copies from the kernel documentation directory. Their original
source bytes, comments and attribution are preserved.

Commands in the tracing guide and the evidence generator run from the
separately maintained Windows tracing workspace. The collector, debugger,
and test scripts referenced there are not shipped in this kernel bundle.
Privileged tracing and any interactive infrared or lock-screen tests require
the operator steps described in the guide. Do not infer safe infrared
emitter settings or successful Linux camera operation from enumeration or
Windows capability lists.
