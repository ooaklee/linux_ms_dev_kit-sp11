<# Verify private capture artifacts and emit a small, reviewed-data-only evidence summary.
No ETLs, registry exports, camera pixels, device instance IDs, or absolute source
paths are copied into the analysis directory. This does not certify parity.
#>
#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CaptureDirectory,
    [Parameter(Mandatory)][string]$StaticDirectory,
    [string]$OutputDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = $PSScriptRoot }
$captureRoot = (Resolve-Path -LiteralPath $CaptureDirectory).Path.TrimEnd('\', '/')
$staticRoot = (Resolve-Path -LiteralPath $StaticDirectory).Path
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
if ($outputRoot -eq $captureRoot -or $outputRoot.StartsWith($captureRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Analysis output must be outside the immutable capture directory.'
}
$manifestPath = Join-Path $captureRoot 'sha256-manifest.csv'
$manifest = @(Import-Csv -LiteralPath $manifestPath)
if ($manifest.Count -eq 0) { throw 'Empty capture manifest.' }
$verifiedPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $manifest) {
    if ([IO.Path]::IsPathRooted($entry.RelativePath)) { throw 'Manifest contains a rooted path.' }
    $path = [IO.Path]::GetFullPath((Join-Path $captureRoot $entry.RelativePath))
    if (-not $path.StartsWith($captureRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest escapes capture directory.' }
    if (-not $verifiedPaths.Add($path)) { throw "Duplicate manifest path: $($entry.RelativePath)" }
    $file = Get-Item -LiteralPath $path -ErrorAction Stop
    if ($file.PSIsContainer -or $file.Length -ne [long]$entry.Length -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.SHA256) {
        throw "Artifact length/hash mismatch: $($entry.RelativePath)"
    }
}
foreach ($file in Get-ChildItem -LiteralPath $captureRoot -Recurse -File) {
    if ($file.FullName -ne $manifestPath -and -not $verifiedPaths.Contains($file.FullName)) { throw "Unlisted artifact: $($file.Name)" }
}
$summary = Get-Content -LiteralPath (Join-Path $captureRoot 'capture-summary.json') -Raw | ConvertFrom-Json
$inventory = Get-Content -LiteralPath (Join-Path $captureRoot 'winrt-inventory\camera-winrt-report.json') -Raw | ConvertFrom-Json
$streams = [Collections.Generic.List[object]]::new()
foreach ($camera in @('front', 'rear')) {
    $probe = Get-Content -LiteralPath (Join-Path $captureRoot ("winrt-$camera-stream\camera-winrt-report.json")) -Raw | ConvertFrom-Json
    if ($probe.SamplesSaved -or $probe.InfraredOpened) { throw 'Unexpected sampling or IR activation in RGB evidence.' }
    foreach ($stream in $probe.Streams) {
        $streams.Add([pscustomobject][ordered]@{
            Camera = $stream.Camera; Stream = $stream.MediaStreamType
            Subtype = $stream.ActualFormat.Subtype; Width = $stream.ActualFormat.Width; Height = $stream.ActualFormat.Height
            NominalFPS = $stream.ActualFormat.NominalFramesPerSecond
            FormatMatched = $stream.FormatMatched; Status = $stream.Status
            ObservedFrameReferences = $stream.FrameCount; DurationSeconds = $stream.DurationSeconds
            ObservedFPSLowerBound = $stream.ObservedFramesPerSecond
            ErrorPresent = -not [string]::IsNullOrWhiteSpace([string]$stream.Error)
        })
    }
}
$profileRows = @(foreach ($profile in $inventory.VideoProfiles) {
    foreach ($description in $profile.Descriptions) {
        [pscustomobject][ordered]@{
            Camera = $profile.DeviceName; ProfileId = $profile.ProfileId; Stream = $description.Stream
            Width = $description.Width; Height = $description.Height; NominalFPS = $description.NominalFramesPerSecond
            Subtype = $description.Subtype; Status = 'advertised-only'
        }
    }
})
$staticManifestPath = Join-Path $staticRoot 'manifest.json'
$staticManifest = Get-Content -LiteralPath $staticManifestPath -Raw | ConvertFrom-Json
$staticFiles = @(foreach ($entry in $staticManifest.files) {
    $decodedPath = [IO.Path]::GetFullPath((Join-Path $staticRoot $entry.decodedFile))
    if (-not $decodedPath.StartsWith($staticRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Static manifest path escapes its directory.' }
    $decoded = Get-Content -LiteralPath $decodedPath -Raw | ConvertFrom-Json
    if ($decoded.sourceSha256 -ne $entry.sha256 -or $decoded.sourceLength -ne $entry.bytes) { throw 'Static decoder/manifest disagreement.' }
    if ((Get-FileHash -LiteralPath $decoded.sourcePath -Algorithm SHA256).Hash -ne $entry.sha256) { throw 'Installed static resource changed after decoding.' }
    [pscustomobject][ordered]@{
        File = $entry.file; Bytes = $entry.bytes; SHA256 = $entry.sha256; Nodes = $decoded.decoded.decodedNodeCount
        DecodedJsonSHA256 = (Get-FileHash -LiteralPath $decodedPath -Algorithm SHA256).Hash
    }
})
$metadata = $summary.Metadata
$verifiedStreams = @($streams | Where-Object {
    $_.Status -eq 'frames-observed' -and $_.FormatMatched -and $_.ObservedFrameReferences -gt 0 -and
    $_.DurationSeconds -ge ($metadata.PreviewSeconds - 0.1) -and -not $_.ErrorPresent
})
$verification = [ordered]@{
    SchemaVersion = 1; GeneratedUtc = [DateTime]::UtcNow.ToString('o')
    CaptureRun = Split-Path $captureRoot -Leaf; StaticRun = Split-Path $staticRoot -Leaf
    CaptureManifestSHA256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
    VerifiedCaptureArtifactCount = $manifest.Count
    CollectionStatus = $summary.CollectionStatus; InventoryErrorCount = @($summary.Inventory.Errors).Count
    CaptureErrorPresent = -not [string]::IsNullOrWhiteSpace([string]$summary.CaptureError)
    WindowsBuild = $metadata.WindowsBuild; PowerShellVersion = $metadata.PowerShellVersion
    CollectorSHA256 = $metadata.CollectorSHA256; SupportSHA256 = $metadata.SupportSHA256; ProbeSHA256 = $metadata.ProbeSHA256
    IsAdministrator = $metadata.IsAdministrator; WprSkipped = $metadata.SkipWpr; ProviderEtwEnabled = $metadata.ProviderEtwEnabled
    EtlFileCount = @($manifest | Where-Object RelativePath -match '\.etl$').Count
    ImageVideoAudioFileCount = @($manifest | Where-Object RelativePath -match '\.(mkv|mp4|webm|avi|mov|h264|hevc|png|jpg|jpeg|bmp|tif|tiff|dng|raw|yuv|wav|flac|mp3|aac)$').Count
    StreamAttempts = $streams.Count; VerifiedStreamAttempts = $verifiedStreams.Count
    StreamCountMeaning = 'Sequential selected RGB frame-source formats only, not every profile or raw sensor mode.'
    CameraInventoryCount = $summary.Inventory.CameraDevices; DiscoveredEtwProviders = $summary.Inventory.DiscoveredEtwProviders
    ProfileDevices = @($inventory.ProfileDevices | Select-Object DeviceName,Status,ProfileCount)
    IrOutcome = @($summary.Phases | Where-Object Phase -eq 'ir-hello' | Select-Object -ExpandProperty Outcome)
    RecorderCleanup = $summary.RecorderCleanup; StaticResources = $staticFiles
    LinuxParity = 'not-tested'; DeploymentReady = $false
}
$null = New-Item -ItemType Directory -Path $outputRoot -Force
$streams | Export-Csv -LiteralPath (Join-Path $outputRoot 'windows-rgb-stream-results.csv') -NoTypeInformation -Encoding utf8
$profileRows | Export-Csv -LiteralPath (Join-Path $outputRoot 'windows-advertised-profiles.csv') -NoTypeInformation -Encoding utf8
$inventory.Controls | Select-Object Camera,Control,Values,ReadErrors | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath (Join-Path $outputRoot 'windows-control-capabilities.json') -Encoding utf8
$verification | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputRoot 'evidence-verification.json') -Encoding utf8
Write-Host "Verified $($manifest.Count) capture artifacts, $($staticFiles.Count) static resources, $($verifiedStreams.Count)/$($streams.Count) RGB stream attempts. Linux parity remains untested."
