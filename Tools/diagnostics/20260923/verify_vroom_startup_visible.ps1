#Requires -Version 5.1
<#
.SYNOPSIS
Targeted, visible startup validation of an existing Vroom Windows package.
.NOTES
Run in the logged-in desktop session 2, after the independent full suite has
finished. No build, download, saved-graphics change or full-suite certification.
The companion analyze_vroom_startup_visible.py must be beside this script.
Every native process has a 180-second timeout; only its own process is stopped.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateNotNullOrEmpty()][string]$ArchiveDirectory,
    [string]$ProjectDir = 'C:\Vroom-Build-20260923\Project\vroom-unreal',
    [string]$LogsRoot = 'C:\Vroom-Build-20260923\Logs',
    [string]$Python = 'C:\Vroom-Build-20260923\Tools\Python313\python.exe',
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$ExpectedVersion = '0.34.0',
    [string]$ExpectedGPU = 'RTX 5060'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

function ConvertTo-WindowsToken([string]$Value) {
    if ($Value -match '[\r\n\x00]') { throw 'Arguments must not contain newlines or NUL.' }
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    # Standard Windows argv escaping, including trailing backslashes.
    $Escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $Escaped = [regex]::Replace($Escaped, '(\\+)$', '$1$1')
    return '"' + $Escaped + '"'
}

function ConvertTo-UnrealArguments([string[]]$Values) {
    $Tokens = foreach ($Value in $Values) {
        if ($Value -match '["\r\n\x00]') { throw 'Unreal arguments must not contain quotes, newlines or NUL.' }
        $Equals = $Value.IndexOf('=')
        if ($Value.StartsWith('-') -and $Equals -gt 0 -and $Value -match '\s') {
            $Value.Substring(0, $Equals + 1) + (ConvertTo-WindowsToken $Value.Substring($Equals + 1))
        } else { ConvertTo-WindowsToken $Value }
    }
    return $Tokens -join ' '
}

function Save-Receipt {
    $Receipt.cases = @($CaseRows.ToArray())
    $Json = $Receipt | ConvertTo-Json -Depth 14
    [IO.File]::WriteAllText($ReceiptPath, $Json + "`n", (New-Object Text.UTF8Encoding($false)))
}

function Get-CsvFiles {
    $Found = @{}
    foreach ($Directory in $ProfileDirectories) {
        if (Test-Path -LiteralPath $Directory -PathType Container) {
            foreach ($File in @(Get-ChildItem -LiteralPath $Directory -Filter '*.csv' -File)) {
                $Found[$File.FullName] = $File.FullName
            }
        }
    }
    return @($Found.Values)
}

$Receipt = $null
$ReceiptPath = $null
$Manifest = $null
$ManifestPath = $null
$PackageHelperLoaded = $false
$TranscriptStarted = $false
$ExitStatus = 1

try {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        throw 'Native Windows and interactive desktop session 2 are required.'
    }
    $SessionId = (Get-Process -Id $PID).SessionId
    if ($SessionId -ne 2 -or -not [Environment]::UserInteractive) {
        throw "Run this helper in the logged-in interactive desktop session 2 (current: $SessionId)."
    }
    # Never overlap the baseline/full suite or terminate somebody else's game.
    if (@(Get-Process -Name Vroom -ErrorAction SilentlyContinue).Count -gt 0) {
        throw 'A Vroom process is already running. Wait for the current validation/game to finish.'
    }
    $ProjectDir = (Resolve-Path -LiteralPath $ProjectDir).Path
    $ArchiveDirectory = (Resolve-Path -LiteralPath $ArchiveDirectory).Path
    $Analyzer = Join-Path $PSScriptRoot 'analyze_vroom_startup_visible.py'
    foreach ($Required in @($Analyzer, (Join-Path $ProjectDir 'Tools\windows_package.ps1'),
            (Join-Path $ProjectDir 'Tools\windows_verification.py'))) {
        if (-not (Test-Path -LiteralPath $Required -PathType Leaf)) { throw "Missing helper: $Required" }
    }
    $PythonCommand = Get-Command -Name $Python -CommandType Application -ErrorAction Stop
    $PythonExecutable = $PythonCommand.Source
    $PythonPrefix = @()
    if ([IO.Path]::GetFileNameWithoutExtension($PythonExecutable) -eq 'py') { $PythonPrefix += '-3' }
    $LogsRoot = [IO.Path]::GetFullPath($LogsRoot)
    $ArchivePrefix = $ArchiveDirectory.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if ($LogsRoot.Equals($ArchiveDirectory, [StringComparison]::OrdinalIgnoreCase) -or
        $LogsRoot.StartsWith($ArchivePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Logs must be outside the immutable archive.'
    }
    New-Item -ItemType Directory -Path $LogsRoot -Force | Out-Null
    $Stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss-fff')
    $RunId = [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $OutputDirectory = Join-Path $LogsRoot ("startup-validation-$Stamp-$RunId")
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
    $ReceiptPath = Join-Path $OutputDirectory 'receipts.json'
    $CaseRows = New-Object 'System.Collections.Generic.List[object]'
    $Receipt = [ordered]@{
        scope = 'targeted-startup-validation'
        expected_version = $ExpectedVersion
        project_dir = $ProjectDir
        archive_directory = $ArchiveDirectory
        session_id = $SessionId
        started_utc = [DateTime]::UtcNow.ToString('o')
        finished_utc = $null
        package_validation_before = $false
        package_validation_after = $false
        manifest_sha256_before = $null
        manifest_sha256_after = $null
        cases = @()
        error = $null
        full_suite = $false
        six_circuit_soak = $false
        visual_review_required = $true
    }
    Save-Receipt
    Start-Transcript -LiteralPath (Join-Path $OutputDirectory 'transcript.log') -NoClobber | Out-Null
    $TranscriptStarted = $true
    . (Join-Path $ProjectDir 'Tools\windows_package.ps1')
    $PackageHelperLoaded = $true
    $ManifestPath = Join-Path $ArchiveDirectory 'vroom-build.json'
    $Manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    if ($Manifest.version -cne $ExpectedVersion -or $Manifest.configuration -cne 'Development') {
        throw 'An existing Development package matching ExpectedVersion is required.'
    }
    $Receipt.manifest_sha256_before = (Get-FileHash -LiteralPath $ManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $Executable = Test-VroomPackage $ArchiveDirectory $Manifest
    $Receipt.package_validation_before = $true
    Copy-Item -LiteralPath $ManifestPath -Destination (Join-Path $OutputDirectory 'vroom-build-before.json')
    Save-Receipt
    $WorkingDirectory = Split-Path -Parent $Executable
    $PackagedGameRoot = Split-Path -Parent (Split-Path -Parent $WorkingDirectory)
    $ProfileDirectories = @(
        (Join-Path $ProjectDir 'Saved\Profiling\CSV'),
        (Join-Path $PackagedGameRoot 'Saved\Profiling\CSV'),
        (Join-Path $env:LOCALAPPDATA 'Vroom\Saved\Profiling\CSV')
    )
    $Cases = @(
        @{ name = 'startup-normal'; flags = @('-VroomExitAfter=6'); capture_after = 3; capture_name = 'title.png'; startup = $false },
        @{ name = 'startup-frames'; flags = @('-VroomExitAfter=6'); capture_after = 3; capture_name = 'title.png'; startup = $true },
        @{ name = 'menu-flow'; flags = @('-VroomMenuTest', '-VroomExitAfter=6'); capture_after = 0; capture_name = $null; startup = $false },
        @{ name = 'start-sequence'; flags = @('-VroomStartTest', '-VroomExitAfter=6'); capture_after = 2; capture_name = 'start.png'; startup = $false },
        @{ name = 'performance'; flags = @('-VroomAutoDemo', '-VroomStartDistance=0', '-VroomStartSpeed=30',
            '-csvCaptureFrames=1800', '-csvGpuStats', '-csvNoProcessingThread', '-ExitAfterCsvProfiling',
            '-VroomMotionTrace', '-VroomExitAfter=150'); capture_after = 0; capture_name = $null; startup = $false }
    )
    Write-Host "Targeted visible startup checks, session $SessionId, 1920x1080 DX12 Ultra/60."
    Write-Host 'This run does not repeat or certify the six-circuit soak/full suite.'
    foreach ($Case in $Cases) {
        $CaseDirectory = Join-Path $OutputDirectory $Case.name
        New-Item -ItemType Directory -Path $CaseDirectory | Out-Null
        $Log = Join-Path $CaseDirectory 'game.log'
        $GameArguments = @('-d3d12', '-windowed', '-ForceRes', '-SaveToUserDir', '-ResX=1920', '-ResY=1080',
            '-VroomVerify', '-NoSplash', '-VroomGraphicsPreset=3', "-abslog=$Log",
            ('-abscrashreportclientlog=' + (Join-Path $CaseDirectory 'crash-reporter.log')))
        $GameArguments += $Case.flags
        $Capture = $null
        $StartupCaptures = @()
        if ($Case.capture_name) {
            $Capture = Join-Path $CaseDirectory $Case.capture_name
            $GameArguments += @("-VroomCapture=$Capture", ('-VroomCaptureAfter=' + $Case.capture_after))
        }
        if ($Case.startup) {
            $FirstFrames = Join-Path $CaseDirectory 'first-frames'
            New-Item -ItemType Directory -Path $FirstFrames | Out-Null
            $GameArguments += "-VroomStartupCaptureDir=$FirstFrames"
            $StartupCaptures = @(0..3 | ForEach-Object { Join-Path $FirstFrames ('startup-{0:D2}.png' -f $_) })
        }
        $CsvBefore = @{}
        if ($Case.name -eq 'performance') {
            foreach ($Path in @(Get-CsvFiles)) { $CsvBefore[$Path] = $true }
        }
        $Row = [ordered]@{
            name = $Case.name; arguments = @($GameArguments); offscreen = $false; session_id = $SessionId
            started_utc = [DateTime]::UtcNow.ToString('o'); finished_utc = $null
            pid = $null; exit = $null; timed_out = $false; error = $null
            log = $Log; capture = $Capture; startup_captures = @($StartupCaptures); csv = $null
            stdout = (Join-Path $CaseDirectory 'stdout.log'); stderr = (Join-Path $CaseDirectory 'stderr.log')
        }
        $CaseRows.Add($Row)
        Save-Receipt
        $GameProcess = $null
        try {
            Write-Host ("Starting " + $Case.name)
            $GameProcess = Start-Process -FilePath $Executable -WorkingDirectory $WorkingDirectory `
                -ArgumentList (ConvertTo-UnrealArguments $GameArguments) -PassThru `
                -RedirectStandardOutput $Row.stdout -RedirectStandardError $Row.stderr
            $Row.pid = $GameProcess.Id
            $Row.process_session_id = $GameProcess.SessionId
            if ($GameProcess.SessionId -ne 2) { throw 'The game was not started in interactive session 2.' }
            # Cache the handle before waiting, including for very short runs.
            $null = $GameProcess.Handle
            Save-Receipt
            if (-not $GameProcess.WaitForExit(180000)) {
                $Row.timed_out = $true
                $Row.error = 'The native game exceeded the 180-second timeout.'
                # Kill this exact process only; never use a process-name/task tree kill.
                $GameProcess.Kill()
                if (-not $GameProcess.WaitForExit(10000)) { throw 'Timed-out game did not stop after its own process was killed.' }
            }
            $GameProcess.Refresh()
            $Row.exit = $GameProcess.ExitCode
        } catch {
            $Row.error = $_.Exception.Message
            if ($GameProcess -and -not $GameProcess.HasExited) {
                try { $GameProcess.Kill(); $null = $GameProcess.WaitForExit(10000) } catch { }
            }
        } finally {
            $Row.finished_utc = [DateTime]::UtcNow.ToString('o')
            if ($GameProcess) { $GameProcess.Dispose() }
        }
        if ($Case.name -eq 'performance') {
            try {
                $Created = @(Get-CsvFiles | Where-Object { -not $CsvBefore.ContainsKey($_) })
                if ($Created.Count -ne 1) { throw "Expected exactly one new native CSV, found $($Created.Count)." }
                $Row.csv = Join-Path $CaseDirectory 'performance.csv'
                Copy-Item -LiteralPath $Created[0] -Destination $Row.csv
                $Row.csv_source = $Created[0]
                $Row.csv_sha256 = (Get-FileHash -LiteralPath $Row.csv -Algorithm SHA256).Hash.ToLowerInvariant()
            } catch {
                $Row.error = (@($Row.error, $_.Exception.Message) | Where-Object { $_ }) -join '; '
            }
        }
        Save-Receipt
        Write-Host ("Finished {0}: exit={1}, timeout={2}" -f $Case.name, $Row.exit, $Row.timed_out)
    }
} catch {
    if ($Receipt) { $Receipt.error = $_.Exception.Message }
    [Console]::Error.WriteLine($_.Exception.Message)
} finally {
    if ($Receipt) {
        if ($PackageHelperLoaded -and $Manifest -and $Receipt.package_validation_before) {
            try {
                $ManifestAfter = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
                $Receipt.manifest_sha256_after = (Get-FileHash -LiteralPath $ManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
                $null = Test-VroomPackage $ArchiveDirectory $ManifestAfter
                if ($Receipt.manifest_sha256_before -cne $Receipt.manifest_sha256_after) { throw 'The build manifest changed during validation.' }
                $Receipt.package_validation_after = $true
                Copy-Item -LiteralPath $ManifestPath -Destination (Join-Path $OutputDirectory 'vroom-build-after.json')
            } catch {
                $Receipt.error = (@($Receipt.error, $_.Exception.Message) | Where-Object { $_ }) -join '; '
            }
        }
        $Receipt.finished_utc = [DateTime]::UtcNow.ToString('o')
        Save-Receipt
    }
    if ($TranscriptStarted) { Stop-Transcript | Out-Null }
}

if ($ReceiptPath -and (Test-Path -LiteralPath $ReceiptPath -PathType Leaf)) {
    $AnalysisProcess = $null
    $AnalysisReceipt = [ordered]@{ started_utc = [DateTime]::UtcNow.ToString('o'); finished_utc = $null;
        pid = $null; exit = $null; timed_out = $false; error = $null }
    try {
        $AnalysisArguments = $PythonPrefix + @($Analyzer, '--project-dir', $ProjectDir,
            '--archive-directory', $ArchiveDirectory, '--receipt', $ReceiptPath,
            '--expected-version', $ExpectedVersion, '--expected-gpu', $ExpectedGPU)
        $AnalysisCommandLine = (@($AnalysisArguments | ForEach-Object { ConvertTo-WindowsToken $_ })) -join ' '
        $AnalysisProcess = Start-Process -FilePath $PythonExecutable -ArgumentList $AnalysisCommandLine `
            -WorkingDirectory $OutputDirectory -PassThru -NoNewWindow `
            -RedirectStandardOutput (Join-Path $OutputDirectory 'analysis-stdout.log') `
            -RedirectStandardError (Join-Path $OutputDirectory 'analysis-stderr.log')
        $AnalysisReceipt.pid = $AnalysisProcess.Id
        $null = $AnalysisProcess.Handle
        if (-not $AnalysisProcess.WaitForExit(180000)) {
            $AnalysisReceipt.timed_out = $true
            $AnalysisProcess.Kill()
            $null = $AnalysisProcess.WaitForExit(10000)
            throw 'The analysis exceeded the 180-second timeout.'
        }
        $AnalysisProcess.Refresh()
        $AnalysisReceipt.exit = $AnalysisProcess.ExitCode
        $ExitStatus = $AnalysisProcess.ExitCode
        foreach ($AnalysisLog in @('analysis-stdout.log', 'analysis-stderr.log')) {
            if (Test-Path -LiteralPath (Join-Path $OutputDirectory $AnalysisLog)) {
                Get-Content -LiteralPath (Join-Path $OutputDirectory $AnalysisLog) | ForEach-Object { Write-Host $_ }
            }
        }
        Write-Host ("Targeted results: " + (Join-Path $OutputDirectory 'results.json'))
        Write-Host 'Review startup/title PNGs visually. This report does not certify the full suite or six-circuit soak.'
    } catch {
        $AnalysisReceipt.error = $_.Exception.Message
        [Console]::Error.WriteLine($_.Exception.Message)
        $ExitStatus = 1
    } finally {
        if ($AnalysisProcess) {
            if (-not $AnalysisProcess.HasExited) {
                try { $AnalysisProcess.Kill(); $null = $AnalysisProcess.WaitForExit(10000) } catch { }
            }
            $AnalysisProcess.Dispose()
        }
        $AnalysisReceipt.finished_utc = [DateTime]::UtcNow.ToString('o')
        $AnalysisReceipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'analysis-process.json') -Encoding UTF8
    }
}
exit $ExitStatus
