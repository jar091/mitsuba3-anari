<#
.SYNOPSIS
  Run the mitsuba-anari test suite on Windows via CTest presets.

.PARAMETER Preset
  Test preset name. Default: windows-msvc-debug.

.PARAMETER Label
  Optional CTest label filter (e.g. unit, integration, pynari).
#>
[CmdletBinding()]
param(
  [string]$Preset = 'windows-msvc-debug',
  [string]$Label
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
  $ctestArgs = @('--preset', $Preset, '--output-on-failure')
  if ($Label) { $ctestArgs += @('-L', $Label) }
  ctest @ctestArgs
  exit $LASTEXITCODE
} finally {
  Pop-Location
}
