<#
.SYNOPSIS
  Downloads the pinned ReXGlue SDK, checks game\assets\Default.xex and
  generates the recompiled C++ sources into generated\default. This is the only entry point that regenerates them.
  When a patch in patches\rexglue-sdk changes the codegen, it generates with a codegen tool built from the patches.

  -Build tu6 recompiles Title Update 6 instead, into generated\tu6 (tools\codegen-tu6.ps1 does that). Its input is
  Default.xex with Default.xexp applied, made in out\tu6\game: -UpdatePackage names the update's Xbox 360 package, and the
  patch is taken from it with the port's own installer. Without the parameter an earlier out\tu6\game\Default.xexp is used.
#>
param(
    [ValidateSet('default', 'tu6')]
    [string]$Build = 'default',
    [string]$UpdatePackage = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'build-common.ps1')
. (Join-Path $PSScriptRoot 'codegen-inputs.ps1')
$spec = $CodegenBuilds[$Build]

$baseline = Read-KeyValueFile (Join-Path $root 'patches\rexglue-sdk\baseline.env')
$sdkVersion = $baseline['SDK_VERSION']
$sdkDir = Join-Path $root 'tools\rexglue'
$rexglue = Join-Path $sdkDir 'win-amd64\bin\rexglue.exe'
$xex = Join-Path $root $CrackdownXexPath
$log = Join-Path $root $spec.Log
$stamp = Join-Path $root $spec.Stamp
if (Test-Path $stamp) { Remove-Item -LiteralPath $stamp }

if (-not (Test-Path $rexglue)) {
    $zip = Join-Path $sdkDir "rexglue-sdk-$sdkVersion-win-amd64.zip"
    if (-not (Test-Path $zip)) {
        New-Item -ItemType Directory -Force $sdkDir | Out-Null
        $url = "https://github.com/rexglue/rexglue-sdk/releases/download/v$sdkVersion/rexglue-sdk-$sdkVersion-win-amd64.zip"
        Write-Host "Downloading ReXGlue SDK $sdkVersion..."
        Invoke-WebRequest -Uri $url -OutFile $zip
    }
    if ((Get-FileSha256 $zip) -ne $baseline['SDK_ZIP_SHA256']) { throw "$zip is not the official ReXGlue $sdkVersion release" }
    Expand-Archive -Path $zip -DestinationPath $sdkDir -Force
}
$installed = "$(& $rexglue --version)".Trim()
if ($installed -ne $sdkVersion) { throw "tools\rexglue has ReXGlue $installed; expected $sdkVersion (delete tools\rexglue to re-download)" }
if ((Get-FileSha256 $rexglue) -ne $baseline['SHA256_bin/rexglue.exe']) {
    throw "$rexglue is not the stock ReXGlue $sdkVersion codegen (delete tools\rexglue to re-download)"
}
$codegenTool = Get-CodegenToolIdentity $root
if ($codegenTool -ne 'stock') {
    $rexglue = Get-PatchedCodegenPath $root $codegenTool
    $runtime = Join-Path (Split-Path $rexglue) 'rexruntime.dll'
    if (-not (Test-Path -LiteralPath $rexglue -PathType Leaf) -or -not (Test-Path -LiteralPath $runtime -PathType Leaf)) {
        Write-Host "Building the ReXGlue codegen from patches\rexglue-sdk ($codegenTool)..."
        & (Get-Process -Id $PID).Path -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build-patched-rexruntime.ps1') -Codegen
        if ($LASTEXITCODE) { throw "Building the patched ReXGlue codegen failed ($LASTEXITCODE)" }
    }
    Write-Host "Using the patched ReXGlue codegen ($codegenTool)."
}

if (-not (Test-Path $xex)) { throw "Missing $xex (copy the disc contents into game\assets first)" }
$hash = Get-FileSha256 $xex
if ($hash -ne $CrackdownXexSha256) { throw "Unsupported Default.xex (SHA-256 $hash). Expected $CrackdownXexSha256 (Crackdown TU0)." }
Write-Host 'Default.xex is the supported version.'

if ($Build -eq 'tu6') {
    $inputDir = Join-Path $root 'out\tu6\game'
    $xexp = Join-Path $root $spec.Update
    New-Item -ItemType Directory -Force $inputDir | Out-Null
    Copy-Item -LiteralPath $xex -Destination (Join-Path $root $spec.Xex) -Force
    if ($UpdatePackage) {
        $toolchain = Get-BuildToolchain
        $savedEnv = Save-BuildEnvironment
        $tool = Join-Path $root 'out\tests\crackdown_install_update.exe'
        try {
            Import-BuildEnvironment $toolchain
            & $toolchain.CMake -S (Join-Path $root 'tests') -B (Join-Path $root 'out\tests') -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
                "-DCMAKE_CXX_COMPILER:FILEPATH=$($toolchain.ClangXX)" "-DCMAKE_LINKER:FILEPATH=$($toolchain.LldLink)" `
                "-DCMAKE_MAKE_PROGRAM:FILEPATH=$($toolchain.Ninja)" | Out-Host
            if ($LASTEXITCODE) { throw "CMake configure failed ($LASTEXITCODE)" }
            & $toolchain.CMake --build (Join-Path $root 'out\tests') --target crackdown_install_update | Out-Host
            if ($LASTEXITCODE) { throw "Building the update installer failed ($LASTEXITCODE)" }
        } finally { Restore-BuildEnvironment $savedEnv }
        $updateRoot = Join-Path $root 'out\tu6\user_data'
        & $tool $UpdatePackage $updateRoot | Out-Host
        if ($LASTEXITCODE) { throw "$UpdatePackage is not a supported Title Update 6 package" }
        Copy-Item -LiteralPath (Join-Path $updateRoot 'title_update\4D5307DC\Default.xexp') -Destination $xexp -Force
    } elseif (-not (Test-Path -LiteralPath $xexp)) {
        throw "Pass -UpdatePackage with the Title Update 6 package: there is no $xexp yet"
    }
    if ((Get-FileSha256 $xexp) -ne $CrackdownUpdateXexpSha256) { throw "$xexp is not Title Update 6's Default.xexp" }
    Write-Host 'Default.xexp is the supported update.'
}

New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
Push-Location $root
try {
    # rexglue logs to stderr; in Windows PowerShell 5.1 that would trip 'Stop', so judge by exit code.
    $ErrorActionPreference = 'Continue'
    $timer = [Diagnostics.Stopwatch]::StartNew()
    # ReXGlue's module fingerprint excludes the codegen binary, so a patched codegen would otherwise be skipped.
    & $rexglue codegen --ignore-stamp $spec.Manifest 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log
    $exitCode = $LASTEXITCODE
    Write-Host ("codegen exit code {0} after {1:N1} s (log: {2})" -f $exitCode, $timer.Elapsed.TotalSeconds, $log)
    if ($exitCode -ne 0) { throw "codegen failed ($exitCode)" }
    [IO.File]::WriteAllText($stamp, (Get-CodegenInputStamp $root $sdkVersion $Build), (New-Object Text.UTF8Encoding $false))
} finally { Pop-Location; $ErrorActionPreference = 'Stop' }
