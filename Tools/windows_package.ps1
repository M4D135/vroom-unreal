# Shared package integrity and ZIP helpers. Compatible with Windows PowerShell 5.1.
Set-StrictMode -Version Latest

function Get-VroomPackageFiles([string]$ArchiveDirectory, [string]$PackageRoot) {
    if ($PackageRoot -notin @('Windows', 'WindowsNoEditor')) { throw 'Racine de package Windows invalide.' }
    $ArchiveDirectory = [IO.Path]::GetFullPath($ArchiveDirectory).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $Directory = Join-Path $ArchiveDirectory $PackageRoot
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) { throw 'Dossier du package absent.' }
    $Items = @(Get-Item -LiteralPath $Directory) + @(Get-ChildItem -LiteralPath $Directory -Recurse -Force)
    foreach ($Item in $Items) {
        if ($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Les liens et jonctions ne sont pas admis dans un package.' }
    }
    foreach ($File in @($Items | Where-Object { -not $_.PSIsContainer } | Sort-Object FullName)) {
        @{
            path = $File.FullName.Substring($ArchiveDirectory.Length + 1).Replace('\', '/')
            sha256 = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
}

function Test-VroomPackage([string]$ArchiveDirectory, $Manifest) {
    if ($Manifest.schema_version -ne 2 -or $Manifest.platform -ne 'Win64') { throw 'Manifeste Windows schema 2 requis.' }
    if ($Manifest.version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version de package invalide.' }
    if ($Manifest.configuration -notin @('Development', 'Shipping')) { throw 'Configuration invalide.' }
    $Current = @(Get-VroomPackageFiles $ArchiveDirectory $Manifest.package_root)
    $Expected = @{}
    foreach ($Row in $Manifest.package_files) {
        $Path = [string]$Row.path
        if (-not $Path.StartsWith($Manifest.package_root + '/', [StringComparison]::Ordinal) -or
            $Path -match '(^|/)\.\.?(/|$)|[\\:\r\n]' -or $Path -match '//' -or
            $Row.sha256 -notmatch '^[0-9a-f]{64}$' -or $Expected.ContainsKey($Path)) {
            throw 'Chemin, empreinte ou doublon invalide dans le manifeste Windows.'
        }
        $Expected[$Path] = [string]$Row.sha256
    }
    if ($Current.Count -eq 0 -or $Current.Count -ne $Expected.Count) { throw 'Liste de fichiers du package modifiee.' }
    foreach ($Row in $Current) {
        if (-not $Expected.ContainsKey($Row.path) -or $Expected[$Row.path] -cne $Row.sha256) {
            throw "Fichier du package absent ou modifie : $($Row.path)"
        }
    }
    $NativeName = if ($Manifest.configuration -eq 'Shipping') { 'Vroom-Win64-Shipping.exe' } else { 'Vroom.exe' }
    $Native = $Manifest.package_root + '/Vroom/Binaries/Win64/' + $NativeName
    if ($Manifest.executable -cne $Native -or -not $Expected.ContainsKey($Native) -or
        $Expected[$Native] -cne $Manifest.executable_sha256) { throw 'Executable natif incoherent.' }
    foreach ($Required in @('/Vroom.exe', '/Vroom/Content/Data/tracks.json', '/Vroom/Content/Data/gloves.json',
        '/Engine/Extras/Redist/en-us/vc_redist.x64.exe')) {
        if (-not $Expected.ContainsKey($Manifest.package_root + $Required)) { throw "Fichier obligatoire absent : $Required" }
    }
    if (-not @($Current | Where-Object { $_.path -like ($Manifest.package_root + '/Vroom/Content/Paks/*.ucas') }).Count) {
        throw 'Donnees Unreal cuisinees absentes.'
    }
    Join-Path $ArchiveDirectory $Native
}

function Export-VroomWindowsZip([string]$ArchiveDirectory, [string]$Destination) {
    $ManifestPath = Join-Path $ArchiveDirectory 'vroom-build.json'
    $Manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    $null = Test-VroomPackage $ArchiveDirectory $Manifest
    $Destination = [IO.Path]::GetFullPath($Destination)
    $PackagePath = [IO.Path]::GetFullPath((Join-Path $ArchiveDirectory $Manifest.package_root))
    if ($Destination.StartsWith($PackagePath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Le ZIP doit etre ecrit en dehors du package.'
    }
    if ((Test-Path -LiteralPath $Destination) -or (Test-Path -LiteralPath ($Destination + '.sha256'))) {
        throw "Distribution deja presente, choisir un nouveau chemin : $Destination"
    }
    $Parent = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Path $Parent -Force | Out-Null
    $Temporary = Join-Path $Parent ('.vroom-' + [Guid]::NewGuid().ToString('N') + '.zip')
    $ManifestHash = (Get-FileHash -LiteralPath $ManifestPath -Algorithm SHA256).Hash
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    try {
        $Stream = [IO.File]::Open($Temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite)
        try {
            $Zip = New-Object IO.Compression.ZipArchive($Stream, [IO.Compression.ZipArchiveMode]::Create, $true)
            try {
                foreach ($Row in $Manifest.package_files) {
                    $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($Zip,
                        (Join-Path $ArchiveDirectory $Row.path), $Row.path, [IO.Compression.CompressionLevel]::Optimal)
                }
                $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($Zip,
                    $ManifestPath, 'vroom-build.json', [IO.Compression.CompressionLevel]::Optimal)
            } finally { $Zip.Dispose() }
        } finally { $Stream.Dispose() }
        $null = Test-VroomPackage $ArchiveDirectory $Manifest
        if ((Get-FileHash -LiteralPath $ManifestPath -Algorithm SHA256).Hash -ne $ManifestHash) {
            throw 'Le manifeste a change pendant la creation du ZIP.'
        }
        [IO.File]::Move($Temporary, $Destination)
        $Hash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash.ToLowerInvariant()
        [IO.File]::WriteAllText($Destination + '.sha256', ($Hash + '  ' + (Split-Path -Leaf $Destination) + "`n"), [Text.Encoding]::ASCII)
        Write-Host "ZIP cree (validation native distincte) : $Destination"
        Write-Host "SHA-256 : $Hash"
    } finally {
        if (Test-Path -LiteralPath $Temporary) { Remove-Item -LiteralPath $Temporary }
    }
}
