# Arena Edit — one-command Windows release (build -> test -> portable -> installer -> zip).
#
# Usage (from any directory, PowerShell 5.1+ or pwsh):
#   tools\release.ps1                                  # core build (no media) + installer + zip
#   tools\release.ps1 -WithMedia -FfmpegDir C:\ffmpeg  # full build: import/decode/export/STT
#
# -FfmpegDir must point at a FFmpeg dev prefix (include/lib/bin layout —
# a vcpkg port dir or an ffmpeg.org build). -WithMedia also enables the
# vendored whisper.cpp (CPU) for local STT.
#
# Code signing: set CERT_PATH / CERT_PASSWORD before running for an
# authenticated installer; without them the package is unsigned.
#
# Artifacts (under dist\):
#   ArenaEdit-<ver>-portable-win64.zip   portable folder (exe + DLLs + assets + i18n)
#   ArenaEdit-<ver>-Setup.exe            Inno Setup installer (branded, icon, credit)
#   portable\                            the staged portable payload
param(
    [string]$Configuration = "Release",
    [switch]$WithMedia,
    [string]$FfmpegDir = "",
    [switch]$SkipTests
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
# Own build dir (release generator) so this script never fights a dev or
# preset-configured build/ directory.
$buildDir = Join-Path $root "build-release"
$distDir = Join-Path $root "dist"
$portable = Join-Path $distDir "portable"
$version = "0.1.0"

Write-Host "================ Arena Edit release ================" -ForegroundColor Cyan
Write-Host "root: $root"
Write-Host "config: $Configuration   media: $WithMedia   ffmpeg: $FfmpegDir"
if ($WithMedia -and (-not $FfmpegDir)) {
    throw "-WithMedia requires -FfmpegDir <prefix> (include/lib/bin layout)."
}

# --- 1. Toolchain sanity ------------------------------------------------------
# iscc: Inno Setup is sometimes installed without a PATH entry; probe the
# standard locations and extend PATH if found.
$iscc = Get-Command iscc -ErrorAction SilentlyContinue
if (-not $iscc) {
    foreach ($probe in @("C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
                         "C:\Program Files\Inno Setup 6\ISCC.exe")) {
        if (Test-Path $probe) {
            $env:PATH += ";$(Split-Path $probe)"
            $iscc = Get-Command iscc -ErrorAction SilentlyContinue
            break
        }
    }
}
foreach ($tool in @("cmake", "ninja", "cl")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Missing tool: $tool. Install VS2022 (Desktop C++) + CMake + Ninja; open a Developer Prompt."
    }
}
if (-not $iscc) {
    throw "Missing tool: iscc. Install Inno Setup 6: choco install innosetup -y"
}

# --- 2. Configure -------------------------------------------------------------
# NOTE: build the ON/OFF values into variables first. In a PowerShell array
# literal the comma binds tighter than '+', so "-DFOO=" + $(...) , ... splits
# into TWO elements ("-DFOO=" and "ON") and CMake receives a broken flag.
$ffOn = if ($WithMedia) { "ON" } else { "OFF" }
$whOn = if ($WithMedia) { "ON" } else { "OFF" }
$cmakeArgs = @(
    "-B", $buildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DARENA_BUILD_TESTS=ON",
    "-DARENA_ENABLE_FFMPEG=$ffOn",
    "-DARENA_ENABLE_WHISPER=$whOn"
    # ARENA_BUILD_DATE: left to CMake's TIMESTAMP fallback (configure time) —
    # passing a spaced value from pwsh through native-arg quoting is fragile.
)
if ($FfmpegDir) { $cmakeArgs += "-DARENA_FFMPEG_DIR=$FfmpegDir" }
# vcpkg toolchain when VCPKG_ROOT is set (CI installs qtbase/ffmpeg there).
if ($env:VCPKG_ROOT) { $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$($env:VCPKG_ROOT)\scripts\buildsystems\vcpkg.cmake" }
Write-Host "== configure ==" -ForegroundColor Cyan
cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

# --- 3. Build -----------------------------------------------------------------
Write-Host "== build ==" -ForegroundColor Cyan
cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw "build failed" }

# --- 4. Tests -----------------------------------------------------------------
if (-not $SkipTests) {
    Write-Host "== tests ==" -ForegroundColor Cyan
    Push-Location $buildDir
    # --timeout: a single stuck test must not pin a runner for hours
    # (M9.21: a headless font query hung a Windows run for 2h).
    ctest --output-on-failure --timeout 300
    $code = $LASTEXITCODE
    Pop-Location
    if ($code -ne 0) { throw "tests failed" }
}

# --- 5. Selftest (headless app check) -----------------------------------------
$exe = Get-ChildItem -Path $buildDir -Recurse -Filter "ArenaEdit.exe" | Select-Object -First 1
if (-not $exe) { throw "ArenaEdit.exe not found after build" }
Write-Host "== selftest: $($exe.FullName) ==" -ForegroundColor Cyan
& $exe.FullName --selftest
if ($LASTEXITCODE -ne 0) { throw "selftest failed" }

# --- 6. Stage the portable payload ---------------------------------------------
# Critical: Qt runtime DLLs must keep their directory structure (the Windows
# platform plugin lives at platforms\qwindows.dll relative to the exe — a flat
# copy makes the exe fail to start). So every DLL found under the build tree is
# copied with its relative path preserved.
Write-Host "== stage portable -> $portable ==" -ForegroundColor Cyan
if (Test-Path $portable) { Remove-Item $portable -Recurse -Force }
New-Item -ItemType Directory -Force $portable | Out-Null
Copy-Item $exe.FullName (Join-Path $portable "ArenaEdit.exe")

# Double-click launcher for end users (portable zip: unzip, run, done).
$runBat = @"
@echo off
rem Arena Edit - Made by arenatechofficial.pages.dev
cd /d "%~dp0"
start "" "ArenaEdit.exe"
"@
Set-Content -Path (Join-Path $portable "Run Arena Edit.bat") -Value $runBat -Encoding ASCII

$dlls = Get-ChildItem -Path $buildDir -Recurse -Filter "*.dll" | Where-Object {
    $_.FullName -notmatch "\\CMakeFiles\\" -and $_.Name -match "Qt6|MSVCP|VCRUNTIME|concrt|vcruntime"
}
foreach ($d in $dlls) {
    $rel = $d.FullName.Substring($buildDir.Length).TrimStart('\')
    $dest = Join-Path $portable $rel
    New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
    Copy-Item $d.FullName $dest
}
$qtDllCount = (Get-ChildItem $portable -Recurse -Filter "*.dll").Count
Write-Host "   staged $qtDllCount runtime DLLs (incl. platforms\qwindows.dll: $(Test-Path (Join-Path $portable 'platforms\qwindows.dll')))"

# FFmpeg runtime DLLs (flat layout, from the provided prefix).
if ($WithMedia) {
    $ffBin = Join-Path $FfmpegDir "bin"
    if (-not (Test-Path $ffBin)) { throw "FFmpeg prefix bin/ not found: $ffBin" }
    Copy-Item (Join-Path $ffBin "*.dll") $portable
    $ffCount = (Get-ChildItem $portable -Filter "av*.dll").Count + (Get-ChildItem $portable -Filter "sw*.dll").Count
    Write-Host "   staged FFmpeg DLLs: $ffCount"
}

# App data: assets (theme, sample project, icon) + i18n catalogs.
Copy-Item -Recurse (Join-Path $root "assets") (Join-Path $portable "assets")
Copy-Item -Recurse (Join-Path $root "i18n") (Join-Path $portable "i18n")

# --- 7. Installer (Inno Setup) --------------------------------------------------
Write-Host "== installer (iscc) ==" -ForegroundColor Cyan
$issFile = Join-Path $root "tools\package.iss"
$isccArgs = @($issFile)
# Optional code signing: package.iss enables its Sign= line only when the
# SIGN_CMD preprocessor define is present. The .pfx (CERT_PATH) is imported
# into the current user's store and signtool's /a selects it — this keeps
# the Sign= template free of embedded quotes (which would corrupt Inno's
# value parsing if passed through /D). The store should hold only the
# signing certificate when this runs.
if ($env:CERT_PATH -and $env:CERT_PASSWORD) {
    $securePass = ConvertTo-SecureString $env:CERT_PASSWORD -AsPlainText -Force
    Import-PfxCertificate -FilePath $env:CERT_PATH -CertStoreLocation "Cert:\CurrentUser\My" -Password $securePass | Out-Null
    $isccArgs = @('/DSIGN_CMD="signtool sign /fd sha256 /a /tr http://timestamp.digicert.com /td sha256 /v $f"', $issFile)
    Write-Host "   signing with certificate from CERT_PATH"
} else {
    Write-Host "   no CERT_PATH/CERT_PASSWORD — building an unsigned installer"
}
& iscc @isccArgs
if ($LASTEXITCODE -ne 0) { throw "iscc failed" }

# --- 8. Zip the portable payload ------------------------------------------------
$zip = Join-Path $distDir "ArenaEdit-$version-portable-win64.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $portable "*") -DestinationPath $zip

Write-Host ""
Write-Host "================ release complete ================" -ForegroundColor Green
Get-ChildItem $distDir | Where-Object { $_.Name -match "ArenaEdit" } | ForEach-Object {
    Write-Host ("  {0,-45} {1,10} MB" -f $_.Name, [math]::Round($_.Length / 1MB, 1))
}
Write-Host "Portable: $portable" -ForegroundColor Green
