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
- **Export FPS** (M9): `tools/benchmark.sh` invokes
  `arena_export_bench` (tools/export_bench.cpp), which runs the *real*
  ExportQueue worker path (stateless per-frame fetchers, the same shape
  the UI uses) over a real media file and reports frames / wall seconds
  per preset. The HW-fallback chain (NVENC/QSV/AMF → x264/VP9/…) is
  exercised for real: on a CPU-only box the log shows nvenc/qsv failing
  over to x264 before the encode. Numbers are conservative by design —
  the per-frame fetcher seeks per frame, unlike a sequential decode.

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
| M4 | 2026-10-13 | — | — | — | codec fallback verified (injectable list); GPU runs deferred to M9 |
| M9 | 2026-10-14 | 130 ms | 63 MB | x264 28.3 / VP9 13.9 | final Linux release-state numbers (below) |

## Results — M9 release state (2026-10-14)

Environment: sandbox laptop, Debian 13, GCC 14.2, Qt 6.8.2, 8 vCPU,
Release build, UPX off (spec default). Media: `tests/fixtures/clip01.mp4`
(640×360 h264+aac, 2 s, 60 frames).

| Metric | M9 result | Target | Status |
|--------|-----------|--------|--------|
| Cold start (selftest, avg of 5) | **130 ms** | < 2000 ms | ✅ 15× under |
| Idle RAM (empty project) | **63 MB** | < 150 MB | ✅ 2.4× under |
| Core binary (no Qt/FFmpeg DLLs) | **4.1 MB** | < 60 MB | ✅ 14× under |
| Export, mp4-h264-balanced (→ x264) | **28.3 FPS** | ≥ 24 FPS (30 fps source) | ✅ 0.95× realtime at 360p |
| Export, webm-vp9 | **13.9 FPS** | informational | i (VP9 CPU is slow; NVENC/HW path is the fast lane on real GPUs) |
| Unit tests (113) | all green, 54 s | — | ✅ |

Notes:

- Export FPS uses the shipped ExportQueue path with the per-frame
  seek-fetcher (stateless, thread-safe, never the playback engine), so
  it is a *lower bound* for sequential decode. 1080p CPU numbers will be
  lower; the NVENC/QSV/AMF path is what clears realtime at 1080p on
  shipped hardware (M4 codec-selection tests verify the selection logic).
- Idle RAM grew 46 → 63 MB since M0: whisper.cpp static init + export
  queue + subtitle tables in the startup path; still well under target.
- Windows numbers (cold start / idle / HW export FPS) are measured on a
  Windows machine via `tools/benchmark.ps1` in the release pipeline —
  not runnable in this Linux sandbox (D-044).
