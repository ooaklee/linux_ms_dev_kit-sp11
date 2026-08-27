# SP11 camera tracing and integration evidence

The collector separates what Windows advertises, what a stream actually
delivers, and what remains to be measured. A successful script exit or an ETL
file does not establish Linux camera parity.

## Capture sequence

Close Windows Camera, browsers using a camera, conferencing software, and any
other capture client. Use a neutral non-person scene and keep `output/`
private. Do not change camera privacy permissions just to force a test through;
record a denied test and resolve access normally.

First collect inventory without starting a frame reader or ETW recorder:

```powershell
.\target\Trace-SP11Camera.ps1 `
    -Label camera-inventory -Phase inventory -NonInteractive -SkipWpr
```

This initializes recognized front/rear RGB groups to read their modes and
controls, which can power the sensors. Static video-profile metadata can cover
IR and other profiles without opening those devices. IR, depth, mixed RGB/IR,
and unrecognized groups are never initialized by the WinRT helper. Nothing is
photographed or saved as image data.

PnP property queries first select Camera/Image/Biometric-class devices and
camera-named/identified candidates, then walk their parent chains. This avoids
an expensive property request for every unrelated device. Metadata records the
present-device, candidate and query counts; topology start/end markers make
partial inventory visible. Use `-FullPnpPropertyScan` only when a missing or
misidentified camera requires the exhaustive, much slower legacy scan. The
PnP filter does not constrain WinRT source-group/profile enumeration.

Next check the available RGB frame-source formats:

```powershell
.\target\Trace-SP11Camera.ps1 `
    -Label camera-rgb-modes -Phase full -NonInteractive -AllModes `
    -PreviewSeconds 5 -SkipWpr `
    -ScenarioNote 'RGB source modes; acquired frames discarded; no IR activation'
```

Each acquired RGB frame reference is immediately disposed. This is real camera
activation with no retained pixels. The report records requested and actual
format, start status, duration, frame count and errors. The polling count is a
lower bound on received frames, not a lossless frame-rate measurement.

To collect WPR Video and provider ETW around these checks, run the same command
from an **elevated** shell and omit `-SkipWpr`. Each RGB phase gets separate
files. Elevation is never requested automatically. Without elevation,
`-SkipWpr` permits the metadata/frame-discard path and truthfully records that
no ETLs were captured.

For one Windows Hello observation, run interactively from an elevated shell:

```powershell
.\target\Trace-SP11Camera.ps1 -Label camera-hello -Phase ir-hello
```

The operator must explicitly agree to the workstation lock, then report
`face-success`, `face-failed`, `fallback` or `not-attempted`. A failed lock is an
error. Optional IR/VCSEL and privacy-indicator timing observations accept
`not-observed`. Never stare into the emitter or force it on. These observations
and event presence do not validate illuminator safety. The collector does not
save IR frames, write illuminator registers or change enrollment.

`-Phase full` without `-NonInteractive` guides Camera-app front/rear previews
and the Hello phase. Preview and mode-switch results are operator reports, not
automatic proofs. Samples remain disabled; explicit `-CaptureSamples` permits
bounded private front/rear ffmpeg clips only. `-SkipSamples` overrides that
switch. An unattended sample request is rejected unless samples are disabled.

## Limits and recorder ownership

| Option or artifact | Meaning |
| --- | --- |
| `-SkipWpr` | Skips WPR and, by default, provider ETW. Does not skip RGB probing. |
| `-SkipWpr -CaptureEtw` | Requests provider ETW only; requires elevation. |
| `-SkipEtw` | Skips provider ETW without changing WPR selection. |
| `-MaximumMegabytes 128..512` | Circular provider-ETL cap, default 256 MB. Does not cap WPR Video. |
| `-PreviewSeconds 5..60` | Requested duration per tested RGB format, default 20 seconds. |
| `-AllModes` | Attempts initialized RGB frame-source formats; does not exercise every advertised profile or still-photo mode. |
| WinRT stream budget | At most 64 mode attempts and 600 seconds per camera; an in-flight operation can add its timeout. The parent terminates its own child at 780 seconds for an AllModes probe. |
| Ordinary WinRT child bound | `PreviewSeconds + 150` seconds for initialization/current-format operations. |
| Recorder watchdog | 900 seconds for unattended AllModes phases; otherwise `PreviewSeconds + 240` seconds. A deadline makes the phase incomplete. |

WPR uses the built-in `Video` profile in file mode, so allow disk space beyond
the provider-ETL cap. The collector inventories the available profiles first.
Provider ETW discovers names and GUIDs from `logman query providers`; it does
not guess Qualcomm GUIDs or enable broad packet-capture providers. Biometric
providers are enabled only for the interactive Hello phase. A provider's
registration does not mean it emits events during a particular test.

Every recording uses a fresh unique name. WPR status, stop, cancel and watchdog
commands carry that exact `-instancename`; logman operations target only the
unique direct ETW session with `-ets`. No global WPR cancellation, machine-wide
trace stopping, registry configuration, driver replacement, or boot change is
performed. If a stop fails, keep the named-session logs and inspect
`RecorderCleanup` in the summary. Do not stop somebody else's session to make
the run green. Watchdogs bound normal stalls; a forcibly terminated host or
machine crash still requires checking for the recorded owned session name.

These switches follow Microsoft's [WPR instance-name
semantics](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/wpr-command-line-options#instancename)
and [logman trace
options](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/logman-create-trace).

## Read the result

Start with `capture-summary.json` and `camera-coverage.md`. The CSV/JSON coverage
table always includes front, rear and IR, including skipped/failed phases.
Do not count an advertised mode as tested unless the corresponding stream row
shows actual frames, its expected format and the requested duration.

| Artifact | Evidence it provides | What it does not prove |
| --- | --- | --- |
| `camera-devices.csv`, endpoint chains | PnP identity, driver and parent relationships | Correct Linux device-tree graph or live sensor data |
| WinRT `ProfileDevices`, `VideoProfiles` | Static video/photo/profile and declared concurrency metadata | Successful initialization or streaming in those profiles; unavailable concurrency queries are not negative support claims |
| WinRT `Modes`, `Controls` | Initialized RGB pipeline formats and control capabilities | Sensor-native register tables, autofocus operation, HDR/ISP quality or still-photo capture |
| WinRT `Streams` | Per-source requested/actual format and actual frame delivery | Lossless FPS, simultaneous-camera support, image quality or Linux support |
| `camera-etw-providers.json`, per-provider files | Registered provider identities, requested keywords and level | Emitted event coverage or CCI writes |
| Per-phase WPR/provider ETLs | Recorded Windows pipeline activity | Sensor register sequences without an identified provider/schema; decode and inspect event loss before analysis |
| `camera-package-files.csv/json` | Original DriverStore path, byte size and SHA-256 | Permission to redistribute binaries or their schema |
| `camera-coverage.csv/json/md` | Explicit success/partial/skip/manual requirements and missing hardware tests | Full Windows/Linux parity |
| `sha256-manifest.csv` | Hash and length of every completed artifact except the manifest itself | Semantic validity, privacy sanitization or successful camera operation |

Metadata records collector/support/probe hashes, OS build, host PowerShell and
process architecture. The WinRT helper runs under Windows PowerShell 5.1 even
when the parent collector runs under PowerShell 7.

## Evidence still needed for Linux integration

The public front-camera work supplies extracted IMX681 tables, not a reusable
sensormodule decoder. Rear/IR binary schemas, table boundaries, register widths,
delays and mode transitions need independent validation. Decoding ACPI AeoB
SCFG/CAM resource blobs reveals topology/resources, not sensor register writes.
Never commit Windows driver, tuning or calibration binaries.

For each actual sensor and operating mode, use the existing debugger stages to
obtain the missing hardware evidence only after reviewing exact modules and
symbols:

1. `windbg/30-camera-session-inventory.txt`: identify the active stack and
   relevant device/controller names.
2. `windbg/31-camera-cci-register-writes-pending.md`: review and arm a bounded
   sensor-write capture against the exact driver/PDB. ETW provider discovery
   does not replace this step.
3. `windbg/32-camera-camss-mmio-dump.txt`: collect per-camera active-stream
   CAMSS/PHY/CSID state only while the relevant clocks are on.

Keep sensor identity, CCI address convention, rails, reset/MCLK ordering, CSI
lanes, link frequencies, payload format and register provenance separate from
Windows ISP output formats. IR illumination safety remains its own reviewed
hardware task; RGB success is not authorization to drive an emitter.

Linux acceptance still requires sensor probe, stream start/stop and format
checks, repeated camera switching, power/suspend recovery, controls, stills,
concurrency if supported, image-quality comparison, and a separate safe IR
validation plan. Preserve every missing item in the integration report; do not
label the branch as full parity before hardware checks establish it.

## Offline regression checks

```powershell
powershell.exe -NoProfile -NonInteractive -File .\tests\Test-CameraTracing.ps1
pwsh.exe -NoProfile -NonInteractive -File .\tests\Test-CameraTracing.ps1
```

The tests parse all scripts, mock WPR/logman cleanup and phase outcomes, check
IR classification without loading WinRT, verify manifest hashes and exercise
only harmless child processes for quoting and timeouts. They never open a
camera or start a real ETW recording. A live elevated run remains necessary to
validate provider availability, ETL output and loss on the installed OS.
