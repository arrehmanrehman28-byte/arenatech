# Arena Edit — one-command Windows build (MSVC 2022 + Ninja + vcpkg + Qt 6).
# Usage:  tools\build.ps1 [-Configuration Release|Debug] [-Tests]
# Prereqs: Visual Studio 2022 (Desktop C++), CMake >= 3.21, Ninja, vcpkg.
# The vcpkg manifest pins qtbase (LGPL) — FFmpeg is added in the Media phase (M1)
# behind -DARENA_ENABLE_FFMPEG=ON.
param(
    [string]$Configuration = "Release",
    [switch]$Tests
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

Write-Host "== Arena Edit build ==" -ForegroundColor Cyan
Write-Host "root: $root  config: $Configuration"

# 1. Toolchain sanity -----------------------------------------------------------
$need = @("cmake", "ninja", "cl")
foreach ($tool in $need) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Missing tool: $tool. Install VS2022 (C++ desktop), CMake, and Ninja, then open a 'Developer Command Prompt'."
    }
}
if (-not (Test-Path "env:\VCPKG_ROOT") -and -not (Get-Command vcpkg -ErrorAction SilentlyContinue)) {
    Write-Warning "VCPKG_ROOT not set; relying on a system Qt 6 (Qt6_DIR). For a manifest build, set VCPKG_ROOT."
}

# 2. Configure ------------------------------------------------------------------
$buildDir = Join-Path $root "build"
$testFlag = if ($Tests) { "ON" } else { "OFF" }
cmake -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=$Configuration `
    -DARENA_BUILD_TESTS=$testFlag `
    -DARENA_ENABLE_FFMPEG=OFF -DARENA_ENABLE_WHISPER=OFF `
    -DARENA_BUILD_DATE=(Get-Date -Format "yyyy-MM-dd HH:mm UTC")
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

# 3. Build ----------------------------------------------------------------------
cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw "build failed" }

# 4. Tests ----------------------------------------------------------------------
if ($Tests) {
    Push-Location (Join-Path $buildDir)
    ctest --output-on-failure --build-config $Configuration
    if ($LASTEXITCODE -ne 0) { Pop-Location; throw "tests failed" }
    Pop-Location
}

# 5. Selftest (headless app check) ---------------------------------------------
$exe = Get-ChildItem -Path $buildDir -Recurse -Filter "ArenaEdit.exe" | Select-Object -First 1
if (-not $exe) { $exe = Get-ChildItem -Path $buildDir -Recurse -Filter "arenaedit.exe" | Select-Object -First 1 }
Write-Host "== selftest ==" -ForegroundColor Cyan
& $exe.FullName --selftest
if ($LASTEXITCODE -ne 0) { throw "selftest failed" }

Write-Host "== done: $($exe.FullName) ==" -ForegroundColor Green
