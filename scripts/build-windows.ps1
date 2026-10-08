<#
.SYNOPSIS
  Configure and build mitsuba-anari on Windows.

.PARAMETER Preset
  Configure preset name. Default: windows-msvc-debug.
  Available: windows-msvc-debug, windows-msvc-release, windows-msvc-relwithdebinfo.
#>
[CmdletBinding()]
param(
  [string]$Preset = 'windows-msvc-debug'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
  cmake --preset $Preset
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  cmake --build --preset $Preset
  exit $LASTEXITCODE
} finally {
  Pop-Location
}
