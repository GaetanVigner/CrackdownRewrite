<#
.SYNOPSIS
  Runs a battery of in-game tests in one sitting: for each run it applies settings, shows what to do in game,
  launches the game and waits for it to close; at the end it restores the settings and writes a report of every
  run's frame statistics (FPS, draws, GPU command thread and process CPU, host GPU time and its categories,
  IssueDraw sections) and errors to logs\battery-<time>.md.
.EXAMPLE
  .\tools\game-test-battery.ps1                         # the default battery
  .\tools\game-test-battery.ps1 -List                   # the batteries and their runs
  .\tools\game-test-battery.ps1 -Battery vulkan-gpu     # one battery
  .\tools\game-test-battery.ps1 -ReportOnly -Since '2026-10-29 01:00'   # report on logs already written
#>
param(
    [string]$Battery = 'vulkan-baseline',
    [string]$BuildDir,             # relative to the repository root; default: build-release-v05, else build-release
    [switch]$List,
    [switch]$ReportOnly,
    [string]$Since,                # with -ReportOnly: logs written after this time
    [int]$BusyDraws = 3000         # windows with at least this many draws a frame are summarized as the busy scene
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent

# Every run: the game's own settings file is rewritten from the player's (saved first) with these values on top.
# Diagnostics stay off except the frame statistics, which the report reads; detailed logging must stay on, or the
# statistics lines are filtered out.
$Common = [ordered]@{
    crackdown_hle_records = 'false'; crackdown_hle_capture = 'false'; crackdown_hle_auto_capture = '0'
    crackdown_debug_profiler = 'false'; crackdown_physics_telemetry = 'false'; gpu_draw_trace_resolve_data = 'false'
    crackdown_detailed_logging = 'true'; gpu_frame_stats = 'true'
}
$Vulkan = [ordered]@{ gpu_backend = '"vulkan"'; vulkan_readback_resolve = 'true' }

$CitySpot = 'Load the save, go to the usual city spot and stand still for 40 s; then drive around for about a minute.'
$CityAb = 'Load the save, go to the usual city spot and stand still for 70 s (the setting alternates every 10 s).'
function Ab([string]$Cvar, [string]$Values) { [ordered]@{ gpu_ab_cvar = "`"$Cvar`""; gpu_ab_values = "`"$Values`"" } }
$Batteries = [ordered]@{
    'vulkan-baseline' = @(
        @{ Name = 'Vulkan'; Settings = $Vulkan
           Do = "In the Agency garage, walk around the parking markers (they must not show through walls). $CitySpot" },
        @{ Name = 'D3D12 reference'; Settings = [ordered]@{ gpu_backend = '"d3d12"' }; Do = $CitySpot }
    )
    'vulkan-gpu' = @(
        @{ Name = 'Vulkan'; Settings = $Vulkan; Do = $CitySpot },
        @{ Name = 'Vulkan, no float24 depth conversion (markers wrong)'
           Settings = $Vulkan + [ordered]@{ depth_float24_convert_in_pixel_shader = 'false' }; Do = $CitySpot },
        @{ Name = 'Vulkan, native 2x MSAA'
           Settings = $Vulkan + [ordered]@{ crackdown_msaa_compatibility = 'false' }; Do = $CitySpot },
        @{ Name = 'Vulkan, resolution scale 1'; Settings = $Vulkan + [ordered]@{ resolution_scale = '1' }; Do = $CitySpot }
    )
    # One launch per setting, alternated every 10 s window in the same scene (SDK patch 0118).
    'vulkan-gpu-ab' = @(
        @{ Name = 'A/B anisotropic filtering override 3 / 0'; Settings = $Vulkan + (Ab 'anisotropic_override' '3,0'); Do = $CityAb },
        @{ Name = 'A/B gamma render targets as UNORM16 / 8-bit'; Settings = $Vulkan + (Ab 'gamma_render_target_as_unorm16' 'true,false'); Do = $CityAb },
        @{ Name = 'A/B occlusion queries on / off (markers may show through walls when off)'; Settings = $Vulkan + (Ab 'occlusion_query_enable' 'true,false'); Do = $CityAb },
        @{ Name = 'A/B dynamic rendering / render passes'; Settings = $Vulkan + (Ab 'vulkan_dynamic_rendering' 'true,false'); Do = $CityAb }
    )
}

if ($List) {
    foreach ($name in $Batteries.Keys) {
        Write-Host $name
        foreach ($run in $Batteries[$name]) { Write-Host "  - $($run.Name)" }
    }
    return
}
if (-not $Batteries.Contains($Battery)) { throw "No battery '$Battery' (see -List)" }

if (-not $BuildDir) {
    $BuildDir = if (Test-Path -LiteralPath (Join-Path $Root 'build-release-v05\crackdown.exe')) { 'build-release-v05' } else { 'build-release' }
}
$Build = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $Root $BuildDir }
$Exe = Join-Path $Build 'crackdown.exe'
$Logs = Join-Path $Build 'logs'
$SettingsFile = Join-Path $Build 'user_data\settings.toml'
if (-not (Test-Path -LiteralPath $Exe)) { throw "No game at $Exe (build it with tools\build.ps1 -Config Release)" }

function Write-Settings([System.Collections.Specialized.OrderedDictionary]$Values, [string[]]$PlayerLines, [string]$Title) {
    $lines = @($PlayerLines | Where-Object { -not $Values.Contains((($_ -split '=', 2)[0]).Trim()) })
    $lines += ''
    $lines += "# game-test-battery: $Title"
    foreach ($key in $Values.Keys) { $lines += "$key = $($Values[$key])" }
    [IO.File]::WriteAllLines($SettingsFile, [string[]]$lines, (New-Object Text.UTF8Encoding $false))
}

# --- report ---------------------------------------------------------------------------------------------------

function Get-Median([double[]]$Values) {
    if (-not $Values -or $Values.Count -eq 0) { return $null }
    $sorted = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2) { return $sorted[$middle] }
    return ($sorted[$middle - 1] + $sorted[$middle]) / 2
}

function Format-Number($Value, [int]$Digits = 1) {
    if ($null -eq $Value) { return '-' }
    return ([double]$Value).ToString("F$Digits", [Globalization.CultureInfo]::InvariantCulture)
}

# One 10 s window per gpu_frame_stats line; the Vulkan lines that follow it in the log belong to the same window.
function Read-Windows([string]$LogPath) {
    $windows = New-Object System.Collections.Generic.List[object]
    $current = $null
    $culture = [Globalization.CultureInfo]::InvariantCulture
    foreach ($line in [IO.File]::ReadLines($LogPath)) {
        if ($line -match 'Frame stats over [\d.]+ s at ([\d.]+) FPS, per frame: (\d+) draws') {
            $current = [ordered]@{ Fps = [double]::Parse($Matches[1], $culture); Draws = [int]$Matches[2] }
            $windows.Add($current)
        } elseif ($null -ne $current -and $line -match 'Vulkan CPU per frame over .*: [\d.]+ FPS, frame ([\d.]+) ms, command processor thread ([\d.]+) ms, submission thread ([\d.]+) ms') {
            $current.FrameMs = [double]::Parse($Matches[1], $culture)
            $current.CommandThreadMs = [double]::Parse($Matches[2], $culture)
            $current.SubmissionThreadMs = [double]::Parse($Matches[3], $culture)
            if ($line -match '\[A/B [^=]+=([^\]]+)\]') { $current.Ab = $Matches[1] }
        } elseif ($null -ne $current -and $line -match 'Vulkan render pass ends per frame.*: barriers ([\d.]+), forced \(copies, dispatches\) ([\d.]+), render target changes ([\d.]+), occlusion queries ([\d.]+), submissions ([\d.]+), other ([\d.]+)') {
            $current.PassEnds = [ordered]@{
                'barriers' = [double]::Parse($Matches[1], $culture); 'forced' = [double]::Parse($Matches[2], $culture)
                'render target changes' = [double]::Parse($Matches[3], $culture); 'occlusion queries' = [double]::Parse($Matches[4], $culture)
                'submissions' = [double]::Parse($Matches[5], $culture); 'other' = [double]::Parse($Matches[6], $culture)
            }
        } elseif ($null -ne $current -and $line -match 'Vulkan IssueDraw per frame.*: \d+ draws, ([\d.]+) ms \((\d+) ns a draw\), (\d+) render pass entries; ns a draw: (.*)$') {
            $current.IssueDrawMs = [double]::Parse($Matches[1], $culture)
            $current.IssueDrawNs = [double]$Matches[2]
            $current.RenderPasses = [double]$Matches[3]
            $sections = [ordered]@{}
            foreach ($part in ($Matches[4] -split ', ')) {
                if ($part -match '^(.*) (\d+)$') { $sections[$Matches[1]] = [double]$Matches[2] }
            }
            $current.Sections = $sections
        } elseif ($null -ne $current -and $line -match 'whole process CPU ([\d.]+) ms, host GPU ([\d.]+) ms') {
            $current.ProcessCpuMs = [double]::Parse($Matches[1], $culture)
            $current.GpuMs = [double]::Parse($Matches[2], $culture)
        } elseif ($null -ne $current -and $line -match 'Vulkan GPU by category per frame.*: draw passes ([\d.]+) ms, render target updates ([\d.]+) ms, resolves ([\d.]+) ms, swap ([\d.]+) ms, other ([\d.]+) ms') {
            $current.GpuCategories = [ordered]@{
                'draw passes' = [double]::Parse($Matches[1], $culture); 'render target updates' = [double]::Parse($Matches[2], $culture)
                'resolves' = [double]::Parse($Matches[3], $culture); 'swap' = [double]::Parse($Matches[4], $culture)
                'other' = [double]::Parse($Matches[5], $culture)
            }
        }
    }
    return $windows
}

function Get-LogFacts([string]$LogPath) {
    $text = [IO.File]::ReadAllText($LogPath)
    $backend = if ($text -match 'Crackdown port \S+ \([^,]+, ([^,]+),') { $Matches[1] } else { '?' }
    # Known harmless: the cache device probes, and the Vulkan loader's informational messages.
    $errors = @([regex]::Matches($text, '(?m)^.*\[error\].*$') | ForEach-Object { $_.Value } |
        Where-Object { $_ -notmatch 'xuifrontend|ResolvePath\(cache' })
    [pscustomobject]@{
        Backend = $backend
        Errors = $errors
        Crashed = $text.Contains('CRASH REPORT')
        DeviceLost = $text -match '(?i)device lost|DEVICE_LOST'
    }
}

function Write-Report($Runs, [string]$Path) {
    $out = New-Object System.Collections.Generic.List[string]
    $out.Add("# Game test battery report")
    $out.Add('')
    $out.Add("Written $(Get-Date -Format 'yyyy-MM-dd HH:mm'). Busy scene = windows with at least $BusyDraws draws a frame; values are medians over them.")
    $out.Add('')
    $out.Add('| Run | Backend | Busy windows | Draws | FPS | Frame ms | GPU cmd thread ms | Process CPU ms | Host GPU ms | IssueDraw ns/draw | Errors |')
    $out.Add('|---|---|---|---|---|---|---|---|---|---|---|')
    foreach ($run in $Runs) {
        $busy = @($run.Windows | Where-Object { $_.Draws -ge $BusyDraws })
        $median = { param($key) Get-Median ([double[]]@($busy | Where-Object { $null -ne $_[$key] } | ForEach-Object { $_[$key] })) }
        $errors = if ($run.Facts) { $run.Facts.Errors.Count } else { '-' }
        if ($run.Facts -and ($run.Facts.Crashed -or $run.Facts.DeviceLost)) { $errors = "$errors (CRASH/DEVICE LOST)" }
        $out.Add("| $($run.Name) | $($run.Facts.Backend) | $($busy.Count) | $(Format-Number (& $median 'Draws') 0) | $(Format-Number (& $median 'Fps')) | $(Format-Number (& $median 'FrameMs') 2) | $(Format-Number (& $median 'CommandThreadMs') 2) | $(Format-Number (& $median 'ProcessCpuMs') 2) | $(Format-Number (& $median 'GpuMs') 2) | $(Format-Number (& $median 'IssueDrawNs') 0) | $errors |")
    }
    foreach ($run in $Runs) {
        $out.Add('')
        $out.Add("## $($run.Name)")
        $out.Add('')
        $out.Add("Logs: $(($run.Logs | ForEach-Object { Split-Path $_ -Leaf }) -join ', ')")
        $busy = @($run.Windows | Where-Object { $_.Draws -ge $BusyDraws })
        $categoryWindows = @($busy | Where-Object { $_.GpuCategories })
        if ($categoryWindows.Count) {
            $out.Add('')
            $out.Add('Host GPU by category (ms a frame, busy median): ' + (($categoryWindows[0].GpuCategories.Keys | ForEach-Object {
                $key = $_; "$key $(Format-Number (Get-Median ([double[]]@($categoryWindows | ForEach-Object { $_.GpuCategories[$key] }))) 2)"
            }) -join ', '))
        }
        $endWindows = @($busy | Where-Object { $_.PassEnds })
        if ($endWindows.Count) {
            $out.Add('')
            $out.Add('Render pass ends a frame (busy median): ' + (($endWindows[0].PassEnds.Keys | ForEach-Object {
                $key = $_; "$key $(Format-Number (Get-Median ([double[]]@($endWindows | ForEach-Object { $_.PassEnds[$key] }))) 1)"
            }) -join ', '))
        }
        $abWindows = @($busy | Where-Object { $_.Ab })
        if ($abWindows.Count) {
            $out.Add('')
            $out.Add('| A/B value | Busy windows | Draws | FPS | Frame ms | GPU cmd ms | Process CPU ms | Host GPU ms |')
            $out.Add('|---|---|---|---|---|---|---|---|')
            foreach ($group in ($abWindows | Group-Object { $_.Ab })) {
                $g = @($group.Group)
                $m = { param($key) Get-Median ([double[]]@($g | Where-Object { $null -ne $_[$key] } | ForEach-Object { $_[$key] })) }
                $out.Add("| $($group.Name) | $($g.Count) | $(Format-Number (& $m 'Draws') 0) | $(Format-Number (& $m 'Fps')) | $(Format-Number (& $m 'FrameMs') 2) | $(Format-Number (& $m 'CommandThreadMs') 2) | $(Format-Number (& $m 'ProcessCpuMs') 2) | $(Format-Number (& $m 'GpuMs') 2) |")
            }
        }
        $sectionWindows = @($busy | Where-Object { $_.Sections })
        if ($sectionWindows.Count) {
            $out.Add('')
            $out.Add('IssueDraw sections (ns a draw, busy median): ' + (($sectionWindows[0].Sections.Keys | ForEach-Object {
                $key = $_; "$key $(Format-Number (Get-Median ([double[]]@($sectionWindows | ForEach-Object { $_.Sections[$key] }))) 0)"
            }) -join ', '))
        }
        $out.Add('')
        $out.Add('| Window | Draws | FPS | Frame ms | GPU cmd ms | Process CPU ms | Host GPU ms |')
        $out.Add('|---|---|---|---|---|---|---|')
        $index = 0
        foreach ($window in $run.Windows) {
            ++$index
            $out.Add("| $index | $($window.Draws) | $(Format-Number $window.Fps) | $(Format-Number $window.FrameMs 2) | $(Format-Number $window.CommandThreadMs 2) | $(Format-Number $window.ProcessCpuMs 2) | $(Format-Number $window.GpuMs 2) |")
        }
        if ($run.Facts -and $run.Facts.Errors.Count) {
            $out.Add('')
            $out.Add('Errors:')
            foreach ($line in ($run.Facts.Errors | Select-Object -First 20)) { $out.Add("    $line") }
        }
    }
    [IO.File]::WriteAllLines($Path, [string[]]$out, (New-Object Text.UTF8Encoding $false))
}

function New-RunResult([string]$Name, [string[]]$LogPaths) {
    $windows = New-Object System.Collections.Generic.List[object]
    $facts = $null
    foreach ($log in $LogPaths) {
        foreach ($window in (Read-Windows $log)) { $windows.Add($window) }
        $logFacts = Get-LogFacts $log
        if (-not $facts -or $logFacts.Backend -ne '?') { $facts = $logFacts }
    }
    [pscustomobject]@{ Name = $Name; Logs = $LogPaths; Windows = $windows; Facts = $facts }
}

$reportPath = Join-Path $Logs ("battery-{0}.md" -f (Get-Date -Format 'yyyyMMdd-HHmm'))

if ($ReportOnly) {
    $since = if ($Since) { [datetime]::Parse($Since) } else { (Get-Date).AddHours(-12) }
    $results = @(Get-ChildItem -LiteralPath $Logs -Filter 'crackdown_*.log' | Where-Object { $_.LastWriteTime -ge $since } |
        Sort-Object LastWriteTime | ForEach-Object { New-RunResult $_.BaseName @($_.FullName) })
    Write-Report $results $reportPath
    Write-Host "Report: $reportPath"
    return
}

if (Get-Process -Name 'crackdown' -ErrorAction SilentlyContinue) { throw 'Close the game first.' }
$playerLines = [IO.File]::ReadAllLines($SettingsFile)
$backup = "$SettingsFile.battery-backup"
[IO.File]::WriteAllLines($backup, $playerLines, (New-Object Text.UTF8Encoding $false))
$results = New-Object System.Collections.Generic.List[object]
try {
    $runs = $Batteries[$Battery]
    $number = 0
    foreach ($run in $runs) {
        ++$number
        $values = [ordered]@{}
        foreach ($key in $Common.Keys) { $values[$key] = $Common[$key] }
        foreach ($key in $run.Settings.Keys) { $values[$key] = $run.Settings[$key] }
        Write-Settings $values $playerLines "$Battery $number/$($runs.Count): $($run.Name)"
        Write-Host ''
        Write-Host "=== Run $number/$($runs.Count): $($run.Name) ===" -ForegroundColor Cyan
        Write-Host $run.Do -ForegroundColor Yellow
        Write-Host 'Close the game when done; the next run starts by itself.'
        $before = @(Get-ChildItem -LiteralPath $Logs -Filter 'crackdown_*.log' -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
        Start-Process -FilePath $Exe -WorkingDirectory $Build -Wait
        $after = @(Get-ChildItem -LiteralPath $Logs -Filter 'crackdown_*.log' | Sort-Object LastWriteTime | ForEach-Object { $_.FullName })
        $new = @($after | Where-Object { $before -notcontains $_ })
        if (-not $new.Count) { $new = @($after | Select-Object -Last 1) }
        $results.Add((New-RunResult $run.Name $new))
    }
} finally {
    [IO.File]::WriteAllLines($SettingsFile, $playerLines, (New-Object Text.UTF8Encoding $false))
    Remove-Item -LiteralPath $backup -ErrorAction SilentlyContinue
    Write-Host 'Player settings restored.'
}
Write-Report $results $reportPath
Write-Host "Report: $reportPath" -ForegroundColor Green
