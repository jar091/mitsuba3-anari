<#
.SYNOPSIS
  Verifies (and where possible prepares) the Windows development environment
  for mitsuba-anari.

.DESCRIPTION
  Checks for:
    - Visual Studio 2022 with the MSVC x64 C++ toolchain
    - CMake >= 3.24
    - Git
    - Python 3.x
  Dependencies (ANARI-SDK) are fetched by CMake itself when
  MITSUBA_ANARI_FETCH_DEPENDENCIES=ON (the default), so no manual dependency
  download is needed here.

  Exits nonzero if a mandatory tool is missing.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$failures = @()

function Test-Tool {
  param([string]$Name, [string]$Command, [string]$Hint)
  $cmd = Get-Command $Command -ErrorAction SilentlyContinue
  if ($cmd) {
    Write-Host ("  [ok] {0}: {1}" -f $Name, $cmd.Source)
    return $true
  }
  Write-Host ("  [MISSING] {0} - {1}" -f $Name, $Hint) -ForegroundColor Red
  $script:failures += $Name
  return $false
}

Write-Host "mitsuba-anari Windows bootstrap check"
Write-Host "-------------------------------------"

if (Test-Tool 'CMake' 'cmake' 'https://cmake.org/download/ (>= 3.24 required)') {
  $v = (cmake --version | Select-Object -First 1) -replace 'cmake version ', ''
  if ([version]($v -replace '-.*$', '') -lt [version]'3.24') {
    Write-Host ("  [MISSING] CMake {0} is too old; >= 3.24 required" -f $v) -ForegroundColor Red
    $failures += 'CMake>=3.24'
  }
}
Test-Tool 'Git' 'git' 'https://git-scm.com/download/win' | Out-Null
Test-Tool 'Python 3' 'python' 'https://www.python.org/downloads/ (3.x)' | Out-Null

# Visual Studio 2022 C++ toolchain
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsFound = $false
if (Test-Path $vswhere) {
  $vs = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -format json | ConvertFrom-Json
  if ($vs) { $vsFound = $true }
}
if (-not $vsFound) {
  # vswhere can miss some installs; fall back to a direct filesystem probe.
  $msvcRoots = @(
    "$env:ProgramFiles\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC",
    "$env:ProgramFiles\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC",
    "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC",
    "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC"
  )
  foreach ($root in $msvcRoots) {
    if (Test-Path $root) { $vsFound = $true; break }
  }
}
if ($vsFound) {
  Write-Host '  [ok] Visual Studio 2022 MSVC x64 toolchain'
} else {
  Write-Host '  [MISSING] Visual Studio 2022 with "Desktop development with C++"' -ForegroundColor Red
  $failures += 'Visual Studio 2022 C++'
}

Write-Host ''
if ($failures.Count -gt 0) {
  Write-Host ("Bootstrap FAILED - missing: {0}" -f ($failures -join ', ')) -ForegroundColor Red
  exit 1
}

Write-Host 'Bootstrap OK. Next steps:'
Write-Host '  cmake --preset windows-msvc-debug'
Write-Host '  cmake --build --preset windows-msvc-debug'
Write-Host '  ctest --preset windows-msvc-debug --output-on-failure'
exit 0
