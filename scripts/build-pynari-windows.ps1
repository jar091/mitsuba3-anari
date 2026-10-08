<#
.SYNOPSIS
  Clone and build PyNARI (pinned commit) against this project's provisioned
  ANARI-SDK, for use by tests/pynari and examples/pynari.

.DESCRIPTION
  - Clones jar091/pynari (fork of ingowald/pynari) and checks out the pinned commit from
    DEPENDENCIES.md.
  - The vendored pybind11 (2.12.0.dev1) does not support Python 3.13, so the
    build uses PYNARI_USE_INSTALLED_PYBIND=ON with a pip-installed pybind11.
  - Builds the module with the same MSVC toolchain as the device.

.PARAMETER BuildDir
  Project build directory holding the provisioned SDK.
  Default: build\windows-msvc-release
#>
[CmdletBinding()]
param(
  [string]$BuildDir = 'build\windows-msvc-release',
  [string]$Python = 'python'
)

# 'Continue' (not 'Stop'): git and cmake write progress to stderr, which
# PowerShell would otherwise escalate to a terminating NativeCommandError.
# Failures are detected explicitly via $LASTEXITCODE after every step.
$ErrorActionPreference = 'Continue'
$repoRoot = Split-Path -Parent $PSScriptRoot
$pynariCommit = 'a860d0b68d08096f552d158265a90d040bd4498d'
$pynariSrc = Join-Path $repoRoot "$BuildDir\_deps\pynari-src"
$pynariBuild = Join-Path $repoRoot "$BuildDir\_deps\pynari-build"
$anariDir = Join-Path $repoRoot "$BuildDir\_deps\anari-sdk-install\lib\cmake"

$anariConfig = Get-ChildItem -Path $anariDir -Filter "anariConfig.cmake" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $anariConfig) {
  Write-Host "ERROR: Provisioned ANARI-SDK not found under $anariDir. Configure the project first (cmake --preset windows-msvc-release)."
  exit 1
}
$anariCMakeDir = $anariConfig.DirectoryName

if (-not (Test-Path "$pynariSrc\CMakeLists.txt")) {
  git clone https://github.com/jar091/pynari $pynariSrc
  if ($LASTEXITCODE -ne 0) { exit 1 }
}
git -C $pynariSrc checkout $pynariCommit
if ($LASTEXITCODE -ne 0) { exit 1 }
git -C $pynariSrc submodule update --init --recursive
if ($LASTEXITCODE -ne 0) { exit 1 }

# Apply the documented upstream fix (see PYNARI.md): pybind11 >= 2.13 routes
# python ints through pynari's float overloads; the patch makes those honor
# the declared integer parameter types. Idempotent via --reverse --check.
$patch = Join-Path $repoRoot 'patches\pynari-pybind213-int-dispatch.patch'
git -C $pynariSrc apply --reverse --check $patch 2>$null
if ($LASTEXITCODE -ne 0) {
  git -C $pynariSrc apply $patch
  if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: failed to apply pynari patch"; exit 1 }
}

& $Python -m pip install --quiet pybind11 numpy pytest
if ($LASTEXITCODE -ne 0) { exit 1 }
$pybindDir = & $Python -m pybind11 --cmakedir
if ($LASTEXITCODE -ne 0) { exit 1 }

cmake -S $pynariSrc -B $pynariBuild -G "Visual Studio 17 2022" -A x64 `
  "-Danari_DIR=$anariCMakeDir" `
  "-DPYNARI_USE_INSTALLED_PYBIND=ON" `
  "-Dpybind11_DIR=$pybindDir" `
  "-DPython_EXECUTABLE=$((Get-Command $Python).Source)"
if ($LASTEXITCODE -ne 0) { exit 1 }

cmake --build $pynariBuild --config Release --parallel
if ($LASTEXITCODE -ne 0) { exit 1 }

$module = Get-ChildItem -Path $pynariBuild -Filter "pynari*.pyd" -Recurse | Select-Object -First 1
if ($module) {
  Write-Host ""
  Write-Host "PyNARI module built: $($module.FullName)"
  Write-Host "Add its directory to PYTHONPATH, e.g.:"
  Write-Host "  `$env:PYTHONPATH = '$($module.DirectoryName)'"
  Write-Host "and configure the project with:"
  Write-Host "  -DMITSUBA_ANARI_BUILD_PYNARI_TESTS=ON -DMITSUBA_ANARI_PYNARI_PYTHONPATH='$($module.DirectoryName)'"
} else {
  Write-Error "pynari module not found after build"
}
