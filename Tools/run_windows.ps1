#Requires -Version 5.1
<#
.SYNOPSIS
Run an existing Vroom Windows package at 3840x2160/DX12 without a rebuild.
.EXAMPLE
.\Tools\run_windows.ps1
.EXAMPLE
.\Tools\run_windows.ps1 -Editor -EngineRoot 'C:\Program Files\Epic Games\UE_5.8' -Windowed
.NOTES
Extra Unreal arguments use -GameArguments @('-VroomCollisionTest=tyre','-VroomExitAfter=6').
No Windows/PowerShell execution was performed on the preparation Mac.
#>
[CmdletBinding()]
param(
    [string]$ProjectDir = (Split-Path -Parent $PSScriptRoot),
    [string]$ArchiveDirectory,
    [string]$EngineRoot = $env:VROOM_UE_ROOT,
    [switch]$Editor,
    [ValidateRange(320, 16384)][int]$Width = 3840,
    [ValidateRange(200, 16384)][int]$Height = 2160,
    [switch]$Windowed,
    [string[]]$GameArguments = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
try {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'Ce lanceur demande Windows x64.' }
    $ProjectDir = (Resolve-Path -LiteralPath $ProjectDir).Path
    $LaunchArguments = @()
    if ($Editor) {
        if ([string]::IsNullOrWhiteSpace($EngineRoot)) { $EngineRoot = Join-Path $env:ProgramFiles 'Epic Games\UE_5.8' }
        $EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
        $Executable = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
        foreach ($Required in @('Vroom.uproject', 'Binaries\Win64\UnrealEditor-Vroom.dll', 'Content\Maps\Circuit.umap')) {
            if (-not (Test-Path -LiteralPath (Join-Path $ProjectDir $Required) -PathType Leaf)) {
                throw "Fichier absent : $Required. Preparer le module/assets avec build_windows.ps1."
            }
        }
        $LaunchArguments += @((Join-Path $ProjectDir 'Vroom.uproject'), '-game')
        $WorkingDirectory = $ProjectDir
    } else {
        if ([string]::IsNullOrWhiteSpace($ArchiveDirectory)) { $ArchiveDirectory = Join-Path $ProjectDir 'Saved\Packages\Win64' }
        elseif (-not [IO.Path]::IsPathRooted($ArchiveDirectory)) { $ArchiveDirectory = Join-Path $ProjectDir $ArchiveDirectory }
        $ArchiveDirectory = [IO.Path]::GetFullPath($ArchiveDirectory)
        $Candidates = @('Windows\Vroom.exe', 'WindowsNoEditor\Vroom.exe', 'Vroom.exe')
        $Found = @($Candidates | ForEach-Object { Join-Path $ArchiveDirectory $_ } | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
        if ($Found.Count -eq 0) { throw "Package Windows absent dans $ArchiveDirectory. Lancer build_windows.ps1, ou utiliser -Editor." }
        $Executable = $Found[0]
        $WorkingDirectory = Split-Path -Parent $Executable
    }
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw "Executable absent : $Executable" }
    $LaunchArguments += @('-d3d12', "-ResX=$Width", "-ResY=$Height", '-NoSplash')
    if ($Windowed) { $LaunchArguments += '-windowed' } else { $LaunchArguments += '-fullscreen' }
    foreach ($Argument in $GameArguments) {
        if ($Argument -match '^-(ResX|ResY|windowed|fullscreen|d3d11|d3d12|dx11|dx12|vulkan)(=|$)') {
            throw 'Utiliser Width/Height/Windowed pour la presentation ; ce lanceur RTX Windows impose DX12.'
        }
    }
    $LaunchArguments += $GameArguments
    Write-Host "Vroom : $Width x $Height, DX12. Entree pour rouler, fleches/ZQSD pour conduire."
    # Start-Process explicitly waits for a GUI application. Unreal FParse reads
    # the raw command line and needs -key="value with spaces", not a fully quoted
    # "-key=value with spaces" token. Plain flags must remain unquoted.
    # No cmd.exe or Invoke-Expression evaluates these executable arguments.
    $QuotedArguments = foreach ($Argument in $LaunchArguments) {
        if ($Argument -match '["\r\n]') { throw 'Fournir chaque argument Unreal sans guillemets internes ni retour a la ligne.' }
        $Equals = $Argument.IndexOf('=')
        if ($Argument.StartsWith('-') -and $Equals -gt 0 -and $Argument -match '\s') {
            $Argument.Substring(0, $Equals + 1) + '"' + $Argument.Substring($Equals + 1) + '"'
        } elseif ($Argument -match '\s') { '"' + $Argument + '"' }
        else { $Argument }
    }
    $Process = Start-Process -FilePath $Executable -ArgumentList ($QuotedArguments -join ' ') `
        -WorkingDirectory $WorkingDirectory -NoNewWindow -Wait -PassThru
    exit $Process.ExitCode
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
