# Arena Edit — benchmark harness (Windows).
# Reports cold-start time, idle RSS, and (from M4) export FPS.
# Usage: tools\benchmark.ps1 [-Runs 5]
param([int]$Runs = 5)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Get-ChildItem -Path $root\build -Recurse -Filter "ArenaEdit.exe" | Select-Object -First 1
if (-not $exe) { throw "ArenaEdit.exe not found; run tools\build.ps1 first" }

$results = @()
for ($i = 1; $i -le $Runs; $i++) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $exe.FullName -ArgumentList "--selftest" -PassThru -NoNewWindow -Wait
    $sw.Stop()
    $results += [pscustomobject]@{ Run = $i; ExitCode = $p.ExitCode; StartMs = [int]$sw.ElapsedMilliseconds }
    if ($p.ExitCode -ne 0) { throw "selftest run $i failed" }
}
$avg = [int]($results | Measure-Object -Property StartMs -Average).Average
$min = [int]($results | Measure-Object -Property StartMs -Minimum).Minimum

# Idle RSS: launch the real UI, wait, sample working set.
$p = Start-Process -FilePath $exe.FullName -PassThru
Start-Sleep -Seconds 3
$proc = Get-Process -Id $p.Id
$mb = [math]::Round($proc.WorkingSet64 / 1MB, 1)
$proc.CloseMainWindow() | Out-Null
Start-Sleep -Seconds 2
if (-not $proc.HasExited) { $proc.Kill() }

Write-Host ""
Write-Host "=== Arena Edit benchmark ($(Get-Date -Format s)) ===" -ForegroundColor Cyan
Write-Host ("cold start (selftest): avg {0} ms, min {1} ms  (target < 2000 ms)" -f $avg, $min)
Write-Host ("idle RSS (empty project): {0} MB  (target < 150 MB)" -f $mb)
Write-Host "core binary size: {0} MB  (target < 60 MB before bundled FFmpeg)" -f [math]::Round((Get-Item $exe.FullName).Length / 1MB, 1)
if ($avg -ge 2000) { Write-Warning "cold start above target" }
if ($mb -ge 150) { Write-Warning "idle RSS above target" }
