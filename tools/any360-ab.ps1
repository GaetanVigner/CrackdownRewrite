# Launches Crackdown minimized on Any360, once per variant in the order given, $Seconds each, and prints for each the
# log it wrote ("=== <variant> (<log>)": the runs file tools/frame_lows.py of Any360 reads), its hangs and errors and
# its last frame-rate and command-processor lines. Not playable (minimized windows get no gamepad input).
#
# A variant is extra arguments; "@exe:<name> " first runs another executable of the build folder (a "before" build kept
# beside the new one). With --any360_input_script=$CityRoute the title goes from its title screen into the city.
#
#   .\tools\any360-ab.ps1 -Variants @("--any360_native_tiling=true $r", "--any360_native_tiling=false $r") -Seconds 110
# -Affinity: the processors the title may run on, as a mask (0xFF: cores 0-7), when sessions share the machine; passed
#   as --any360_cpu_affinity, which holds it on both runtimes (ReXGlue's opens the process to every processor while it
#   starts, so a mask set on the process alone is lost);
# -Device: a substring of the Vulkan device to use (--any360_gpu_device);
# -Small: the small machine (2 processors, the AMD Radeon iGPU): -Affinity 0x3 -Device Radeon, and --d3d12_adapter=1 for
#   a variant on ReXGlue's GPU (one with --any360_gpu=false).
param([string[]]$Variants = @(''), [int]$Seconds = 110, [string]$Build = '', [long]$Affinity = 0, [string]$Device = '',
      [switch]$Small)
if ($Small) {
  $Affinity = 0x3
  $Device = 'Radeon'
}
$wt = Split-Path $PSScriptRoot -Parent
if (-not $Build) { $Build = Join-Path $wt 'build-any360' }
$logs = Join-Path $Build 'logs'
foreach ($v in $Variants) {
  $before = @(Get-ChildItem $logs -Filter *.log -ErrorAction SilentlyContinue | Select-Object -Expand Name)
  $arguments = @("--game_data_root=$wt\game\assets", "--user_data_root=$Build\user_data", '--crackdown_skip_launcher=true',
    '--crackdown_skip_intro=true', '--any360_gpu=true', '--any360_sys_groups=stateless,files,objects,threads,memory,xam,input,audio',
    '--crackdown_frame_rate=0') + ($v -split ' ' | Where-Object { $_ -and $_ -notlike '@exe:*' })
  if ($Device) { $arguments += "--any360_gpu_device=$Device" }
  if ($Affinity) { $arguments += ('--any360_cpu_affinity=0x{0:X}' -f $Affinity) }
  if ($Small -and $v -match '--any360_gpu=false') { $arguments += '--d3d12_adapter=1' }
  $exe = 'crackdown.exe'
  if ($v -match '^@exe:(\S+)') { $exe = $Matches[1] }
  $p = Start-Process -FilePath (Join-Path $Build $exe) -ArgumentList $arguments -WorkingDirectory $Build -WindowStyle Minimized -PassThru
  if ($Affinity) { $p.ProcessorAffinity = [IntPtr]$Affinity }  # every thread, those already started included
  for ($i = 0; $i -lt $Seconds -and -not $p.HasExited; $i++) { Start-Sleep -Seconds 1 }
  if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } else { "exited (code $($p.ExitCode))" }
  Start-Sleep -Seconds 2
  $log = Get-ChildItem $logs -Filter *.log | Where-Object { $before -notcontains $_.Name } | Sort-Object LastWriteTime | Select-Object -Last 1
  $l = Get-Content $log.FullName
  "=== $v ($($log.Name))"
  "hangs: " + @($l | Select-String '\[HANG\]').Count + "; errors: " + @($l | Select-String '\[error\]' | Where-Object { $_.Line -notmatch 'HANG|#[0-9]|====|crash report' }).Count
  $l | Select-String 'Any360: [0-9.]+ FPS; per frame' | Select-Object -Last 2 | ForEach-Object { $x = $_.Line; $i0 = $x.IndexOf('Any360:'); $x.Substring($i0, [Math]::Min(110, $x.Length - $i0)) }
  $l | Select-String 'command processor per frame' | Select-Object -Last 1 | ForEach-Object { $x = $_.Line; $i0 = $x.IndexOf('Any360:'); $x.Substring($i0) }
}
# Only this script's launches from this build folder (their user data root), never another session's: sessions sharing
# the machine run the same script on their own build folders at the same time.
Get-CimInstance Win32_Process -Filter "name like 'crackdown%.exe'" | Where-Object { $_.CommandLine -like '*crackdown_frame_rate=0*' -and $_.CommandLine -like "*--user_data_root=$Build\user_data*" } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force; 'killed stray' }
