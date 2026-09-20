# Portable PowerShell fixture tests; no Unreal executable or Windows GPU is run.
# Run: pwsh -NoProfile -File Tests/windows_package_test.ps1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'Tools/windows_package.ps1')
$Fixture = Join-Path ([IO.Path]::GetTempPath()) ('vroom package ' + [Guid]::NewGuid().ToString('N'))
$Archive = Join-Path $Fixture 'archive'
$Count = 0
function Assert-True($Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:Count++
}
function Assert-Throws([scriptblock]$Action, [string]$Message) {
    $Failed = $false
    try { & $Action | Out-Null } catch { $Failed = $true }
    Assert-True $Failed $Message
}
try {
    foreach ($Path in @('Windows/Vroom.exe', 'Windows/Vroom/Binaries/Win64/Vroom.exe',
        'Windows/Vroom/Content/Data/tracks.json', 'Windows/Vroom/Content/Data/gloves.json',
        'Windows/Vroom/Content/Paks/Vroom.ucas', 'Windows/Engine/runtime.dll',
        'Windows/Engine/Extras/Redist/en-us/vc_redist.x64.exe')) {
        $File = Join-Path $Archive $Path
        New-Item -ItemType Directory -Path (Split-Path -Parent $File) -Force | Out-Null
        [IO.File]::WriteAllText($File, 'Synthetic fixture, not a Windows binary: ' + $Path)
    }
    $Manifest = @{
        schema_version = 2; version = '0.34.0'; platform = 'Win64'; configuration = 'Development'
        package_root = 'Windows'; executable = 'Windows/Vroom/Binaries/Win64/Vroom.exe'
        executable_sha256 = (Get-FileHash -LiteralPath (Join-Path $Archive 'Windows/Vroom/Binaries/Win64/Vroom.exe')).Hash.ToLowerInvariant()
        package_files = @(Get-VroomPackageFiles $Archive 'Windows')
    }
    $Native = Test-VroomPackage $Archive $Manifest
    Assert-True (Test-Path -LiteralPath $Native) 'Matching package must pass.'
    $Data = Join-Path $Archive 'Windows/Vroom/Content/Data/gloves.json'
    $Original = [IO.File]::ReadAllBytes($Data)
    [IO.File]::WriteAllText($Data, 'changed hands')
    Assert-Throws { Test-VroomPackage $Archive $Manifest } 'Changed data must be rejected.'
    [IO.File]::WriteAllBytes($Data, $Original)
    $Extra = Join-Path $Archive 'Windows/extra.dll'
    [IO.File]::WriteAllText($Extra, 'unexpected')
    Assert-Throws { Test-VroomPackage $Archive $Manifest } 'Added DLL must be rejected.'
    Remove-Item -LiteralPath $Extra
    $Backup = Join-Path $Fixture 'gloves.backup'
    Move-Item -LiteralPath $Data -Destination $Backup
    Assert-Throws { Test-VroomPackage $Archive $Manifest } 'Missing data must be rejected.'
    Move-Item -LiteralPath $Backup -Destination $Data
    $Rows = $Manifest.package_files
    $Manifest.package_files = @($Rows) + @(@{path='Windows/../outside.dll';sha256=('a'*64)})
    Assert-Throws { Test-VroomPackage $Archive $Manifest } 'Escaping paths must be rejected.'
    $Manifest.package_files = @($Rows) + @($Rows[0])
    Assert-Throws { Test-VroomPackage $Archive $Manifest } 'Duplicate paths must be rejected.'
    $Manifest.package_files = $Rows
    $Manifest.executable = 'Windows/Vroom.exe'
    Assert-Throws { Test-VroomPackage $Archive $Manifest } 'Bootstrap cannot replace the native executable.'
    $Manifest.executable = 'Windows/Vroom/Binaries/Win64/Vroom.exe'
    $RequiredPath = 'Windows/Engine/Extras/Redist/en-us/vc_redist.x64.exe'
    $Prerequisite = Join-Path $Archive $RequiredPath
    Move-Item -LiteralPath $Prerequisite -Destination (Join-Path $Fixture 'prerequisite.backup')
    $Manifest.package_files = @($Rows | Where-Object { $_.path -ne $RequiredPath })
    Assert-Throws { Test-VroomPackage $Archive $Manifest } 'A matching inventory without prerequisites must fail.'
    Move-Item -LiteralPath (Join-Path $Fixture 'prerequisite.backup') -Destination $Prerequisite
    $Manifest.package_files = $Rows
    $ManifestPath = Join-Path $Archive 'vroom-build.json'
    $Manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ManifestPath -Encoding UTF8
    $Destination = Join-Path $Fixture 'distribution/Vroom.zip'
    Export-VroomWindowsZip $Archive $Destination
    Assert-True (Test-Path -LiteralPath ($Destination + '.sha256')) 'ZIP checksum must exist.'
    Assert-True ((Get-Content -LiteralPath ($Destination + '.sha256')).StartsWith((Get-FileHash -LiteralPath $Destination).Hash.ToLowerInvariant())) 'ZIP checksum must match.'
    $Zip = [IO.Compression.ZipFile]::OpenRead($Destination)
    try {
        Assert-True ($Zip.Entries.Count -eq ($Rows.Count + 1)) 'ZIP must contain exactly the package and its manifest.'
        foreach ($Row in $Rows) {
            $Entry = $Zip.GetEntry($Row.path)
            Assert-True ($null -ne $Entry) ('ZIP entry missing: ' + $Row.path)
            $Stream = $Entry.Open()
            $Hasher = [Security.Cryptography.SHA256]::Create()
            try {
                $Digest = [BitConverter]::ToString($Hasher.ComputeHash($Stream)).Replace('-', '').ToLowerInvariant()
                Assert-True ($Digest -ceq $Row.sha256) ('ZIP entry changed: ' + $Row.path)
            } finally { $Stream.Dispose(); $Hasher.Dispose() }
        }
    } finally { $Zip.Dispose() }
    $Before = (Get-FileHash -LiteralPath $Destination).Hash
    Assert-Throws { Export-VroomWindowsZip $Archive $Destination } 'Existing distribution must not be overwritten.'
    Assert-True ((Get-FileHash -LiteralPath $Destination).Hash -eq $Before) 'Existing distribution must remain intact.'
    Assert-Throws { Export-VroomWindowsZip $Archive (Join-Path $Archive 'Windows/inside.zip') } 'ZIP inside package must be rejected.'
    Write-Host "$Count package fixture assertions passed. No native Windows build or game was run."
} finally {
    if (Test-Path -LiteralPath $Fixture) { Remove-Item -LiteralPath $Fixture -Recurse -Force }
}
