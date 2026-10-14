# Arena Edit — one-command Windows build (MSVC 2022 + Ninja + vcpkg + Qt 6).
# Usage:  tools\build.ps1 [-Configuration Release|Debug] [-Tests] [-WithMedia]
#         [-FfmpegDir <ffmpeg-dev-prefix>]
# Prereqs: Visual Studio 2022 (Desktop C++), CMake >= 3.21, Ninja, vcpkg.
# The vcpkg manifest pins qtbase (LGPL) — FFmpeg (LGPL) is linked from
# -FfmpegDir (a vcpkg port dir or ffmpeg.org build prefix: include/lib/bin)
# when -WithMedia is given; whisper.cpp (MIT, vendored in third_party/)
# compiles from source in the same switch. Without -WithMedia the app
# builds and tests with media stubs (no import/decode/export/STT).
param(
    [string]$Configuration = "Release",
    [switch]$Tests,
    [switch]$WithMedia,
    [string]$FfmpegDir = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

Write-Host "== Arena Edit build ==" -ForegroundColor Cyan
Write-Host "root: $root  config: $Configuration  media: $WithMedia"

if ($WithMedia -and (-not $FfmpegDir)) {
    throw "-WithMedia requires -FfmpegDir <prefix> (vcpkg port dir or ffmpeg.org build with include/lib/bin)."
}

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
$ffmpegFlag = if ($WithMedia) { "ON" } else { "OFF" }
$whisperFlag = if ($WithMedia) { "ON" } else { "OFF" }
$ffmpegDirArg = if ($FfmpegDir) { "-DARENA_FFMPEG_DIR=$FfmpegDir" } else { @() }
$cmakeArgs = @(
    "-B", $buildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DARENA_BUILD_TESTS=$testFlag",
    "-DARENA_ENABLE_FFMPEG=$ffmpegFlag",
    "-DARENA_ENABLE_WHISPER=$whisperFlag",
    "-DARENA_BUILD_DATE=" + (Get-Date -Format "yyyy-MM-dd HH:mm UTC")
) + $ffmpegDirArg
cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

# 3. Build ----------------------------------------------------------------------
cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw "build failed" }

# 4. Tests ----------------------------------------------------------------------
if ($Tests) {
    Push-Location $buildDir
    ctest --output-on-failure --build-config $Configuration
    $code = $LASTEXITCODE
    Pop-Location
    if ($code -ne 0) { throw "tests failed" }
}

# 5. Selftest (headless app check) ---------------------------------------------
$exe = Get-ChildItem -Path $buildDir -Recurse -Filter "ArenaEdit.exe" | Select-Object -First 1
if (-not $exe) { $exe = Get-ChildItem -Path $buildDir -Recurse -Filter "arenaedit.exe" | Select-Object -First 1 }
Write-Host "== selftest ==" -ForegroundColor Cyan
& $exe.FullName --selftest
if ($LASTEXITCODE -ne 0) { throw "selftest failed" }

Write-Host "== done: $($exe.FullName) ==" -ForegroundColor Green
if ($WithMedia) {
    Write-Host "Next: stage the portable payload (ci.yml windows job) and run iscc tools\package.iss" -ForegroundColor Cyan
}
