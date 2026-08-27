.. SPDX-License-Identifier: GPL-2.0

Surface Pro 11 camera integration evidence
==========================================

This bundle records the camera investigation of 27 August 2026 for the
experimental ``sp11/integration-7.2.x-ooaklee-karsies-wq-cams`` branch.
Two receiver changes disable unconfigured CSID680 RDI crop/drop enables and
select the VFE680 RDI MIPI RAW write mode. No sensor is enabled by this series.
Linux capture, infrared operation, Windows Hello, and full camera feature
parity remain unvalidated. Sensor power, per-endpoint PHY configuration,
buffer layout, privacy indicators, and infrared illumination still need
evidence and hardware tests before camera enablement.

Start with the :download:`integration report
<sp11-camera-integration/camera-integration-report.md>` and the
:download:`source audit <sp11-camera-integration/source-audit.md>`.
The report distinguishes advertised capabilities, observed Windows stream
checks, static resource declarations, and unverified Linux behavior.
Source pins and existing author attributions are preserved in the audit and
patch series.

Verification performed
----------------------

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
  The broader repository check has inherited diagnostics: 11 errors,
  67 warnings, and 497 checks at both base and patched revisions, including
  three project-authored errors. Its broken-pipe handling can return zero
  despite errors, so its exit status does not establish clean CI.
* The preserved diff and mail artifacts retain blank context lines, context
  prefixes before tabs, mail separators, and an ending blank line. A whole
  documentation whitespace check reports those artifact lines. Authored
  documentation and the other evidence files are checked separately without
  rewriting the original patch bytes or their hashes. Windows evidence
  also retains CRLF line endings; that check recognizes CR at end of line
  using a command-local Git whitespace setting.

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

Commands in the tracing guide and the evidence generator run from the
separately maintained Windows tracing workspace. The collector, debugger,
and test scripts referenced there are not shipped in this kernel bundle.
Privileged tracing and any interactive infrared or lock-screen tests require
the operator steps described in the guide. Do not infer safe infrared
emitter settings or successful Linux camera operation from enumeration or
Windows capability lists.
