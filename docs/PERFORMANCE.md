# Arena Edit — Performance

## Targets (spec §2)

| Area | Target |
|------|--------|
| Cold start | < 2 s on mid-range laptop (4 cores, 8 GB, iGPU) |
| Idle RAM | < 150 MB with empty project |
| Core binary | < 60 MB before bundled FFmpeg (AI models on demand, never bundled) |
| UI responsiveness | No UI freezes during decode/export/transcription/AI |
| Timeline playback | Smooth 1080p scrubbing via proxies |
| Stability | Autosave 60 s, crash recovery, crash logs |

## Method

- **Cold start**: `tools/benchmark.sh` (Linux CI) / `tools/benchmark.ps1`
  (Windows) — wall time of `arenaedit --selftest` (full app init: Qt,
  logger, i18n load, theme build, main window construction), 5 runs,
  avg/min reported.
- **Idle RAM**: launch the real UI, wait 3 s, sample `VmRSS` (Linux) /
  `WorkingSet64` (Windows) with an empty project.
- **Binary size**: `du`/`Get-Item` on the final exe (before staging Qt/
  FFmpeg DLLs — the portable payload in M9 is the number that must stay
  < 60 MB).
- **UI freeze policy** (M1+): decode/export/STT on a worker pool sized
  physical-cores − 1; frame budget 33 ms; dropped-frame counter +
  performance overlay in the monitor; Tracy/Windows Performance Analyzer
  traces recorded per milestone below.
- **Export FPS** (M4): `tools/benchmark.ps1` encodes a fixed 30 s 1080p
  clip and reports encode FPS per codec (NVENC vs x264 fallback).

## Results — M0 skeleton (2026-10-12)

Environment: sandbox laptop, Debian 13, GCC 14.2, Qt 6.8.2, 8 vCPU (CI
harness — a mid-range Windows laptop should be within the same order).

| Metric | M0 result | Target | Status |
|--------|-----------|--------|--------|
| Cold start (selftest, avg of 5) | **92 ms** | < 2000 ms | ✅ 21× under |
| Cold start (selftest, min) | 88 ms | < 2000 ms | ✅ |
| Idle RAM (empty project) | **46 MB** | < 150 MB | ✅ 3.3× under |
| Core binary (no Qt/FFmpeg) | **0.75 MB** | < 60 MB | ✅ 80× under |
| Unit tests (34) | all green in 0.34 s | — | ✅ |

Notes:

- The M0 idle RSS includes the offscreen Qt platform + fontconfig
  baseline; the Windows build's number will differ but the trend
  (headroom for the FFmpeg/whisper layers) is what matters.
- Qt 6.8's `QApplication` offscreen path itself costs ~60 ms; the app's
  own contribution (i18n + theme + window) is ~30 ms.
- No Tracy traces yet (no decode path); first traces land with M1.

## Per-milestone log

| Milestone | Date | Start (avg) | Idle RAM | Export FPS | Notes |
|-----------|------|-------------|----------|-----------|-------|
| M0 | 2026-10-12 | 92 ms | 46 MB | — | skeleton + tests |
| M1 | — | — | — | — | decode path, first Tracy trace |
| M4 | — | — | — | — | export FPS per codec |
| M9 | — | — | — | — | release build, UPX off, final |
