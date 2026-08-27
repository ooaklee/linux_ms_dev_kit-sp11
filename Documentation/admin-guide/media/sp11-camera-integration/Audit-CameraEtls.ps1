#Requires -Version 5.1
<#
.SYNOPSIS
Audits completed camera ETLs without starting or controlling any trace session.
.DESCRIPTION
Uses installed tracerpt.exe in strict mode, first writing only a native summary.
Optional provider XML decoding is bounded by time, input size, sampled output
size and free disk space. All outputs, including the metadata-only audit, remain
private under workspace output/. The original capture and manifest are untouched.
This does not prove camera parity, complete circular history or CCI transactions.
.EXAMPLE
.\Audit-CameraEtls.ps1 -CaptureDirectory .\output\completed-camera-run -DecodeProviderXml
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CaptureDirectory,
    [switch]$DecodeProviderXml,
    [ValidateRange(10, 3600)][int]$DecoderTimeoutSeconds = 180,
    [ValidateRange(1, 1024)][int]$MaximumInputMegabytes = 64,
    [ValidateRange(1, 2048)][int]$MaximumDecodedMegabytes = 256,
    [ValidateRange(256, 8192)][int]$MinimumFreeMegabytes = 1024
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Read-CameraEtlSummary {
    param([Parameter(Mandatory)][string]$Path)
    $text = Get-Content -LiteralPath $Path -Raw
    $counts = [ordered]@{}
    foreach ($entry in @{
        BuffersProcessed = '(?im)^Total Buffers\s+Processed\s+(\d+)\s*$'
        EventsProcessed = '(?im)^Total Events\s+Processed\s+(\d+)\s*$'
        EventsLost = '(?im)^Total Events\s+Lost\s+(\d+)\s*$'
        ElapsedSeconds = '(?im)^Elapsed Time\s+(\d+)\s+sec\s*$'
    }.GetEnumerator()) {
        $match = [regex]::Match($text, $entry.Value)
        $counts[$entry.Key] = if ($match.Success) { [long]$match.Groups[1].Value } else { $null }
    }
    $table = 'unclassified'
    $allRows = @(foreach ($line in ($text -split '\r?\n')) {
        if ($line -match '^\|Event Count\s+Event Name\s+') {
            $table = if ($line -match '\sEvent ID\s+') { 'event-id' } elseif ($line -match '\sTask\s+Opcode\s+') { 'task-opcode' } else { 'unclassified' }
            continue
        }
        # Long provider/task names overflow tracerpt's display columns. Do not
        # invent EventIDs or split these rows by fixed character positions.
        $match = [regex]::Match($line, '^\|\s*(\d+)\s+(.+?)\s+(\{[0-9a-fA-F-]{36}\})\s*\|$')
        if ($match.Success) {
            [pscustomobject]@{ Table = $table; Count = [long]$match.Groups[1].Value
                Descriptor = $match.Groups[2].Value.Trim()
                ProviderGuid = $match.Groups[3].Value.ToLowerInvariant() }
        }
    })
    # tracerpt prints TWO views of the same events (task/opcode and event ID).
    # Adding both tables doubles every provider's apparent coverage.
    $selectedTable = if (@($allRows | Where-Object Table -eq 'event-id').Count) { 'event-id' } else { 'task-opcode' }
    $rows = @($allRows | Where-Object Table -eq $selectedTable)
    $tables = @(foreach ($group in @($allRows | Group-Object Table)) {
        $total = [long](($group.Group | Measure-Object -Property Count -Sum).Sum)
        [pscustomobject]@{ Table = $group.Name; Events = $total
            MatchesProcessed = if ($null -eq $counts.EventsProcessed) { $null } else { $total -eq $counts.EventsProcessed } }
    })
    $tableTotal = [long]0
    foreach ($row in $rows) { $tableTotal += $row.Count }
    $providers = @(foreach ($group in @($rows | Group-Object ProviderGuid)) {
        [pscustomobject]@{ ProviderGuid = $group.Name
            Events = [long](($group.Group | Measure-Object -Property Count -Sum).Sum) }
    })
    [pscustomobject]@{ Counters = [pscustomobject]$counts; EventTypes = $rows; Providers = $providers
        SelectedTable = $selectedTable; NativeTables = $tables
        TableEvents = $tableTotal
        TableMatchesProcessed = if ($null -eq $counts.EventsProcessed) { $null } else { $tableTotal -eq $counts.EventsProcessed }
        CounterLanguage = 'English CLI labels; unrecognized/missing counters remain null, never zero.' }
}

function Measure-CameraEtlXmlEvents {
    param([Parameter(Mandatory)][string]$Path)
    $settings = [Xml.XmlReaderSettings]::new()
    $settings.DtdProcessing = [Xml.DtdProcessing]::Prohibit; $settings.XmlResolver = $null
    $reader = [Xml.XmlReader]::Create($Path, $settings)
    $count = [long]0
    try {
        while ($reader.Read()) {
            if ($reader.NodeType -eq [Xml.XmlNodeType]::Element -and $reader.Depth -eq 1 -and $reader.LocalName -eq 'Event' -and $reader.NamespaceURI -eq 'http://schemas.microsoft.com/win/2004/08/events/event') { $count++ }
        }
    } finally { $reader.Dispose() }
    return $count
}

function Read-CameraEtlWinEventMetadata {
    param([Parameter(Mandatory)][string]$Path)
    $providers = @{}; $count = [long]0; $infrastructure = [long]0
    Get-WinEvent -Path $Path -Oldest -ErrorAction Stop | ForEach-Object {
        $record = $_
        try {
            $count++
            $guid = if ($null -eq $record.ProviderId) { '(unknown)' } else { '{' + $record.ProviderId.ToString().ToLowerInvariant() + '}' }
            if ($guid -in @('{68fdd900-4a3e-11d1-84f4-0000f80464e3}', '{9e814aad-3204-11d2-9a82-006008a86939}')) { $infrastructure++ }
            else {
                if (-not $providers.ContainsKey($guid)) { $providers[$guid] = @{ Events = [long]0; TimedEvents = [long]0; Names = @{}; First = $null; Last = $null } }
                $row = $providers[$guid]; $row.Events++
                if (-not [string]::IsNullOrEmpty($record.ProviderName)) { $row.Names[$record.ProviderName] = $true }
                # Access no Message, Properties, FormatDescription or payload XML.
                if ($null -ne $record.TimeCreated) {
                    $utc = $record.TimeCreated.ToUniversalTime(); $row.TimedEvents++
                    if ($null -eq $row.First -or $utc -lt $row.First) { $row.First = $utc }
                    if ($null -eq $row.Last -or $utc -gt $row.Last) { $row.Last = $utc }
                }
            }
        } finally { $record.Dispose() }
    }
    [pscustomobject]@{ Status = 'read'; Events = $count; TraceInfrastructureEvents = $infrastructure
        TimeBasis = 'Get-WinEvent TimeCreated converted to UTC from the saved ETL; payload values are not accessed. Compare counts per provider, since readers expose classic trace metadata differently.'
        FullPhaseCoverageEstablished = $false
        Providers = @(foreach ($guid in @($providers.Keys | Sort-Object)) {
            $row = $providers[$guid]
            [pscustomobject]@{ ProviderGuid = $guid; ProviderNames = @($row.Names.Keys | Sort-Object)
                Events = $row.Events; TimedEvents = $row.TimedEvents
                FirstEventUtc = if ($null -eq $row.First) { $null } else { $row.First.ToString('o') }
                LastEventUtc = if ($null -eq $row.Last) { $null } else { $row.Last.ToString('o') } }
        }) }
}

function Read-CameraEtlXmlMetadata {
    param([Parameter(Mandatory)][string]$Path)
    Add-Type -AssemblyName System.Xml.Linq
    $ns = [Xml.Linq.XNamespace]'http://schemas.microsoft.com/win/2004/08/events/event'
    $settings = [Xml.XmlReaderSettings]::new()
    $settings.IgnoreWhitespace = $true
    $settings.DtdProcessing = [Xml.DtdProcessing]::Prohibit
    $settings.XmlResolver = $null
    $reader = [Xml.XmlReader]::Create($Path, $settings)
    $types = @{}
    $timeOffsets = @{}
    $headers = [Collections.Generic.List[object]]::new()
    $count = [long]0; $errors = [long]0; $binaryEvents = [long]0
    $first = $null; $last = $null
    try {
        while ($reader.Read()) {
            if ($reader.NodeType -ne [Xml.XmlNodeType]::Element -or $reader.LocalName -ne 'Event' -or $reader.NamespaceURI -ne [string]$ns) { continue }
            $subtree = $reader.ReadSubtree()
            try { $event = [Xml.Linq.XElement]::Load($subtree) } finally { $subtree.Dispose() }
            $count++
            $system = $event.Element($ns + 'System')
            if ($null -eq $system) { throw 'Event has no System element; metadata audit is incomplete.' }
            $provider = $system.Element($ns + 'Provider')
            if ($null -eq $provider) { throw 'Event has no Provider element; metadata audit is incomplete.' }
            $guidAttribute = $provider.Attribute('Guid')
            $guid = if ($null -eq $guidAttribute) { '' } else { $guidAttribute.Value.Trim().ToLowerInvariant() }
            $nameAttribute = $provider.Attribute('Name')
            $providerName = if ($null -eq $nameAttribute) { '' } else { $nameAttribute.Value }
            $systemFields = @{}
            foreach ($name in @('EventID', 'Opcode', 'Task', 'Version')) {
                $element = $system.Element($ns + $name)
                $systemFields[$name] = if ($null -eq $element) { '' } else { $element.Value }
            }
            $eventId = $systemFields.EventID; $opcode = $systemFields.Opcode
            $task = $systemFields.Task; $version = $systemFields.Version
            $isHeader = $guid -in @('{68fdd900-4a3e-11d1-84f4-0000f80464e3}', '{9e814aad-3204-11d2-9a82-006008a86939}') -and $eventId -eq '0' -and $opcode -eq '0'
            $hasError = $null -ne $event.Element($ns + 'ProcessingErrorData')
            if ($hasError) { $errors++ }
            $hasBinary = @($event.Descendants($ns + 'Binary')).Count -gt 0
            if ($hasBinary) { $binaryEvents++ }
            $timeElement = $system.Element($ns + 'TimeCreated')
            $timestamp = $null
            if ($null -ne $timeElement -and $null -ne $timeElement.Attribute('SystemTime')) {
                $rawTimestamp = $timeElement.Attribute('SystemTime').Value
                $offset = [regex]::Match($rawTimestamp, '(Z|[+-]\d{2}:\d{2})$')
                $timeOffsets[$(if ($offset.Success) { $offset.Value } else { '(no offset)' })] = $true
                $timestamp = [DateTimeOffset]::Parse($rawTimestamp, [Globalization.CultureInfo]::InvariantCulture)
                # Circular logs can be out of order; calculate bounds, not first/last encounter.
                if ($null -eq $first -or $timestamp -lt $first) { $first = $timestamp }
                if ($null -eq $last -or $timestamp -gt $last) { $last = $timestamp }
            }
            $key = @($guid, $providerName, $eventId, $version, $task, $opcode) -join '|'
            if (-not $types.ContainsKey($key)) {
                $types[$key] = [ordered]@{ ProviderGuid = $guid; ProviderName = $providerName
                    EventId = $eventId; Version = $version; Task = $task; Opcode = $opcode
                    Events = [long]0; ProcessingErrors = [long]0; BinaryEvents = [long]0
                    IsTraceHeader = $isHeader; TimedNonHeaderEvents = [long]0
                    FirstNonHeaderTime = $null; LastNonHeaderTime = $null
                    Fields = @{}; UnnamedDataFields = [long]0 }
            }
            $row = $types[$key]
            $row.Events++
            if ($hasError) { $row.ProcessingErrors++ }
            if ($hasBinary) { $row.BinaryEvents++ }
            if (-not $isHeader -and $null -ne $timestamp) {
                $row.TimedNonHeaderEvents++
                if ($null -eq $row.FirstNonHeaderTime -or $timestamp -lt $row.FirstNonHeaderTime) { $row.FirstNonHeaderTime = $timestamp }
                if ($null -eq $row.LastNonHeaderTime -or $timestamp -gt $row.LastNonHeaderTime) { $row.LastNonHeaderTime = $timestamp }
            }
            $eventData = $event.Element($ns + 'EventData')
            $headerValues = [ordered]@{}
            if ($null -ne $eventData) {
                foreach ($data in $eventData.Elements($ns + 'Data')) {
                    $nameAttribute = $data.Attribute('Name')
                    $name = if ($null -eq $nameAttribute) { '' } else { $nameAttribute.Value }
                    if ([string]::IsNullOrWhiteSpace($name)) { $row.UnnamedDataFields++ } else { $row.Fields[$name] = $true }
                    # Persist values only for this explicit trace-header counter allowlist.
                    if ($isHeader -and -not $hasError -and $name -in @('EventsLost', 'BuffersLost', 'LogFileMode', 'MaxFileSize', 'MaximumFileSize', 'BuffersWritten', 'StartBuffers')) {
                        $value = $data.Value.Trim()
                        if ($value -match '^\d+$') { $headerValues[$name] = [UInt64]$value }
                        elseif ($value -match '^0x[0-9a-fA-F]+$') { $headerValues[$name] = [Convert]::ToUInt64($value.Substring(2), 16) }
                    }
                }
            }
            $userData = $event.Element($ns + 'UserData')
            if ($null -ne $userData) {
                foreach ($field in $userData.Descendants()) {
                    if (-not $field.HasElements) { $row.Fields['UserData/' + $field.Name.LocalName] = $true }
                }
            }
            if ($headerValues.Count -gt 0) {
                $headers.Add([pscustomobject]@{ Sequence = $count; ProviderGuid = $guid; Counters = [pscustomobject]$headerValues
                    Circular = if ($headerValues.Contains('LogFileMode')) { ($headerValues.LogFileMode -band 2) -ne 0 } else { $null } })
            }
        }
    } finally { $reader.Dispose() }
    if ($count -eq 0) { throw 'XML contains no ETW Event elements in the expected namespace; schema coverage is unknown.' }
    $eventTypes = @(foreach ($row in $types.Values) {
        $fields = @($row.Fields.Keys | Sort-Object)
        [pscustomobject]@{ ProviderGuid = $row.ProviderGuid; ProviderName = $row.ProviderName
            EventId = $row.EventId; Version = $row.Version; Task = $row.Task; Opcode = $row.Opcode
            Events = $row.Events; ProcessingErrors = $row.ProcessingErrors; BinaryEvents = $row.BinaryEvents
            IsTraceHeader = $row.IsTraceHeader; TimedNonHeaderEvents = $row.TimedNonHeaderEvents
            FirstNonHeaderEventUtc = if ($null -eq $row.FirstNonHeaderTime) { $null } else { $row.FirstNonHeaderTime.UtcDateTime.ToString('o') }
            LastNonHeaderEventUtc = if ($null -eq $row.LastNonHeaderTime) { $null } else { $row.LastNonHeaderTime.UtcDateTime.ToString('o') }
            FieldNames = $fields; UnnamedDataFields = $row.UnnamedDataFields
            RegisterTriageFieldNames = @($fields | Where-Object { $_ -match '(?i)(\bcci\b|\bi2c\b|\bi3c\b|\bspb\b|register(?:address|value|offset|read|write)|sensor.*(?:read|write).*reg)' }) }
    })
    $providerBounds = @(foreach ($group in @($eventTypes | Where-Object { -not $_.IsTraceHeader } | Group-Object ProviderGuid)) {
        $timedTypes = @($group.Group | Where-Object { $null -ne $_.FirstNonHeaderEventUtc })
        [pscustomobject]@{ ProviderGuid = $group.Name; ProviderNames = @($group.Group.ProviderName | Sort-Object -Unique)
            NonHeaderEvents = [long](($group.Group | Measure-Object -Property Events -Sum).Sum)
            TimedNonHeaderEvents = [long](($group.Group | Measure-Object -Property TimedNonHeaderEvents -Sum).Sum)
            FirstNonHeaderEventUtc = if ($timedTypes.Count) { ($timedTypes.FirstNonHeaderEventUtc | Sort-Object | Select-Object -First 1) } else { $null }
            LastNonHeaderEventUtc = if ($timedTypes.Count) { ($timedTypes.LastNonHeaderEventUtc | Sort-Object | Select-Object -Last 1) } else { $null } }
    })
    [pscustomobject]@{ Status = 'decoded'; Events = $count; EventElements = (Measure-CameraEtlXmlEvents -Path $Path)
        ProcessingErrors = $errors; BinaryEvents = $binaryEvents
        FirstEventUtc = if ($null -eq $first) { $null } else { $first.UtcDateTime.ToString('o') }
        LastEventUtc = if ($null -eq $last) { $null } else { $last.UtcDateTime.ToString('o') }
        Headers = $headers.ToArray(); EventTypes = @($eventTypes | Sort-Object ProviderGuid, EventId, Version, Task, Opcode)
        ProviderBoundsNonHeader = @($providerBounds | Sort-Object ProviderGuid)
        SerializedTimeOffsets = @($timeOffsets.Keys | Sort-Object)
        TimeBasis = 'tracerpt SystemTime parsed exactly as serialized, with no offset correction. Cross-check against WinEventMetadata before correlating these apparent UTC bounds to the phase.'
        TimeCoverage = 'Bounds of retained events only. Recognized trace headers are excluded from non-header bounds, but other rundown/metadata and sparse emission may remain. Full phase time coverage is not proven; continuity remains unknown.'
        CciRegisterTransactionsEstablished = $false
        CciAssessment = 'Field-name matches are triage only. Register transactions require an attributable bus/sensor, address, width, value, direction, time and completion; opaque or undecoded events remain unknown.' }
}

function Invoke-CameraEtlDecoder {
    param([Parameter(Mandatory)][string]$Executable, [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][string[]]$MonitorPaths, [int]$TimeoutSeconds,
        [long]$MaximumOutputBytes, [long]$MinimumFreeBytes)
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Executable
    $info.Arguments = ($Arguments | ForEach-Object { '"' + ([regex]::Replace($_, '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"' }) -join ' '
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new(); $process.StartInfo = $info
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $reason = $null; $exitCode = $null; $stdout = ''; $stderr = ''; $started = $false
    $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($MonitorPaths[0]))
    try {
        if ($drive.AvailableFreeSpace -lt $MinimumFreeBytes) { throw 'Insufficient free disk space before decoder start.' }
        $started = $process.Start()
        if (-not $started) { throw 'tracerpt process did not start.' }
        $outputTask = $process.StandardOutput.ReadToEndAsync()
        $errorTask = $process.StandardError.ReadToEndAsync()
        while (-not $process.WaitForExit(200)) {
            $bytes = [long]0
            foreach ($path in $MonitorPaths) { if (Test-Path -LiteralPath $path -PathType Leaf) { $bytes += (Get-Item -LiteralPath $path).Length } }
            if ($clock.Elapsed.TotalSeconds -ge $TimeoutSeconds) { $reason = 'timeout' }
            elseif ($bytes -ge $MaximumOutputBytes) { $reason = 'output-size-limit' }
            elseif ($drive.AvailableFreeSpace -lt $MinimumFreeBytes) { $reason = 'free-space-limit' }
            if ($null -ne $reason) { $process.Kill(); break }
        }
        if (-not $process.WaitForExit(5000)) { throw 'Owned decoder did not exit after termination.' }
        $exitCode = $process.ExitCode
        # A short process can finish between polling intervals. Still label an
        # oversized result incomplete even when no termination was necessary.
        if ($null -eq $reason) {
            $bytes = [long]0
            foreach ($path in $MonitorPaths) { if (Test-Path -LiteralPath $path -PathType Leaf) { $bytes += (Get-Item -LiteralPath $path).Length } }
            if ($bytes -ge $MaximumOutputBytes) { $reason = 'output-size-limit' }
            elseif ($drive.AvailableFreeSpace -lt $MinimumFreeBytes) { $reason = 'free-space-limit' }
        }
        if ($outputTask.Wait(5000)) { $stdout = $outputTask.Result }
        if ($errorTask.Wait(5000)) { $stderr = $errorTask.Result }
    } catch { $reason = 'decoder-error'; $stderr += $_.Exception.Message }
    finally {
        if ($started -and -not $process.HasExited) { try { $process.Kill() } catch {} }
        $process.Dispose(); $clock.Stop()
    }
    [pscustomobject]@{ Executable = $Executable; Arguments = $Arguments; ExitCode = $exitCode
        Completed = $exitCode -eq 0 -and $null -eq $reason; LimitOrError = $reason
        DurationSeconds = [Math]::Round($clock.Elapsed.TotalSeconds, 3); Output = $stdout; ErrorOutput = $stderr }
}

function Write-CameraEtlAuditManifest {
    param([Parameter(Mandatory)][string]$Directory)
    $Directory = (Resolve-Path -LiteralPath $Directory).Path
    $manifest = Join-Path $Directory 'audit-sha256-manifest.csv'
    # Export-Csv opens its output before an upstream pipeline enumerates files.
    # Materialize hashes first and explicitly exclude this manifest on retries.
    $rows = @(Get-ChildItem -LiteralPath $Directory -File | Where-Object { $_.FullName -ne $manifest } | Sort-Object Name | ForEach-Object {
        [pscustomobject]@{ File = $_.Name; Length = $_.Length; SHA256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    })
    $rows | Export-Csv -LiteralPath $manifest -NoTypeInformation -Encoding utf8
}

$capture = (Resolve-Path -LiteralPath $CaptureDirectory).Path
$summaryPath = Join-Path $capture 'capture-summary.json'
$manifestPath = Join-Path $capture 'sha256-manifest.csv'
if (-not (Test-Path -LiteralPath $summaryPath -PathType Leaf) -or -not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'Only completed collector directories with capture-summary.json and sha256-manifest.csv are accepted.'
}
$captureSummary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
if (-not $captureSummary.FinishedUtc) { throw 'Capture has no completion timestamp; do not analyze an active ETL.' }
$captureManifest = @(Import-Csv -LiteralPath $manifestPath)
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$auditDirectory = Join-Path $workspace ('output\camera-etl-audit-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $auditDirectory -ErrorAction Stop | Out-Null
$decoder = (Get-Command tracerpt.exe -ErrorAction Stop).Source
$report = [ordered]@{ SchemaVersion = '1.0'; StartedUtc = [DateTime]::UtcNow.ToString('o'); FinishedUtc = $null
    CaptureDirectory = $capture; CaptureFinishedUtc = $captureSummary.FinishedUtc
    CaptureCollectionStatus = $captureSummary.CollectionStatus
    CaptureManifestSHA256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
    AnalysisScriptSHA256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
    Decoder = $decoder; DecoderVersion = (Get-Item -LiteralPath $decoder).VersionInfo.FileVersion
    DecodeProviderXmlRequested = [bool]$DecodeProviderXml; Status = 'completed'; Sources = @()
    Limitations = @('All audit artifacts remain private; raw XML may contain identifiers and biometric activity.',
        'No real-time input, sessions, cameras, provider registration, symbols, uploads or CCI writes are performed.',
        'Zero lost events does not prove complete circular history; circular files can replace old events.',
        'Registered/enabled providers differ from providers that emitted events; silent providers are not proof of a broken device.',
        'Event loss, buffer loss, decoding failures, frame delivery and sensor-register coverage are separate measurements.',
        'Global timestamp bounds can include old headers/rundown. Non-header bounds still do not prove full phase coverage or continuity.',
        'Only provider ETLs receive optional bounded XML decoding. WPR remains summary-only.',
        'Output-size/free-space limits are sampled every 200ms and may overshoot; partial outputs are retained and labeled.') }
$sources = [Collections.Generic.List[object]]::new()
foreach ($etl in @(Get-ChildItem -LiteralPath $capture -File -Filter '*.etl' | Sort-Object Name)) {
    Write-Host ('Auditing closed ETL: ' + $etl.Name)
    $row = [ordered]@{ File = $etl.Name; Length = $etl.Length; SHA256 = $null; ManifestMatches = $false
        SourceUnchanged = $false; Status = 'completed'; Summary = $null
        Xml = [pscustomobject]@{ Status = 'not-requested' }; WinEventMetadata = [pscustomobject]@{ Status = 'not-requested' }
        EventCounts = $null; TimeCrossCheck = @(); RequestedProviderListStatus = 'not-applicable'; RequestedProviders = @(); Errors = @() }
    $sourceLock = $null
    try {
        # Refuse files still open for writing, and prevent writes during this audit.
        $sourceLock = [IO.File]::Open($etl.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        $row.SHA256 = (Get-FileHash -InputStream $sourceLock -Algorithm SHA256).Hash
        $manifestEntry = @($captureManifest | Where-Object { $_.RelativePath -eq $etl.Name })
        $row.ManifestMatches = $manifestEntry.Count -eq 1 -and $manifestEntry[0].SHA256 -eq $row.SHA256 -and [long]$manifestEntry[0].Length -eq $etl.Length
        if (-not $row.ManifestMatches) { throw 'ETL does not exactly match its capture manifest; no decode attempted.' }
        $prefix = Join-Path $auditDirectory $etl.BaseName
        $nativeSummary = $prefix + '.summary.txt'
        $native = Invoke-CameraEtlDecoder -Executable $decoder -Arguments @($etl.FullName, '-of', 'CSV', '-o', 'NUL', '-summary', $nativeSummary, '-y') `
            -MonitorPaths @($nativeSummary) -TimeoutSeconds $DecoderTimeoutSeconds -MaximumOutputBytes 16MB -MinimumFreeBytes ([long]$MinimumFreeMegabytes * 1MB)
        $native | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($prefix + '.summary-process.json') -Encoding utf8
        if (-not $native.Completed -or -not (Test-Path -LiteralPath $nativeSummary -PathType Leaf)) { throw 'Native summary did not complete; inspect private decoder log.' }
        $row.Summary = Read-CameraEtlSummary -Path $nativeSummary
        if ($null -eq $row.Summary.Counters.EventsLost -or $row.Summary.TableMatchesProcessed -ne $true) { $row.Status = 'partial'; $row.Errors += 'Missing counters or event-table mismatch; loss/coverage is not fully known.' }
        if ($row.Summary.Counters.EventsLost -gt 0) { $row.Status = 'partial'; $row.Errors += 'ETW reports lost events.' }
        if ($etl.Name -match '^camera-(?<phase>.+)-providers\.etl$') {
            $requestedPath = Join-Path $capture ('etw-providers-' + $Matches.phase + '.txt')
            if (Test-Path -LiteralPath $requestedPath -PathType Leaf) {
                $row.RequestedProviderListStatus = 'present'
                $row.RequestedProviders = @(foreach ($line in Get-Content -LiteralPath $requestedPath) {
                    if ($line -match '^\s*(\{[0-9a-fA-F-]{36}\})\s+') {
                        $guid = $Matches[1].ToLowerInvariant()
                        $observed = @($row.Summary.Providers | Where-Object ProviderGuid -eq $guid)
                        [pscustomobject]@{ ProviderGuid = $guid; Events = if ($observed.Count) { $observed[0].Events } else { 0 }
                            Meaning = 'Requested in saved provider list; emission count does not establish sensor-register coverage.' }
                    }
                })
            } else { $row.RequestedProviderListStatus = 'missing'; $row.Status = 'partial'; $row.Errors += 'Requested-provider list missing; enabled-provider comparison unavailable.' }
            if ($DecodeProviderXml) {
                if ($etl.Length -gt [long]$MaximumInputMegabytes * 1MB) { $row.Xml = [pscustomobject]@{ Status = 'skipped-input-size-limit' }; $row.Status = 'partial' }
                else {
                    $xmlPath = $prefix + '.private.xml'; $xmlSummary = $prefix + '.xml-summary.txt'
                    $nativeXml = Invoke-CameraEtlDecoder -Executable $decoder -Arguments @($etl.FullName, '-of', 'XML', '-o', $xmlPath, '-summary', $xmlSummary, '-y') `
                        -MonitorPaths @($xmlPath, $xmlSummary) -TimeoutSeconds $DecoderTimeoutSeconds -MaximumOutputBytes ([long]$MaximumDecodedMegabytes * 1MB) -MinimumFreeBytes ([long]$MinimumFreeMegabytes * 1MB)
                    $nativeXml | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($prefix + '.xml-process.json') -Encoding utf8
                    if (-not $nativeXml.Completed) { $row.Xml = [pscustomobject]@{ Status = 'incomplete'; Reason = $nativeXml.LimitOrError }; $row.Status = 'partial' }
                    else {
                        $row.Xml = Read-CameraEtlXmlMetadata -Path $xmlPath
                        if ($row.Xml.Events -ne $row.Summary.Counters.EventsProcessed -or $row.Xml.Events -ne $row.Xml.EventElements) {
                            $row.Status = 'partial'; $row.Errors += 'Native/XML/parser event counts differ; this is not itself evidence of ETW event loss.'
                        }
                        if ($row.Xml.ProcessingErrors -gt 0) { $row.Status = 'partial'; $row.Errors += ('XML has ' + $row.Xml.ProcessingErrors + ' ProcessingErrorData records; inspect their providers separately from ETW loss.') }
                        foreach ($header in $row.Xml.Headers) {
                            foreach ($name in @('EventsLost', 'BuffersLost')) {
                                $counter = $header.Counters.PSObject.Properties[$name]
                                if ($null -ne $counter -and $counter.Value -gt 0) {
                                    $row.Status = 'partial'; $row.Errors += ('Trace header ' + $header.Sequence + ' reports ' + $name + '=' + $counter.Value)
                                }
                            }
                        }
                        try {
                            $row.WinEventMetadata = Read-CameraEtlWinEventMetadata -Path $etl.FullName
                            $row.TimeCrossCheck = @(foreach ($provider in $row.WinEventMetadata.Providers) {
                                $xmlProvider = @($row.Xml.ProviderBoundsNonHeader | Where-Object ProviderGuid -eq $provider.ProviderGuid)
                                if ($xmlProvider.Count -eq 1 -and $null -ne $provider.FirstEventUtc -and $null -ne $xmlProvider[0].FirstNonHeaderEventUtc) {
                                    [pscustomobject]@{ ProviderGuid = $provider.ProviderGuid; CountsMatch = $provider.Events -eq $xmlProvider[0].NonHeaderEvents
                                        XmlMinusWinEventFirstSeconds = ([DateTimeOffset]::Parse($xmlProvider[0].FirstNonHeaderEventUtc) - [DateTimeOffset]::Parse($provider.FirstEventUtc)).TotalSeconds
                                        XmlMinusWinEventLastSeconds = ([DateTimeOffset]::Parse($xmlProvider[0].LastNonHeaderEventUtc) - [DateTimeOffset]::Parse($provider.LastEventUtc)).TotalSeconds }
                                }
                            })
                            if (@($row.TimeCrossCheck | Where-Object { [Math]::Abs($_.XmlMinusWinEventFirstSeconds) -gt 0.01 -or [Math]::Abs($_.XmlMinusWinEventLastSeconds) -gt 0.01 }).Count) {
                                $row.Status = 'partial'; $row.Errors += 'tracerpt XML time bounds differ from independent Get-WinEvent ETL metadata. XML offsets are preserved, not silently corrected.'
                            }
                        } catch { $row.Status = 'partial'; $row.Errors += ('Independent time metadata read failed: ' + $_.Exception.Message) }
                        $row.EventCounts = [pscustomobject]@{ NativeProcessed = $row.Summary.Counters.EventsProcessed; XmlEventElements = $row.Xml.EventElements; XmlParsed = $row.Xml.Events
                            WinEventRecords = if ($row.WinEventMetadata.Status -eq 'read') { $row.WinEventMetadata.Events } else { $null }
                            Meaning = 'Counts are separate reader views; classic infrastructure records may be exposed differently. No count is forced to match.' }
                    }
                }
            }
        }
    } catch { $row.Status = 'failed'; $row.Errors += $_.Exception.Message }
    finally {
        if ($null -ne $sourceLock) {
            try { $sourceLock.Position = 0; $row.SourceUnchanged = (Get-FileHash -InputStream $sourceLock -Algorithm SHA256).Hash -eq $row.SHA256 -and $sourceLock.Length -eq $row.Length }
            catch { $row.Errors += 'Could not re-hash the locked source.' }
            finally { $sourceLock.Dispose() }
        }
    }
    if (-not $row.SourceUnchanged) { $row.Status = 'failed'; $row.Errors += 'Source stability could not be confirmed.' }
    if ($row.Status -ne 'completed') { $report.Status = 'partial' }
    $sources.Add([pscustomobject]$row)
}
if ($sources.Count -eq 0) { $report.Status = 'no-etls' }
$report.Sources = $sources.ToArray(); $report.FinishedUtc = [DateTime]::UtcNow.ToString('o')
$report | ConvertTo-Json -Depth 18 | Set-Content -LiteralPath (Join-Path $auditDirectory 'camera-etl-audit.private.json') -Encoding utf8
@('PRIVATE: all decoder outputs and this metadata report remain local.',
  'Raw XML may contain device/user identifiers, paths, or biometric activity. Do not upload it.',
  'This directory is a separate audit; the source capture manifest was not rewritten.') |
    Set-Content -LiteralPath (Join-Path $auditDirectory 'PRIVACY-README.txt') -Encoding utf8
Write-CameraEtlAuditManifest -Directory $auditDirectory
Write-Host ('Private ETL audit: ' + $auditDirectory)
Write-Host ('Audit status: ' + $report.Status)
if ($report.Status -ne 'completed') { exit 2 }
