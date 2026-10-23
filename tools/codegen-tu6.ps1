<#
.SYNOPSIS
  Recompiles Title Update 6 into generated\tu6: Default.xex with the update's Default.xexp applied. Same as
  tools\codegen.ps1 -Build tu6.

.EXAMPLE
  .\tools\codegen-tu6.ps1 -UpdatePackage game\title_update_tu6\TU_16L61US_0000004000000.00000000001G1
#>
param([string]$UpdatePackage = '')
& (Join-Path $PSScriptRoot 'codegen.ps1') -Build tu6 -UpdatePackage $UpdatePackage
exit $LASTEXITCODE
