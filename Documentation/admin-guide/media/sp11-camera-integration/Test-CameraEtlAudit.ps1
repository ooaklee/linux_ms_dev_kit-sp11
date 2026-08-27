#Requires -Version 5.1
# Offline fixtures and harmless child-process tests; never records ETW or opens a camera.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot 'Audit-CameraEtls.ps1'
$tokens = $null; $parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors.Message -join '; ') }
foreach ($function in $ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] }, $false)) {
    . ([scriptblock]::Create($function.Extent.Text))
}
$fixtureDirectory = Join-Path ([IO.Path]::GetTempPath()) ('camera-etl-audit-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureDirectory | Out-Null
$passed = 0
function Assert-Audit { param([bool]$Condition, [string]$Message) if (-not $Condition) { throw $Message } }
function Test-Audit { param([string]$Name, [scriptblock]$Body) & $Body; $script:passed++; Write-Host ('PASS ' + $Name) }
try {
    Test-Audit 'native alternate tables are not double-counted and long descriptors survive' {
        $path = Join-Path $fixtureDirectory 'summary.txt'
        @'
Files Processed:
Total Buffers Processed 12
Total Events  Processed 7
Total Events  Lost      2
Elapsed Time            10 sec
|Event Count   Event Name           Task            Opcode          Version         Guid                                  |
|          3   Microsoft-Windows-Long-Provider-Name-And-A-Long-Task 173 LongOpcode 2 {11111111-1111-1111-1111-111111111111}|
|          4   Microsoft-Windows-Long-Provider-Name-And-A-Long-Task 174 OtherOpcode 2 {11111111-1111-1111-1111-111111111111}|
|Event Count   Event Name           Event ID        Version         Guid                                  |
|          3   Microsoft-Windows-Long-Provider-Name-And-A-Long-Task 173 2 {11111111-1111-1111-1111-111111111111}|
|          4   Microsoft-Windows-Long-Provider-Name-And-A-Long-Task 174 2 {11111111-1111-1111-1111-111111111111}|
'@ | Set-Content -LiteralPath $path -Encoding utf8
        $result = Read-CameraEtlSummary $path
        Assert-Audit ($result.Counters.EventsLost -eq 2 -and $result.Counters.EventsProcessed -eq 7) 'Native counters were not parsed.'
        Assert-Audit ($result.TableMatchesProcessed -and $result.Providers.Count -eq 1 -and $result.Providers[0].Events -eq 7) 'Provider events were not correctly aggregated.'
        Assert-Audit ($result.SelectedTable -eq 'event-id' -and $result.NativeTables.Count -eq 2 -and $result.EventTypes[0].Descriptor -like '*Long-Task*') 'Alternate tables or overwide metadata columns were lost.'
    }
    Test-Audit 'unrecognized counters stay unknown instead of zero' {
        $path = Join-Path $fixtureDirectory 'localized.txt'
        'Localized or incomplete summary' | Set-Content -LiteralPath $path
        $result = Read-CameraEtlSummary $path
        Assert-Audit ($null -eq $result.Counters.EventsLost -and $null -eq $result.Counters.EventsProcessed -and $null -eq $result.TableMatchesProcessed) 'Missing counters became known zero values.'
    }
    Test-Audit 'XML metadata keeps field names, not raw values; reports schema errors separately' {
        $path = Join-Path $fixtureDirectory 'events.xml'
        @'
<Events xmlns="http://schemas.microsoft.com/win/2004/08/events/event">
 <Event><System><Provider Guid="{68fdd900-4a3e-11d1-84f4-0000f80464e3}"/><EventID>0</EventID><Opcode>0</Opcode><TimeCreated SystemTime="2026-08-27T09:00:00Z"/></System><EventData><Data Name="EventsLost">0</Data><Data Name="BuffersLost">1</Data><Data Name="LogFileMode">0x2</Data><Data Name="LoggerName">SENSITIVE_LOGGER_NAME</Data></EventData></Event>
 <Event><System><Provider Guid="{11111111-1111-1111-1111-111111111111}" Name="FixtureProvider"/><EventID>12</EventID><Opcode>4</Opcode><Version>2</Version><Task>3</Task><TimeCreated SystemTime="2026-08-27T10:00:08Z"/></System><EventData><Data Name="DeviceId">SENSITIVE_DEVICE_SERIAL</Data><Data Name="RegisterAddress">SENSITIVE_REGISTER_VALUE</Data><Data Name="EventsLost">99</Data><Data>UNNAMED_SECRET</Data></EventData></Event>
 <Event><System><Provider Guid="{11111111-1111-1111-1111-111111111111}" Name="FixtureProvider"/><EventID>12</EventID><Opcode>4</Opcode><Version>2</Version><Task>3</Task><TimeCreated SystemTime="2026-08-27T10:00:03Z"/></System><ProcessingErrorData><Data Name="Error">PRIVATE_ERROR_PAYLOAD</Data></ProcessingErrorData><EventData><Binary>DEADBEEF</Binary></EventData><UserData><Example><UserName>PRIVATE_USERNAME</UserName></Example></UserData></Event>
 <Event><System><Provider Guid="{68fdd900-4a3e-11d1-84f4-0000f80464e3}"/><EventID>0</EventID><Opcode>0</Opcode></System><EventData><Data Name="EventsLost">0</Data><Data Name="BuffersLost">1</Data><Data Name="LogFileMode">0x2</Data></EventData></Event>
</Events>
'@ | Set-Content -LiteralPath $path -Encoding utf8
        $result = Read-CameraEtlXmlMetadata $path
        Assert-Audit ($result.Events -eq 4 -and $result.EventElements -eq 4 -and $result.ProcessingErrors -eq 1 -and $result.BinaryEvents -eq 1) 'XML element/parser/error/binary counts are wrong.'
        Assert-Audit ($result.FirstEventUtc -like '2026-08-27T09:00:00*' -and $result.LastEventUtc -like '2026-08-27T10:00:08*') 'Global timestamp bounds assumed file order.'
        Assert-Audit ($result.Headers.Count -eq 2 -and $result.Headers[0].Circular -and $result.Headers[0].Counters.BuffersLost -eq 1) 'Trace headers were not retained separately.'
        $type = @($result.EventTypes | Where-Object EventId -eq '12')[0]
        Assert-Audit ($type.FirstNonHeaderEventUtc -like '2026-08-27T10:00:03*' -and $type.LastNonHeaderEventUtc -like '2026-08-27T10:00:08*' -and $type.TimedNonHeaderEvents -eq 2) 'Non-header event-type bounds included the old trace header or assumed ordering.'
        Assert-Audit ($result.ProviderBoundsNonHeader.Count -eq 1 -and $result.ProviderBoundsNonHeader[0].NonHeaderEvents -eq 2 -and $result.ProviderBoundsNonHeader[0].FirstNonHeaderEventUtc -eq $type.FirstNonHeaderEventUtc -and $result.TimeCoverage -like '*continuity remains unknown*') 'Provider time bounds or continuity limitation are missing.'
        Assert-Audit ($type.Events -eq 2 -and $type.UnnamedDataFields -eq 1 -and 'DeviceId' -in $type.FieldNames -and 'UserData/UserName' -in $type.FieldNames) 'Fields/types were not extracted as text values.'
        Assert-Audit ('RegisterAddress' -in $type.RegisterTriageFieldNames -and -not $result.CciRegisterTransactionsEstablished) 'Triage keywords were mistaken for transaction evidence.'
        $json = $result | ConvertTo-Json -Depth 15
        Assert-Audit ($json -notmatch 'SENSITIVE_|UNNAMED_SECRET|PRIVATE_|DEADBEEF|"EventsLost":\s*99') 'A raw application value leaked into metadata output.'
    }
    Test-Audit 'WinEvent time crosscheck ignores infrastructure and never reads payload properties' {
        $script:disposedFixtureEvents = 0
        function Get-WinEvent {
            param($Path, [switch]$Oldest, $ErrorAction)
            foreach ($fixture in @(
                @('68fdd900-4a3e-11d1-84f4-0000f80464e3', '2026-08-27T09:00:00Z'),
                @('11111111-1111-1111-1111-111111111111', '2026-08-27T10:00:08Z'),
                @('11111111-1111-1111-1111-111111111111', '2026-08-27T10:00:03Z'))) {
                $record = [pscustomobject]@{ ProviderId = [guid]$fixture[0]; ProviderName = 'Fixture'; TimeCreated = [DateTime]::Parse($fixture[1], [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind) }
                $record | Add-Member -MemberType ScriptProperty -Name Properties -Value { throw 'Payload read forbidden in metadata-only reader.' }
                $record | Add-Member -MemberType ScriptProperty -Name Message -Value { throw 'Message read forbidden in metadata-only reader.' }
                $record | Add-Member -MemberType ScriptMethod -Name Dispose -Value { $script:disposedFixtureEvents++ }
                $record
            }
        }
        $result = Read-CameraEtlWinEventMetadata -Path 'fixture-only'
        Assert-Audit ($result.Events -eq 3 -and $result.TraceInfrastructureEvents -eq 1 -and $result.Providers.Count -eq 1 -and $result.Providers[0].Events -eq 2) 'Infrastructure was counted as a camera provider.'
        Assert-Audit ($result.Providers[0].FirstEventUtc -like '2026-08-27T10:00:03*' -and $result.Providers[0].LastEventUtc -like '2026-08-27T10:00:08*' -and $script:disposedFixtureEvents -eq 3) 'Independent UTC bounds or record disposal failed.'
    }
    Test-Audit 'unusual serialized timestamp offsets are preserved without guessed corrections' {
        $path = Join-Path $fixtureDirectory 'offset.xml'
        '<Events><Event xmlns="http://schemas.microsoft.com/win/2004/08/events/event"><System><Provider Guid="{11111111-1111-1111-1111-111111111111}"/><EventID>1</EventID><TimeCreated SystemTime="2026-08-27T13:55:51.000000000+00:59"/></System></Event></Events>' | Set-Content -LiteralPath $path
        $result = Read-CameraEtlXmlMetadata $path
        Assert-Audit ($result.FirstEventUtc -like '2026-08-27T12:56:51*' -and '+00:59' -in $result.SerializedTimeOffsets -and $result.TimeBasis -like '*no offset correction*') 'An unusual offset was silently corrected or hidden.'
    }
    Test-Audit 'projection CSV preserves UTC and fractional precision across JSON types and cultures' {
        $exportTokens = $null; $exportErrors = $null
        $exportAst = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'Export-CameraEtlEvidence.ps1'), [ref]$exportTokens, [ref]$exportErrors)
        Assert-Audit ($exportErrors.Count -eq 0) 'Exporter parse error.'
        $function = $exportAst.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'ConvertTo-CameraEvidenceUtc' }, $false)
        . ([scriptblock]::Create($function.Extent.Text))
        $expected = '2026-08-27T12:33:39.1234567Z'
        $fromJson = '{"Timestamp":"2026-08-27T12:33:39.1234567Z"}' | ConvertFrom-Json
        $inputs = @($expected, $fromJson.Timestamp, [DateTimeOffset]::Parse('2026-08-27T13:33:39.1234567+01:00'), ([DateTimeOffset]::Parse($expected)).UtcDateTime)
        $savedCulture = [Threading.Thread]::CurrentThread.CurrentCulture
        try {
            foreach ($culture in @('en-GB', 'fr-FR')) {
                [Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::GetCultureInfo($culture)
                $rows = @(foreach ($inputValue in $inputs) { [pscustomobject]@{ TimeUtc = ConvertTo-CameraEvidenceUtc -Value $inputValue } })
                $path = Join-Path $fixtureDirectory 'utc.csv'
                $rows | Export-Csv -LiteralPath $path -NoTypeInformation
                foreach ($row in @(Import-Csv -LiteralPath $path)) { Assert-Audit ($row.TimeUtc -ceq $expected) 'CSV lost UTC or fractional precision through locale/JSON conversion.' }
            }
        } finally { [Threading.Thread]::CurrentThread.CurrentCulture = $savedCulture }
        $failed = $false
        try { $null = ConvertTo-CameraEvidenceUtc -Value ([DateTime]::SpecifyKind([DateTime]::Now, [DateTimeKind]::Unspecified)) } catch { $failed = $true }
        Assert-Audit $failed 'Exporter guessed a timezone for an unspecified DateTime.'
    }
    Test-Audit 'DTD, wrong namespace and malformed XML fail closed' {
        foreach ($fixture in @('<!DOCTYPE Events SYSTEM "file:///private.xml"><Events/>', '<Events xmlns="wrong-namespace"><Event/></Events>', '<Events xmlns="http://schemas.microsoft.com/win/2004/08/events/event"><Event>')) {
            $path = Join-Path $fixtureDirectory 'invalid.xml'
            $fixture | Set-Content -LiteralPath $path
            $failed = $false
            try { $null = Read-CameraEtlXmlMetadata $path } catch { $failed = $true }
            Assert-Audit $failed 'Invalid/DTD XML was accepted as a complete decode.'
        }
    }
    Test-Audit 'owned native child timeout is bounded' {
        $child = Join-Path $fixtureDirectory 'sleep.ps1'
        'Start-Sleep -Seconds 8' | Set-Content -LiteralPath $child
        $result = Invoke-CameraEtlDecoder -Executable (Get-Process -Id $PID).Path -Arguments @('-NoProfile', '-NonInteractive', '-File', $child) `
            -MonitorPaths @((Join-Path $fixtureDirectory 'absent.txt')) -TimeoutSeconds 1 -MaximumOutputBytes 1MB -MinimumFreeBytes 1
        Assert-Audit (-not $result.Completed -and $result.LimitOrError -eq 'timeout' -and $result.DurationSeconds -lt 7) 'The owned child timeout did not work.'
    }
    Test-Audit 'completed oversized output is not reported as bounded success' {
        $child = Join-Path $fixtureDirectory 'write.ps1'; $output = Join-Path $fixtureDirectory 'oversized.txt'
        'param([string]$OutputPath); [IO.File]::WriteAllText($OutputPath, ("x" * 4096))' | Set-Content -LiteralPath $child
        $result = Invoke-CameraEtlDecoder -Executable (Get-Process -Id $PID).Path -Arguments @('-NoProfile', '-NonInteractive', '-File', $child, '-OutputPath', $output) `
            -MonitorPaths @($output) -TimeoutSeconds 10 -MaximumOutputBytes 512 -MinimumFreeBytes 1
        # The short-lived child can exit between the limit check and Kill(). A
        # conservative decoder-error is also safe; never accept bounded success
        # for a child that demonstrably wrote the oversized output.
        $oversized = (Test-Path -LiteralPath $output) -and (Get-Item -LiteralPath $output).Length -ge 4096
        Assert-Audit ($oversized -and -not $result.Completed -and $result.LimitOrError -in @('output-size-limit', 'decoder-error')) ('Oversized-output guard result: ' + ($result | ConvertTo-Json -Compress -Depth 3))
    }
    Test-Audit 'audit manifest hashes sources and excludes its own output on creation and retry' {
        $directory = Join-Path $fixtureDirectory 'manifest-fixture'
        New-Item -ItemType Directory -Path $directory | Out-Null
        'first-fixture' | Set-Content -LiteralPath (Join-Path $directory 'first.txt')
        'second-fixture' | Set-Content -LiteralPath (Join-Path $directory 'second.txt')
        foreach ($attempt in @(1, 2)) {
            Write-CameraEtlAuditManifest -Directory $directory
            $manifest = @(Import-Csv -LiteralPath (Join-Path $directory 'audit-sha256-manifest.csv'))
            Assert-Audit ($manifest.Count -eq 2 -and 'audit-sha256-manifest.csv' -notin $manifest.File) 'Audit manifest included or locked itself.'
            foreach ($row in $manifest) {
                Assert-Audit ($row.SHA256 -eq (Get-FileHash -LiteralPath (Join-Path $directory $row.File) -Algorithm SHA256).Hash) 'Audit source hash mismatch.'
            }
        }
    }
    Write-Host ("Passed $passed offline ETL audit tests under PowerShell " + $PSVersionTable.PSVersion)
} finally {
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureDirectory)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolvedFixture.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($resolvedFixture) -notlike 'camera-etl-audit-tests-*') { throw 'Refusing fixture cleanup outside the owned temporary directory.' }
    Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
}
