# Crackdown: Any360 vs ReXGlue, alternating launches on cores 0-7, under the machine lock.
# Prints "=== <variant> (<log>)" lines, then per launch: hangs, errors, private MB at the end, last guest-frames lines.
param([int]$Pairs = 5, [int]$Seconds = 130, [string]$Order = 'any360,rexglue', [string]$Extra = '', [string]$Shot = '', [switch]$KeepLock)
$ErrorActionPreference = 'Stop'
$wt = 'F:\Crackdown\crackdown-any360-sys'
$Build = Join-Path $wt 'build-any360'
$logs = Join-Path $Build 'logs'
$lock = 'F:\Crackdown\any360-machine.lock'
$route = '--any360_input_script=12:start,16:a,20:a,24:a,28:a,32:a,40:a,50:a'
$variants = @{
  any360  = '--any360_gpu=true --any360_sys_groups=stateless,files,objects,threads,memory,xam,input,audio --any360_gpu_device=NVIDIA --any360_gpu_vblank_hz=1000'
  rexglue = '--any360_gpu=false --any360_sys_groups=input --any360_xdk_natives=false --any360_d3d_native_draws=false --any360_d3d_native_refcounts=false'
}
while (-not ((Test-Path $lock) -and ((Get-Content $lock -Raw) -like "*Crackdown (refresh*"))) {
  try { $f = [IO.File]::Open($lock, 'CreateNew'); $f.Close(); break } catch { Start-Sleep -Seconds 120 }
}
Set-Content -Path $lock -Value "Crackdown (refresh session local_f8c6a873, bench) $(Get-Date -Format s)" -Encoding utf8
try {
  for ($i = 0; $i -lt $Pairs; $i++) {
    foreach ($name in $Order.Split(',')) {
      $before = @(Get-ChildItem $logs -Filter *.log -ErrorAction SilentlyContinue | Select-Object -Expand Name)
      $arguments = @("--game_data_root=$wt\game\assets", "--user_data_root=$Build\user_data", '--crackdown_skip_launcher=true',
        '--crackdown_skip_intro=true', '--crackdown_frame_rate=0', '--crackdown_window_tag=cd-bench', $route) +
        ($variants[$name] -split ' ' | Where-Object { $_ }) + ($Extra -split ' ' | Where-Object { $_ })
      if ($Shot -and $i -eq 0 -and $name -eq 'any360') { $arguments += "--any360_gpu_screenshot=$Shot-$i.png" }
      $p = Start-Process -FilePath (Join-Path $Build 'crackdown.exe') -ArgumentList $arguments -WorkingDirectory $Build -WindowStyle Minimized -PassThru
      $p.ProcessorAffinity = [IntPtr]0xFF
      $private = [long]0
      for ($s = 0; $s -lt $Seconds -and -not $p.HasExited; $s++) {
        Start-Sleep -Seconds 1
        if ($Shot -and $i -eq 0 -and $name -eq 'any360' -and $s -eq $Seconds - 15) { Set-Content "$Shot-$i.png.go" 'go' }
        if ($s -ge $Seconds - 3 -and -not $p.HasExited) { $p.Refresh(); $private = [Math]::Max($private, $p.PrivateMemorySize64) }
      }
      $exited = $p.HasExited
      if (-not $exited) { Stop-Process -Id $p.Id -Force } else { "exited (code $($p.ExitCode))" }
      Start-Sleep -Seconds 3
      $log = Get-ChildItem $logs -Filter *.log | Where-Object { $before -notcontains $_.Name } | Sort-Object LastWriteTime | Select-Object -Last 1
      $l = Get-Content $log.FullName
      "=== $name ($($log.Name))"
      "private at end: $([int]($private / 1MB)) MB; hangs: " + @($l | Select-String '\[HANG\]').Count + "; errors: " + @($l | Select-String '\[error\]' | Where-Object { $_.Line -notmatch 'HANG|#[0-9]|====|crash report' }).Count
      $l | Select-String 'Crackdown: guest frames' | Select-Object -Last 3 | ForEach-Object { $x = $_.Line; $x.Substring($x.IndexOf('Crackdown:')) }
    }
  }
} finally {
  Get-CimInstance Win32_Process -Filter "name like 'crackdown%.exe'" | Where-Object { $_.CommandLine -like '*cd-bench*' -and $_.CommandLine -like "*--user_data_root=$Build\user_data*" } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force; 'killed stray' }
  if (-not $KeepLock) { Remove-Item $lock -Force } else { 'lock kept' }
}
