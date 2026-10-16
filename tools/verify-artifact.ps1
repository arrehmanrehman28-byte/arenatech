# Arena Edit v0.1.0 - artifact verifier (run on Windows)
#
# Usage:
#   1. Download the artifact from Actions (arenaedit-windows-x86_64-full),
#      unzip it, then unzip the portable zip inside.
#   2. tools\verify-artifact.ps1 C:\path\to\portable-folder
#
# Verifies the exact runtime requirements that killed release run
# 38795270722 (missing DLL at startup) so a broken package can never
# be mistaken for a working one.

param([string]$PortDir)

if (-not $PortDir) {
    $PortDir = Split-Path $PSScriptRoot -Parent   # assume <root>\dist\portable
}
if (-not (Test-Path $PortDir)) { Write-Host "Folder not found: $PortDir"; exit 2 }
Set-Location $PortDir

$fail = 0
function Check($name, $ok) {
    if ($ok) { Write-Host ("  [OK]   {0}" -f $name) -ForegroundColor Green }
    else     { Write-Host ("  [FAIL] {0}" -f $name) -ForegroundColor Red; $script:fail++ }
}

Write-Host "=== Arena Edit portable verifier: $PortDir ===" -ForegroundColor Cyan

# 1. The app itself
Check "ArenaEdit.exe present" (Test-Path ".\ArenaEdit.exe")
Check "Run Arena Edit.bat present" (Test-Path ".\Run Arena Edit.bat")

# 2. Qt platform plugin - THE killer from the failed run (exe cannot start
#    without it; it lives in vcpkg's plugins tree, never next to the exe)
Check "platforms\qwindows.dll present" (Test-Path ".\platforms\qwindows.dll")

# 3. Runtime DLLs: the complete closure, incl. the transitive ones the old
#    name-filter (Qt6|MSVCP|...) dropped and the shared-whisper imports.
$required = @(
    "Qt6Core.dll","Qt6Gui.dll","Qt6Widgets.dll","Qt6Network.dll",
    "icuin78.dll","icuuc78.dll","icudt78.dll",
    "libcrypto-3-x64.dll","libssl-3-x64.dll",
    "z.dll","libpng16.dll","freetype.dll","harfbuzz.dll",
    "double-conversion.dll","pcre2-16.dll","zstd.dll","md4c.dll",
    "brotlidec.dll","brotlicommon.dll","bz2.dll"
)
$missing = @($required | Where-Object { -not (Test-Path (Join-Path $PortDir $_)) })
if ($missing.Count -eq 0) { Check ("all {0} required runtime DLLs present" -f $required.Count) $true }
else { Check ("missing DLLs: {0}" -f ($missing -join ", ")) $false }

# FFmpeg set: only in the FULL (media) artifact - core builds ship without it
if (Test-Path (Join-Path $PortDir "avformat-63.dll")) {
    $ffMissing = @("avformat-63.dll","avcodec-63.dll","avutil-61.dll","swscale-10.dll","swresample-7.dll" |
        Where-Object { -not (Test-Path (Join-Path $PortDir $_)) })
    if ($ffMissing.Count -eq 0) { Check "all 5 FFmpeg runtime DLLs present (media build)" $true }
    else { Check ("missing FFmpeg DLLs: {0}" -f ($ffMissing -join ", ")) $false }
} else {
    Write-Host "  [info] no FFmpeg DLLs (core build - media features disabled by design)" -ForegroundColor DarkGray
}

# 4. Data the app loads at runtime
Check "assets\ present" (Test-Path ".\assets")
Check "i18n\ present" (Test-Path ".\i18n")

# 5. The real proof: the app must start and pass its headless smoke test
#    (same check CI runs; exercises core + i18n + action system + media layer)
$exe = Join-Path $PortDir "ArenaEdit.exe"
if (Test-Path $exe) {
    Write-Host "  running selftest..." -ForegroundColor Cyan
    & $exe --selftest 6>$null
    $rc = $LASTEXITCODE
    Check ("ArenaEdit.exe --selftest exits 0 (got {0})" -f $rc) ($rc -eq 0)
} else {
    Check "ArenaEdit.exe --selftest (exe missing)" $false
}

# 6. Installer (if the artifact's Setup.exe is next to the folder)
$setup = Get-ChildItem (Split-Path $PortDir -Parent) -Filter "ArenaEdit-*Setup.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($setup) {
    $ok = $setup.Length -gt 1MB
    Check ("Setup.exe present, {0:N1} MB" -f ($setup.Length/1MB)) $ok
} else {
    Write-Host "  [info] no Setup.exe next to the portable folder (not required here)" -ForegroundColor DarkGray
}

Write-Host ""
if ($fail -eq 0) {
    Write-Host "=== VERIFIED: artifact is complete and the app starts ===" -ForegroundColor Green
    exit 0
} else {
    Write-Host "=== {0} CHECK(S) FAILED - do not ship this artifact ===" -f $fail -ForegroundColor Red
    exit 1
}
