#Requires -Version 5.1
<#
.SYNOPSIS
Verify an existing native Windows Development package, with explicit version.
.EXAMPLE
.\Tools\verify_windows.ps1 -ExpectedVersion 0.30.0 -Profile -TargetFPS 60
.EXAMPLE
.\Tools\verify_windows.ps1 -ExpectedVersion 0.30.0 -Profile -Soak -TargetFPS 120 -ExpectedDLSS quality
.NOTES
Requires Python 3.10+ and an interactive Windows desktop. No build/download occurs.
Captures are onscreen. TargetFPS sets acceptance gates, not graphics preferences.
Prepared on Mac; Windows execution remains unverified until results.json exists.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$ExpectedVersion,
    [string]$ProjectDir = (Split-Path -Parent $PSScriptRoot),
    [string]$ArchiveDirectory,
    [string]$Python = 'py',
    [switch]$Profile,
    [switch]$Soak,
    [ValidateRange(320,16384)][int]$Width = 3840,
    [ValidateRange(200,16384)][int]$Height = 2160,
    [ValidateSet(60,120)][int]$TargetFPS = 60,
    [ValidateRange(241,1000000)][int]$Frames = 1800,
    [ValidateRange(0,100000)][int]$Warmup = 120,
    [string]$ExpectedGPU = 'RTX 5090',
    [ValidateSet('any','native','dlaa','quality','balanced','performance')][string]$ExpectedDLSS = 'any',
    [double]$MinMeanFPS = 0,
    [double]$MaxP99Ms = 0,
    [double]$MaxFrameMs = 0,
    [ValidateRange(0,1)][double]$MaxOver20MsFraction = 0.005
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
try {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'Ce controle demande Windows natif et un bureau interactif.' }
    $ProjectDir = (Resolve-Path -LiteralPath $ProjectDir).Path
    $Runner = Join-Path $ProjectDir 'Tools\windows_verification.py'
    if (-not (Test-Path -LiteralPath $Runner -PathType Leaf)) { throw "Controleur absent : $Runner" }
    $Arguments = @()
    if ([IO.Path]::GetFileNameWithoutExtension($Python) -eq 'py') { $Arguments += '-3' }
    $Arguments += @($Runner, '--project-dir', $ProjectDir, '--expected-version', $ExpectedVersion,
        '--width', "$Width", '--height', "$Height", '--target-fps', "$TargetFPS", '--frames', "$Frames", '--warmup', "$Warmup",
        '--expected-gpu', $ExpectedGPU, '--expected-dlss', $ExpectedDLSS,
        '--max-over-20-ms-fraction', $MaxOver20MsFraction.ToString([Globalization.CultureInfo]::InvariantCulture))
    if ($ArchiveDirectory) {
        if (-not [IO.Path]::IsPathRooted($ArchiveDirectory)) { $ArchiveDirectory = Join-Path $ProjectDir $ArchiveDirectory }
        $Arguments += @('--archive-directory', [IO.Path]::GetFullPath($ArchiveDirectory))
    }
    if ($Profile) { $Arguments += '--profile' }
    if ($Soak) { $Arguments += '--soak' }
    foreach ($Pair in @(@('--min-mean-fps',$MinMeanFPS), @('--max-p99-ms',$MaxP99Ms), @('--max-frame-ms',$MaxFrameMs))) {
        if ([double]::IsNaN([double]$Pair[1]) -or [double]::IsInfinity([double]$Pair[1])) { throw 'Les seuils doivent etre finis.' }
        if ([double]$Pair[1] -lt 0) { throw 'Les seuils doivent etre positifs ou zero pour les valeurs par defaut.' }
        if ([double]$Pair[1] -gt 0) { $Arguments += @($Pair[0], ([double]$Pair[1]).ToString([Globalization.CultureInfo]::InvariantCulture)) }
    }
    & $Python @Arguments
    exit $LASTEXITCODE
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
