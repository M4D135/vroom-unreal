#Requires -Version 5.1
<#
.SYNOPSIS
Build Vroom for Unreal Engine 5.8 / Windows x64. No software is installed.
.EXAMPLE
.\Tools\build_windows.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8'
.EXAMPLE
.\Tools\build_windows.ps1 -EngineRoot 'D:\UE_5.8' -Mode Assets
.NOTES
Package keeps the committed assets. Assets explicitly regenerates bootstrap assets.
Entry points/arguments checked against UE 5.8 UBT and AutomationTool sources.
Native Windows build remains unverified until run on a Windows build host.
#>
[CmdletBinding()]
param(
    [string]$EngineRoot = $env:VROOM_UE_ROOT,
    [string]$ProjectDir,
    [string]$ArchiveDirectory,
    [ValidateSet('Package', 'Editor', 'Assets')][string]$Mode = 'Package',
    [ValidateSet('Development', 'Shipping')][string]$Configuration = 'Development',
    [switch]$CreateZip,
    [string]$ZipPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

function Assert-BatchPath([string]$Path) {
    # cmd.exe performs another expansion when invoking .bat. Reject exceptional
    # path characters rather than composing or evaluating an unsafe command line.
    if ($Path -match '["%!?&|<>^()\r\n]') {
        throw "Chemin incompatible avec les scripts batch Unreal : $Path"
    }
}

function Invoke-VroomTool([string]$Tool, [string[]]$ToolArguments) {
    Write-Host "Execution : $Tool"
    if ($Tool.EndsWith('.exe', [StringComparison]::OrdinalIgnoreCase)) {
        # Unreal reads its raw command line: use -key="value with spaces", not
        # "-key=value with spaces". The bootstrap paths were checked above.
        $NativeArguments = foreach ($Argument in $ToolArguments) {
            $Equals = $Argument.IndexOf('=')
            if ($Argument.StartsWith('-') -and $Equals -gt 0 -and $Argument -match '\s') {
                $Argument.Substring(0, $Equals + 1) + '"' + $Argument.Substring($Equals + 1) + '"'
            } elseif ($Argument -match '\s') { '"' + $Argument + '"' }
            else { $Argument }
        }
        $Process = Start-Process -FilePath $Tool -ArgumentList ($NativeArguments -join ' ') -NoNewWindow -Wait -PassThru
        $Code = $Process.ExitCode
    } else {
        & $Tool @ToolArguments
        $Code = $LASTEXITCODE
    }
    if ($Code -ne 0) {
        throw "Echec de $Tool (code $Code)."
    }
}

try {
    # Windows PowerShell 5.1 can bind defaults before PSScriptRoot is set.
    if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
        $ProjectDir = Split-Path -Parent $PSScriptRoot
    }
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        throw 'Ce script demande Windows x64. Utiliser build_linux.sh ou build_mac.command sur les autres systemes.'
    }
    if (-not [Environment]::Is64BitOperatingSystem) { throw 'Windows x64 est requis.' }
    if ($CreateZip -and $Mode -ne 'Package') { throw 'CreateZip demande le mode Package.' }
    if ($ZipPath -and -not $CreateZip) { throw 'ZipPath demande CreateZip.' }
    . (Join-Path $PSScriptRoot 'windows_package.ps1')
    $ProjectDir = (Resolve-Path -LiteralPath $ProjectDir).Path
    $ProjectFile = Join-Path $ProjectDir 'Vroom.uproject'
    if (-not (Test-Path -LiteralPath $ProjectFile -PathType Leaf)) { throw "Projet absent : $ProjectFile" }
    if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
        $EngineRoot = Join-Path $env:ProgramFiles 'Epic Games\UE_5.8'
    }
    $EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
    if ([string]::IsNullOrWhiteSpace($ArchiveDirectory)) {
        $ArchiveDirectory = Join-Path $ProjectDir 'Saved\Packages\Win64'
    } elseif (-not [IO.Path]::IsPathRooted($ArchiveDirectory)) {
        $ArchiveDirectory = Join-Path $ProjectDir $ArchiveDirectory
    }
    $ArchiveDirectory = [IO.Path]::GetFullPath($ArchiveDirectory)
    foreach ($Path in @($EngineRoot, $ProjectDir, $ArchiveDirectory)) { Assert-BatchPath $Path }

    $VersionFile = Join-Path $EngineRoot 'Engine\Build\Build.version'
    $Version = Get-Content -LiteralPath $VersionFile -Raw | ConvertFrom-Json
    if ($Version.MajorVersion -ne 5 -or $Version.MinorVersion -ne 8) { throw 'EngineRoot doit designer Unreal Engine 5.8.' }
    $BuildTool = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
    $AutomationTool = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
    $EditorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    foreach ($Tool in @($BuildTool, $AutomationTool, $EditorCmd)) {
        if (-not (Test-Path -LiteralPath $Tool -PathType Leaf)) { throw "Outil Windows Unreal absent : $Tool" }
    }
    $Bootstrap = Join-Path $ProjectDir 'Tools\bootstrap_assets.py'
    if ($Mode -eq 'Assets' -and -not (Test-Path -LiteralPath $Bootstrap -PathType Leaf)) {
        throw "Generateur absent : $Bootstrap"
    }
    if ($Mode -eq 'Package') {
        if ((Test-Path -LiteralPath $ArchiveDirectory) -and @(Get-ChildItem -LiteralPath $ArchiveDirectory -Force).Count) {
            throw 'ArchiveDirectory doit etre vide pour eviter un melange de builds. Choisir un nouveau dossier ; le package precedent reste conserve.'
        }
        foreach ($Asset in @('Content\Maps\Circuit.umap', 'Content\Materials\M_Asphalt.uasset', 'Content\Data\tracks.json', 'Content\Data\gloves.json', 'Content\Meshes\SM_EnvironmentBroadleaf.uasset', 'Content\Materials\M_EnvironmentGround.uasset')) {
            if (-not (Test-Path -LiteralPath (Join-Path $ProjectDir $Asset) -PathType Leaf)) {
                throw "Asset absent : $Asset. Recuperer les assets versionnes ou lancer -Mode Assets."
            }
        }
    }

    Push-Location -LiteralPath $ProjectDir
    try {
        # Build.bat <Target> <Platform> <Configuration>, as generated by UBT.
        Invoke-VroomTool $BuildTool @('VroomEditor', 'Win64', 'Development', "-Project=$ProjectFile", '-WaitMutex', '-NoHotReloadFromIDE')
        if ($Mode -eq 'Assets') {
            $LogDir = Join-Path $ProjectDir 'Saved\Logs'
            New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
            Invoke-VroomTool $EditorCmd @($ProjectFile, '-run=pythonscript', "-script=$Bootstrap", '-unattended', '-nop4', '-nosplash', "-abslog=$(Join-Path $LogDir 'VroomBootstrap-Windows.log')")
            if (-not (Test-Path -LiteralPath (Join-Path $ProjectDir 'Content\Maps\Circuit.umap') -PathType Leaf)) {
                throw 'La generation de Circuit.umap est incomplete. Consulter Saved\Logs.'
            }
        } elseif ($Mode -eq 'Package') {
            New-Item -ItemType Directory -Path $ArchiveDirectory -Force | Out-Null
            # UE 5.8 omits its platform subfolder when any archive path component
            # starts with Windows or Win64. Give UAT the package directory explicitly
            # so distribution roots such as Win64-<date> still contain Windows/.
            $UatArchiveDirectory = Join-Path $ArchiveDirectory 'Windows'
            Invoke-VroomTool $AutomationTool @('BuildCookRun', "-project=$ProjectFile", '-nop4', '-unattended', '-WaitForUATMutex', '-platform=Win64', "-clientconfig=$Configuration", '-build', '-cook', '-stage', '-pak', '-iostore', '-package', '-prereqs', '-archive', "-archivedirectory=$UatArchiveDirectory", '-utf8output')
            $Candidates = @('Windows\Vroom.exe', 'WindowsNoEditor\Vroom.exe')
            $Built = @($Candidates | ForEach-Object { Join-Path $ArchiveDirectory $_ } | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
            if ($Built.Count -eq 0) { throw "UAT termine mais lanceur Vroom.exe non trouve dans $ArchiveDirectory. Examiner la sortie UAT." }
            $NativeName = if ($Configuration -eq 'Shipping') { 'Vroom-Win64-Shipping.exe' } else { 'Vroom.exe' }
            $NativeCandidates = @("Windows\Vroom\Binaries\Win64\$NativeName", "WindowsNoEditor\Vroom\Binaries\Win64\$NativeName")
            $Native = @($NativeCandidates | Where-Object { Test-Path -LiteralPath (Join-Path $ArchiveDirectory $_) -PathType Leaf })
            if ($Native.Count -ne 1) { throw 'Le binaire natif du jeu est absent ou ambigu; aucun manifeste de validation ne sera cree.' }
            $VersionLine = @(Select-String -LiteralPath (Join-Path $ProjectDir 'Config\DefaultGame.ini') -Pattern '^ProjectVersion=(.+)$')
            if ($VersionLine.Count -ne 1) { throw 'ProjectVersion manquant ou ambigu.' }
            $SourceFiles = @(foreach ($Directory in @('Source', 'Config', 'Content')) {
                Get-ChildItem -LiteralPath (Join-Path $ProjectDir $Directory) -File -Recurse | Sort-Object FullName | ForEach-Object {
                    @{ path = $_.FullName.Substring($ProjectDir.Length + 1).Replace('\','/'); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
                }
            })
            $SourceFiles += @{ path = 'Vroom.uproject'; sha256 = (Get-FileHash -LiteralPath $ProjectFile -Algorithm SHA256).Hash.ToLowerInvariant() }
            $PackageRoot = $Native[0].Split('\')[0]
            $PackageDirectory = Join-Path $ArchiveDirectory $PackageRoot
            $Launcher = "@echo off`r`nsetlocal`r`nstart `"`" /wait `"%~dp0Vroom.exe`" -NoSplash -SaveToUserDir -d3d12 -windowed -ResX=1920 -ResY=1080`r`nexit /b %errorlevel%`r`n"
            [IO.File]::WriteAllText((Join-Path $PackageDirectory 'Jouer.cmd'), $Launcher, [Text.Encoding]::ASCII)
            $Readme = @'
VROOM - WINDOWS X64

Extraire tout le ZIP puis ouvrir Windows\Jouer.cmd (ne pas lancer depuis le ZIP).
Unreal Engine et Visual Studio ne sont pas necessaires pour jouer.
Si Windows signale un runtime Visual C++ absent, lancer le programme fourni
Engine\Extras\Redist\en-us\vc_redist.x64.exe dans ce dossier, puis reessayer.
Le lanceur commence en fenetre 1920x1080, DirectX 12. Le menu O regle la qualite.
Entree : rouler ; W/Z/fleche haut : accelerer ; A/Q/D/fleches : braquer ;
S/fleche bas : freiner ; Maj : glisse ; P : pause ; Echap : menu.
Les preferences et sauvegardes restent dans le dossier utilisateur Windows.

Ce paquet est compile ; ses essais natifs et performances sont distincts.
DLSS n'est disponible que si le plugin compatible et la carte le permettent.
Le manifeste vroom-build.json et le fichier ZIP.sha256 identifient les fichiers.
Les licences des gants Valve et des arbres sont fournies dans Licences.
'@
            [IO.File]::WriteAllText((Join-Path $PackageDirectory 'LISEZ-MOI.txt'), $Readme, [Text.Encoding]::ASCII)
            $Licences = Join-Path $PackageDirectory 'Licences'
            New-Item -ItemType Directory -Path $Licences -Force | Out-Null
            Copy-Item -LiteralPath (Join-Path $ProjectDir 'Content\ArtSource\Gloves\LICENSE') -Destination (Join-Path $Licences 'Valve-Gloves.txt')
            Copy-Item -LiteralPath (Join-Path $ProjectDir 'Content\ArtSource\TreesRealistic\LICENSE-CC0.txt') -Destination (Join-Path $Licences 'Trees-CC0.txt')
            $Manifest = @{
                schema_version = 2; version = $VersionLine[0].Matches[0].Groups[1].Value.Trim()
                engine_version = "$($Version.MajorVersion).$($Version.MinorVersion).$($Version.PatchVersion)"
                configuration = $Configuration; platform = 'Win64'; built_utc = [DateTime]::UtcNow.ToString('o')
                executable = $Native[0].Replace('\','/')
                executable_sha256 = (Get-FileHash -LiteralPath (Join-Path $ArchiveDirectory $Native[0]) -Algorithm SHA256).Hash.ToLowerInvariant()
                source_files = $SourceFiles
                package_root = $PackageRoot
                package_files = @(Get-VroomPackageFiles $ArchiveDirectory $PackageRoot)
            }
            $null = Test-VroomPackage $ArchiveDirectory $Manifest
            $Manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $ArchiveDirectory 'vroom-build.json') -Encoding UTF8
            Write-Host "Package $Configuration cree : $($Built[0])"
            Write-Host 'Manifeste natif et empreintes : vroom-build.json. Verification Windows reste a executer.'
            if ($CreateZip) {
                if ([string]::IsNullOrWhiteSpace($ZipPath)) { $ZipPath = Join-Path $ProjectDir ("dist\Vroom-$($Manifest.version)-Win64-$Configuration.zip") }
                elseif (-not [IO.Path]::IsPathRooted($ZipPath)) { $ZipPath = Join-Path $ProjectDir $ZipPath }
                Export-VroomWindowsZip $ArchiveDirectory $ZipPath
            }
        } else {
            Write-Host 'Module Editor construit. run_windows.ps1 -Editor permet de jouer sans nouveau build.'
        }
    } finally {
        Pop-Location
    }
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
