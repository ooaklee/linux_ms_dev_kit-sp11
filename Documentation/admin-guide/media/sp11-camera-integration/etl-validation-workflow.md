# Private camera ETL validation

`Audit-CameraEtls.ps1` is an offline reader. It does not record ETW, control a
session, open cameras, fetch symbols, register manifests or upload files. Run it
only after the collector has finished and written `sha256-manifest.csv`.

```powershell
.\analysis\camera-integration-20260827\Audit-CameraEtls.ps1 `
  -CaptureDirectory .\output\COMPLETED-CAMERA-CAPTURE `
  -DecodeProviderXml
```

All output goes to a new **private** `output/camera-etl-audit-*` directory.
The input ETLs are held open with read sharing only, compared to their capture
manifest, and hashed again afterward. The capture directory is not changed.
The audit writes a separate manifest; do not add audit files to the original
capture manifest. Exit 2 means partial/no-ETL audit; inspect the report instead of
treating a process exit alone as camera success.

## Installed tool and exact commands

On this Surface's Windows build 29585, `tracerpt.exe` version
`10.0.29585.1000`, `wevtutil.exe` and `wpr.exe` are installed. `xperf.exe` and
`wpaexporter.exe` were not found in PATH or the standard Windows Performance
Toolkit directories. No additional tool installation is needed for this audit.

The first decoder pass suppresses event payload output:

```powershell
tracerpt.exe INPUT.etl -of CSV -o NUL -summary PRIVATE-summary.txt -y
```

The native summary contains alternative task/opcode and event-ID tables for the
same events. The audit selects one view for provider totals and checks table
counts against `Total Events Processed`; it never sums both views.

Optional provider-payload decoding uses:

```powershell
tracerpt.exe INPUT.etl -of XML -o PRIVATE-events.xml -summary PRIVATE-xml-summary.txt -y
```

These are file inputs, not the `-rt` real-time option. `-lr` is deliberately
omitted: it requests best-effort decoding when schemas do not match. Available
WPP decoding options include `-tmf`, `-tp`, `-i` and `-pdb`, but using them requires
the correct metadata for the exact driver build. No guessed metadata or symbol
download is part of this workflow. [Microsoft tracerpt reference](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/tracerpt)

`Get-WinEvent -Path INPUT.etl -Oldest` can enumerate ETL files. A read-only check
against the existing microphone ETL on this machine returned a classic
EventTrace header as `ProcessingErrorData` with zero projected properties.
That is a decoding limitation, not evidence that the header has no loss
counters. The audit therefore uses native tracerpt summaries and, when
requested, decoded XML. [Get-WinEvent reference](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.diagnostics/get-winevent)

For provider ETLs with completed XML decoding the audit also reads only
`Get-WinEvent` record metadata: provider identities and `TimeCreated`, never
`Message`, `Properties` or formatted payloads. It reports this reader's counts
separately from native processed events, independently counted XML event
elements, and parsed XML events. Classic infrastructure records can differ
between reader views without indicating lost camera events.

An actual Hello trace exposed a timestamp serialization mismatch on this build:
tracerpt wrote London local timestamps with offset `+00:59`, while
`Get-WinEvent` placed the same records 60 seconds earlier in UTC, consistent
with the collector's start time. XML offsets are preserved and flagged; the
audit does not silently subtract a guessed correction. Use the independent
ETL metadata bounds with the collector timeline, and keep continuity unknown.
The larger RGB XML files mix `+00:59` and `+01:00` within each file, so their
first/last boundary differences are not a uniform 60-second shift. The
sanitized CSV explicitly formats independent UTC as ISO8601 with fractional
precision, even when PowerShell 7 converts JSON strings to DateTime objects.

## Bounds and privacy

Defaults are 180 seconds per decoder process, 64 MiB maximum provider ETL input
for XML, 256 MiB decoded output, and 1 GiB minimum free disk. WPR ETLs receive a
summary only. Disk/output checks are sampled every 200 ms and can overshoot;
partial files remain explicitly incomplete. The decoder kills only its own
child if a bound is reached. The input ETL and the recorder are untouched.

The first disk check found only 4.4 GiB free. Do not decode the older 1.46 GB
microphone ETL to full XML as part of this camera task. Avoid heavy decoding
while a live camera capture is running. A memory-backed WPR recording can still
produce a large ETL when saved.

Raw XML may contain user/device identifiers, paths and biometric activity.
The JSON metadata audit retains provider/event identities, field **names**,
counts, time bounds and an explicit trace-header counter allowlist; it does not
copy application payload values. All of these files remain private by default.
Review derived evidence before sharing even the metadata audit.

## Interpret these measurements separately

| Evidence | Establishes | Does not establish |
| --- | --- | --- |
| ETL manifest match and unchanged hash | Attribution to this saved capture | Successful camera operation |
| Native events/buffers processed | Decoder processed retained data | Every intended scenario was recorded |
| Native `Total Events Lost` | Reported ETW event loss; missing labels remain unknown | Buffer loss, full circular history or schema coverage |
| Decoded header `EventsLost` / `BuffersLost` | Per-header loss counters when decoded | A missing header counter is zero |
| Decoded `LogFileMode` bit 0x2 | Circular file mode | Whether/how much old history was overwritten |
| Requested provider list vs observed GUID counts | Which requested providers emitted retained events | Silent providers malfunctioned or tracing reached the sensor bus |
| `ProcessingErrorData`, binary and unnamed field counts | Explicit decoding gaps/opaque data | Opaque payloads contain no useful register data |
| Per-type/provider non-header time bounds | Range of retained events, excluding recognized trace headers | Full phase coverage, continuity or absence of other rundown metadata |
| WinRT frame counts and actual formats | Frames delivered by the tested source/mode | Correct colours, photo/AF quality, IR or Linux parity |

Trace-header loss counters are retained per header rather than summed across
duplicate/merged headers. The header format documents both event and buffer
loss. [Microsoft EventTrace_Header](https://learn.microsoft.com/en-us/windows/win32/etw/eventtrace-header)

Circular file logging replaces old events after the configured maximum size is
reached. Treat that deliberate retention limit separately from reported event
loss. An event-loss counter of zero is therefore insufficient to claim full
capture history. [Microsoft logging modes](https://learn.microsoft.com/en-us/windows/win32/etw/logging-mode-constants)

Global XML timestamp bounds may include an old trace header or rundown. Use
the independent `WinEventMetadata` provider bounds and inspect `TimeCrossCheck`
before correlating XML non-header bounds to the phase timeline. Keep time
coverage/continuity unknown: sparse activity and other metadata can still make
the retained range misleading. Infrastructure GUIDs are excluded from the
independent camera-provider bounds.

## CCI/register evidence gate

Media Foundation and FrameServer activity can document formats, capture
lifecycle, timing and errors. It does not by itself identify CCI transactions.
The audit flags register-related field names only as leads, never as proof.
Useful register evidence needs a known schema that attributes the sensor/bus,
register address, width, value, read/write direction, timestamp and completion.
It must be correlated to the camera phase and the actual driver version.

No matches means only that the decoded field names provided no such lead.
Unknown schemas, binary data, unregistered vendor/WPP instrumentation or a
provider that was never enabled remain gaps. Correct manifest resources, MOF or
WPP/TMF metadata may be needed before these payloads can be interpreted.
[Microsoft event metadata overview](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/ETW/event-metadata-overview.md)

## Offline checks

```powershell
.\analysis\camera-integration-20260827\Test-CameraEtlAudit.ps1
& "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive `
  -File .\analysis\camera-integration-20260827\Test-CameraEtlAudit.ps1
```

Fixtures cover counter unknowns, long native summary rows, payload-value
exclusion, out-of-order timestamp bounds, duplicate headers, opaque/error
events, DTD/malformed XML rejection, child timeouts and output limits. No
camera or trace is activated by these checks. Manifest regression checks cover
both first creation and a retry, excluding the manifest from its own hashes.
Additional fixtures verify independent metadata-only time reads, record
disposal, and preservation of unusual serialized timestamp offsets.
The projection regression also covers string/DateTime/DateTimeOffset inputs,
PowerShell JSON conversion and locale-independent UTC CSV formatting.
