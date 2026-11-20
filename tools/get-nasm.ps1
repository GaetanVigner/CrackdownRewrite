<#
.SYNOPSIS
  Puts NASM in tools\nasm\nasm.exe, for FFmpeg's x86 assembly in Any360's XMA decoder (Any360 builds the ReXGlue SDK's
  FFmpeg itself; without NASM it builds its C version, about 20% slower a frame). tools\build.ps1 runs this when
  tools\nasm is missing; CMakeLists.txt finds NASM on the PATH first, then there.
.NOTES
  NASM 2.16.03 for 64-bit Windows from nasm.us (BSD-2-Clause). nasm.us publishes no checksums: the SHA-256 below is
  the archive's as downloaded on 2026-11-20, so a changed file is refused.
#>
param([switch]$Force)
$ErrorActionPreference = 'Stop'
$Version = '2.16.03'
$Url = "https://www.nasm.us/pub/nasm/releasebuilds/$Version/win64/nasm-$Version-win64.zip"
$Sha256 = '3ee4782247bcb874378d02f7eab4e294a84d3d15f3f6ee2de2f47a46aa7226e6'
$Dir = Join-Path $PSScriptRoot 'nasm'
$Exe = Join-Path $Dir 'nasm.exe'
if ((Test-Path -LiteralPath $Exe) -and -not $Force) {
    Write-Host "NASM: $Exe"
    return
}
$zip = Join-Path ([IO.Path]::GetTempPath()) "nasm-$Version-win64.zip"
Write-Host "NASM: downloading $Url"
Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $zip
$actual = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $Sha256) {
    Remove-Item -LiteralPath $zip -Force
    throw "nasm-$Version-win64.zip does not match its pinned SHA-256 ($actual)"
}
$unpacked = Join-Path ([IO.Path]::GetTempPath()) "nasm-$Version-win64"
if (Test-Path -LiteralPath $unpacked) { Remove-Item -LiteralPath $unpacked -Recurse -Force }
Expand-Archive -LiteralPath $zip -DestinationPath $unpacked
New-Item -ItemType Directory -Force -Path $Dir | Out-Null
Copy-Item -LiteralPath (Join-Path $unpacked "nasm-$Version\nasm.exe") -Destination $Exe -Force
Copy-Item -LiteralPath (Join-Path $unpacked "nasm-$Version\LICENSE") -Destination (Join-Path $Dir 'LICENSE') -Force
Remove-Item -LiteralPath $zip, $unpacked -Recurse -Force
Write-Host "NASM: $Exe ($(& $Exe -v))"
