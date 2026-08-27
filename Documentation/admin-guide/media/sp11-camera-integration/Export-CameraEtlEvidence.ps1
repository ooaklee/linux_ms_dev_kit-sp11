#Requires -Version 5.1
# Exports selected metadata only. Raw XML, payload values, account/device IDs,
# absolute user paths and decoder stdout are never copied into these files.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string[]]$AuditDirectories,
    [string]$OutputDirectory = $PSScriptRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function ConvertTo-CameraEvidenceUtc {
    param([AllowNull()]$Value)
    if ($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)) { return $null }
    if ($Value -is [DateTimeOffset]) { return $Value.UtcDateTime.ToString('o', [Globalization.CultureInfo]::InvariantCulture) }
    if ($Value -is [DateTime]) {
        if ($Value.Kind -eq [DateTimeKind]::Unspecified) { throw 'UTC projection refuses a DateTime with unspecified timezone.' }
        return $Value.ToUniversalTime().ToString('o', [Globalization.CultureInfo]::InvariantCulture)
    }
    $text = [string]$Value
    if ($text -notmatch '(Z|[+-]\d{2}:\d{2})$') { throw 'UTC projection requires an explicit timestamp offset.' }
    return ([DateTimeOffset]::Parse($text, [Globalization.CultureInfo]::InvariantCulture)).UtcDateTime.ToString('o', [Globalization.CultureInfo]::InvariantCulture)
}

if (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) { New-Item -ItemType Directory -Path $OutputDirectory | Out-Null }
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$infrastructureGuids = @('{68fdd900-4a3e-11d1-84f4-0000f80464e3}', '{9e814aad-3204-11d2-9a82-006008a86939}')
$summaries = [Collections.Generic.List[object]]::new()
$providers = [Collections.Generic.List[object]]::new()
$verifications = [Collections.Generic.List[object]]::new()

foreach ($directory in $AuditDirectories) {
    $directory = (Resolve-Path -LiteralPath $directory).Path
    $auditManifestPath = Join-Path $directory 'audit-sha256-manifest.csv'
    $auditManifest = @(Import-Csv -LiteralPath $auditManifestPath)
    if ($auditManifest.Count -eq 0) { throw 'Audit manifest is empty; refusing to export an incomplete analysis attempt.' }
    $listedFiles = @($auditManifest.File | Sort-Object)
    $actualFiles = @(Get-ChildItem -LiteralPath $directory -File | Where-Object Name -ne 'audit-sha256-manifest.csv' | Select-Object -ExpandProperty Name | Sort-Object)
    if (@(Compare-Object $listedFiles $actualFiles).Count) { throw 'Audit contains unlisted or missing files; no projection published.' }
    foreach ($entry in $auditManifest) {
        if ([IO.Path]::GetFileName($entry.File) -ne $entry.File) { throw 'Audit manifest entry is not a simple file name.' }
        $file = Get-Item -LiteralPath (Join-Path $directory $entry.File)
        if ($file.Length -ne [long]$entry.Length -or (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne $entry.SHA256) { throw 'Audit artifact hash/length verification failed.' }
    }
    $audit = Get-Content -LiteralPath (Join-Path $directory 'camera-etl-audit.private.json') -Raw | ConvertFrom-Json
    $capture = $audit.CaptureDirectory
    if ((Get-FileHash -LiteralPath (Join-Path $capture 'sha256-manifest.csv') -Algorithm SHA256).Hash -ne $audit.CaptureManifestSHA256) { throw 'Capture manifest changed since the audit.' }
    $registered = @{}
    foreach ($provider in @(Get-Content -LiteralPath (Join-Path $capture 'camera-etw-providers.json') -Raw | ConvertFrom-Json)) {
        $registered[$provider.Guid.ToLowerInvariant()] = $provider.Name
    }
    $verifications.Add([pscustomobject]@{ AuditRun = [IO.Path]::GetFileName($directory); CaptureRun = [IO.Path]::GetFileName($capture)
        AuditPayloadsVerified = $auditManifest.Count; AuditManifestSHA256 = (Get-FileHash -LiteralPath $auditManifestPath -Algorithm SHA256).Hash
        CaptureManifestSHA256 = $audit.CaptureManifestSHA256; CaptureManifestUnchanged = $true
        AuditorSHA256 = $audit.AnalysisScriptSHA256; AuditFinishedUtc = (ConvertTo-CameraEvidenceUtc -Value $audit.FinishedUtc) })
    foreach ($source in $audit.Sources) {
        if (-not $source.ManifestMatches -or -not $source.SourceUnchanged) { throw 'A source ETL lacks successful provenance/stability checks; refusing to publish a clean provenance statement.' }
        $xmlDecoded = $source.Xml.Status -eq 'decoded'
        $bufferLoss = @(); $headerEventLoss = @(); $payloadErrors = $null; $metadataErrors = $null
        $triage = @(); $offsets = @()
        if ($xmlDecoded) {
            foreach ($header in $source.Xml.Headers) {
                if ($null -ne $header.Counters.PSObject.Properties['BuffersLost']) { $bufferLoss += $header.Counters.BuffersLost }
                if ($null -ne $header.Counters.PSObject.Properties['EventsLost']) { $headerEventLoss += $header.Counters.EventsLost }
            }
            $payloadErrors = [long]0; $metadataErrors = [long]0
            foreach ($type in $source.Xml.EventTypes) {
                if ($type.ProviderGuid -in $infrastructureGuids) { $metadataErrors += $type.ProcessingErrors }
                else { $payloadErrors += $type.ProcessingErrors }
            }
            $triage = @($source.Xml.EventTypes | ForEach-Object { $_.RegisterTriageFieldNames } | Sort-Object -Unique)
            $offsets = @($source.Xml.SerializedTimeOffsets)
        }
        $summaries.Add([pscustomobject][ordered]@{
            CaptureRun = [IO.Path]::GetFileName($capture); AuditRun = [IO.Path]::GetFileName($directory); File = $source.File
            Length = $source.Length; SHA256 = $source.SHA256; CaptureManifestMatches = $source.ManifestMatches; SourceUnchangedDuringAudit = $source.SourceUnchanged
            CaptureCollectionStatus = $audit.CaptureCollectionStatus; AuditStatus = $source.Status
            NativeEventsProcessed = $source.Summary.Counters.EventsProcessed; NativeEventsLost = $source.Summary.Counters.EventsLost
            NativeBuffersProcessed = $source.Summary.Counters.BuffersProcessed; NativeElapsedSeconds = $source.Summary.Counters.ElapsedSeconds
            NativeProviderGuids = @($source.Summary.Providers.ProviderGuid)
            XmlStatus = $source.Xml.Status; XmlEventElements = if ($xmlDecoded) { $source.Xml.EventElements } else { $null }
            XmlParsedEvents = if ($xmlDecoded) { $source.Xml.Events } else { $null }
            XmlProcessingErrors = if ($xmlDecoded) { $source.Xml.ProcessingErrors } else { $null }
            XmlCameraProviderProcessingErrors = $payloadErrors; XmlTraceMetadataProcessingErrors = $metadataErrors
            HeaderEventsLost = @($headerEventLoss | Sort-Object -Unique); HeaderBuffersLost = @($bufferLoss | Sort-Object -Unique)
            BuffersLostKnown = $bufferLoss.Count -gt 0
            RequestedProviders = if ($source.RequestedProviderListStatus -eq 'present') { $source.RequestedProviders.Count } else { $null }
            RequestedEmittingProviders = if ($source.RequestedProviderListStatus -eq 'present') { @($source.RequestedProviders | Where-Object Events -gt 0).Count } else { $null }
            RequestedSilentProviders = if ($source.RequestedProviderListStatus -eq 'present') { @($source.RequestedProviders | Where-Object Events -eq 0).Count } else { $null }
            WinEventRecords = if ($source.WinEventMetadata.Status -eq 'read') { $source.WinEventMetadata.Events } else { $null }
            XmlSerializedTimeOffsets = $offsets; XmlTimeCrossCheck = $source.TimeCrossCheck
            RegisterTriageFieldNames = $triage; CciRegisterTransactionsEstablished = $false; FullPhaseContinuityEstablished = $false
        })
        foreach ($requested in $source.RequestedProviders) {
            $guid = $requested.ProviderGuid
            $types = @(if ($xmlDecoded) { $source.Xml.EventTypes | Where-Object ProviderGuid -eq $guid })
            $timeRows = @(if ($source.WinEventMetadata.Status -eq 'read') { $source.WinEventMetadata.Providers | Where-Object ProviderGuid -eq $guid })
            $cross = @($source.TimeCrossCheck | Where-Object ProviderGuid -eq $guid)
            $fieldNames = @($types | ForEach-Object { $_.FieldNames } | Where-Object {
                $_ -match '^[A-Za-z_][A-Za-z0-9_./:-]{0,119}$' -and
                $_ -match '(?i)(frame|width|height|format|duration|status|error|hresult|mode|stream|sensor|exposure|focus|gain|register|cci|i2c|spb|length|size|count|timestamp)' -and
                $_ -notmatch '(?i)(user|account|sid|token|password|secret|serial|path|symbolic|identifier|deviceid)'
            } | Sort-Object -Unique)
            $providers.Add([pscustomobject][ordered]@{
                CaptureRun = [IO.Path]::GetFileName($capture); File = $source.File; ProviderGuid = $guid
                RegisteredName = if ($registered.ContainsKey($guid)) { $registered[$guid] } else { '(not in saved registered-provider map)' }
                Requested = $true; EmittedRetainedEvents = $requested.Events -gt 0; NativeEvents = $requested.Events
                XmlEvents = if (-not $xmlDecoded) { $null } elseif ($types.Count) { [long](($types | Measure-Object -Property Events -Sum).Sum) } else { [long]0 }
                XmlProcessingErrors = if (-not $xmlDecoded) { $null } elseif ($types.Count) { [long](($types | Measure-Object -Property ProcessingErrors -Sum).Sum) } else { [long]0 }
                EventIds = if ($types.Count) { @($types.EventId | Sort-Object -Unique) -join ';' } else { '' }
                SelectedFieldNames = $fieldNames -join ';'
                WinEventEvents = if ($timeRows.Count -eq 1) { $timeRows[0].Events } elseif ($source.WinEventMetadata.Status -eq 'read') { 0 } else { $null }
                FirstEventUtc = if ($timeRows.Count -eq 1) { ConvertTo-CameraEvidenceUtc -Value $timeRows[0].FirstEventUtc } else { $null }
                LastEventUtc = if ($timeRows.Count -eq 1) { ConvertTo-CameraEvidenceUtc -Value $timeRows[0].LastEventUtc } else { $null }
                TimeBasis = 'Independent Get-WinEvent metadata; no raw payload values; continuity unknown.'
                XmlMinusWinEventFirstSeconds = if ($cross.Count -eq 1) { $cross[0].XmlMinusWinEventFirstSeconds } else { $null }
                XmlMinusWinEventLastSeconds = if ($cross.Count -eq 1) { $cross[0].XmlMinusWinEventLastSeconds } else { $null }
            })
        }
    }
}

$summaryPath = Join-Path $OutputDirectory 'etl-summary.sanitized.json'
$providerPath = Join-Path $OutputDirectory 'etl-providers.sanitized.csv'
$markdownPath = Join-Path $OutputDirectory 'etl-validation.sanitized.md'
[pscustomobject][ordered]@{ SchemaVersion = '1.0'; GeneratedUtc = [DateTime]::UtcNow.ToString('o'); Etls = $summaries.ToArray()
    Limitations = @('Only selected event metadata is exported. Raw payloads, device/account identifiers, paths and decoded XML remain private.',
        'Native event loss is distinct from buffer loss, schema errors, circular history and camera success. Missing buffer counters remain unknown.',
        'WPR is summary-only; vendor-register payload coverage there was not decoded.',
        'Get-WinEvent and tracerpt can expose classic headers differently; counts are retained separately.',
        'Hello has +00:59 serialization and all eight emitting-provider bounds differ by +60 seconds from Get-WinEvent. RGB mixes +00:59/+01:00 within each file, so its boundary differences are not a uniform shift. XML is retained without correction.',
        'No validated CCI/register transactions or full phase continuity is established. Sparse emission, metadata and circular retention prevent parity claims.') } |
    ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding utf8
$providers.ToArray() | Sort-Object File, RegisteredName | Export-Csv -LiteralPath $providerPath -NoTypeInformation -Encoding utf8
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('# Camera ETL validation (selected metadata)'); $lines.Add('')
$lines.Add('The completed RGB and fresh Hello retry are included. The interrupted Hello run and first incomplete audit are excluded. Raw ETLs/XML remain private.'); $lines.Add('')
$lines.Add('| ETL | Native events | Events lost | Buffer loss | XML camera/schema errors | Requested providers emitting |')
$lines.Add('| --- | ---: | ---: | --- | --- | --- |')
foreach ($source in $summaries) {
    $buffers = if ($source.BuffersLostKnown) { $source.HeaderBuffersLost -join ',' } else { 'unknown' }
    $schema = if ($source.XmlStatus -eq 'decoded') { [string]$source.XmlCameraProviderProcessingErrors + ' camera; ' + $source.XmlTraceMetadataProcessingErrors + ' trace metadata' } else { 'summary only' }
    $emission = if ($null -eq $source.RequestedProviders) { 'WPR profile' } else { [string]$source.RequestedEmittingProviders + '/' + $source.RequestedProviders }
    $lines.Add('| ' + $source.File + ' | ' + $source.NativeEventsProcessed + ' | ' + $source.NativeEventsLost + ' | ' + $buffers + ' | ' + $schema + ' | ' + $emission + ' |')
}
$lines.Add(''); $lines.Add('Every source ETL matched its original capture manifest and was unchanged during the audit. Audit manifests and payload hashes were independently checked before this projection. The two captures retain their `partial-inventory` status; those inventory errors are separate from ETW transport and decoding.'); $lines.Add('')
$lines.Add('## Time and schema caveats'); $lines.Add('')
$lines.Add('The provider CSV maps requested GUIDs to the names saved during discovery, includes silent providers, and uses independent `Get-WinEvent` metadata for UTC bounds. No payload values or device/account identifiers were exported.'); $lines.Add('')
$lines.Add('Hello XML used `+00:59`; all eight emitting-provider bounds are 60 seconds later than the independent reader. RGB XML mixes `+00:59` and `+01:00` within each file, so its first/last boundary differences are not uniform. Do not apply a single time shift. The CSV uses independent ISO8601 UTC timestamps with fractional precision; XML remains unchanged and flagged. No event bounds prove phase continuity.'); $lines.Add('')
$lines.Add('Native, XML element, XML parser and Get-WinEvent counts are separate columns in the JSON. Classic trace headers can be exposed differently. ProcessingErrorData counts do not automatically mean lost camera events. WPR was not payload-decoded, so its buffer-loss/schema/register coverage remains unknown.'); $lines.Add('')
$lines.Add('## Register evidence'); $lines.Add('')
$lines.Add('No attributable CCI/register transaction sequence has been validated. Selected field-name triage is only a lead, not a bus address/value/direction/completion trace. Zero transport loss, advertised controls and operator-reported Hello success do not establish Linux camera parity.'); $lines.Add('')
$lines.Add('Source hashes, per-provider counts and safe field names are in `etl-summary.sanitized.json` and `etl-providers.sanitized.csv`. Verification records are in `etl-projection-verification.json`.')
$lines | Set-Content -LiteralPath $markdownPath -Encoding utf8
$outputs = @($summaryPath, $providerPath, $markdownPath) | ForEach-Object { [pscustomobject]@{ File = [IO.Path]::GetFileName($_); SHA256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash } }
[pscustomobject]@{ VerifiedUtc = [DateTime]::UtcNow.ToString('o'); ExporterSHA256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
    Audits = $verifications.ToArray(); Outputs = @($outputs) } | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath (Join-Path $OutputDirectory 'etl-projection-verification.json') -Encoding utf8
Write-Host ('Exported ' + $summaries.Count + ' ETLs and ' + $providers.Count + ' requested-provider rows to ' + $OutputDirectory)
