# Arena Edit — Build Plan

Product: **Arena Edit** — a lightweight, multi-language, AI-assisted video editor for
Windows 10/11 (64-bit). Credit: *Made by arenatechofficial.pages.dev*.

Status legend: `not started` / `in progress` / `verified` (build + tests green, logged in `PROGRESS.md`).

## Milestones (mapped to the build order in the spec)

| # | Phase | Scope (summary) | Status |
|---|-------|-----------------|--------|
| M0 | Skeleton | CMake, Qt main window + dockable layout, theme tokens, settings, i18n loader (EN + AR), logging, autosave stub, CI | verified |
| M1 | Media | FFmpeg import, decode, A/V-synced preview, thumbnails, waveforms, missing-media relink | verified |
| M2 | Timeline | Tracks, clips, ripple/roll/slip/slide trims, snapping, markers, undo/redo, autosave/recovery | verified (slip/slide deferred) |
| M3 | Rendering | D3D11/OpenGL compositor, transforms, opacity, transitions, basic color, frame cache (LRU 512 MB) | verified (GPU path → M9) |
| M4 | Export | Presets, hardware encode with software fallback, batch queue with progress/ETA/cancel | verified (MOV/ProRes, WAV → M8) |
| M5 | Subtitles | SRT/VTT/ASS import-export, multi-language tracks (BCP-47), burn-in, RTL, font fallback, reading-speed checks | verified (ASS style round-trip, per-track styling → M8) |
| M6 | Kilo Gateway | Streaming client (verified against live gateway), subtitle translation, chapters, titles, AI panel, budget, cache | not started |
| M7 | Smart features | whisper.cpp transcription, edit suggestions from transcript + silence map, NL command bar (`/ai`) | not started |
| M8 | Polish | Onboarding tour, empty states, motion, accessibility audit, DPI 100–300 %, performance tuning | not started |
| M9 | Release | Portable .exe, Inno Setup installer, code-signing hook, benchmark report, docs final, release checklist | not started |

## Phase details and exit criteria

### M0 — Skeleton (current)
- [x] CMake project, C++20, `/W4` (MSVC) / `-Wall -Wextra` (Clang/GCC), zero-warning target
- [x] `MainWindow` with dockable, resizable panels: Media Bin, Program Monitor, Inspector, Timeline, AI Panel, Subtitles
- [x] Design tokens in `assets/theme/theme.json` (5 surface levels, accent `#4C8DFF`, AI violet `#9B6DFF`, radii 6/10, spacing grid)
- [x] Three themes (dark default, light, high-contrast); accent user-changeable; follows OS theme on first launch
- [x] Settings (QSettings-backed, typed accessors, live change signals)
- [x] i18n loader: JSON translation files, auto-discovery (drop-in language), EN + AR shipped, RTL detection, immediate switch, no concatenated translated fragments
- [x] Logging to `%LOCALAPPDATA%/ArenaEdit/logs` (Windows) with daily rotation, crash-context writes
- [x] Autosave (60 s) + crash-recovery check on launch (restore prompt)
- [x] Command palette (Ctrl+K) with fuzzy search over commands + shortcuts; `/ai` NL mode hook
- [x] About dialog with full branding + credit link; plain-language error dialogs with "Show details" / "Copy diagnostics"
- [x] Kilo Gateway client skeleton (real, verified against the live API — see `AI_INTEGRATION.md`), opt-in, privacy notice, budget meter in status bar
- [x] CI: GitHub Actions (Linux build+test+benchmark now; Windows build+package job defined)
- **Exit criteria:** app launches < 2 s, idle < 150 MB target tracked in `PERFORMANCE.md`; unit tests green on core/i18n/validator/themer; `--selftest` passes in CI headless.

### M1 — Media
- FFmpeg (LGPL, dynamic) wrapper behind `media/` interfaces; probe/import; decode on worker pool (never UI thread); preview playback with A/V sync (WASAPI); thumbnails; waveforms; proxy generator (540 p); relink missing media.
- Exit: import → scrub 1080p clip via proxy without UI freeze; waveforms visible; A/V drift < 1 frame over 60 s playback.

### M2 — Timeline
- Multi-track (V/A/S/adjustment) timeline, clip manipulation, ripple/roll/slip/slide, snap with visual feedback, markers, command-pattern undo/redo with action names in the UI, autosave/recovery wired.
- Exit: every edit is a command, undo/redo round-trips 100 steps in tests; snap line visible; autosave restores after simulated crash (test).

### M3 — Rendering
- D3D11 (preferred) / OpenGL compositor; effects: transform, opacity, blend modes, Ken Burns, color (exposure/contrast/saturation/temperature/.cube), blur, sharpen, speed with frame blending; transitions (dissolve, wipe, push, fade-to-black); LRU frame cache 512 MB.
- Exit: golden-frame tests (deterministic renders, SSIM/pixel-diff harness); playback at Full/1/2/1/4 with dropped-frame counter.
- **Done (software path)**: deterministic compositor (transform/opacity/color/blur/kenburns/speed schema),
  tail transitions (cross_dissolve, fade_to_black, wipe, push), LRU frame cache (512 MB, configurable),
  Inspector effects editor (undoable), monitor compositing with decode cache, Full/1/2/1/4 playback
  resolutions, dropped-frame counter. GPU path (D3D11/OpenGL) and sharpen/blend/.cube deferred (M9).

### M4 — Export
- MP4 (H.264/H.265/AV1), MOV (ProRes where licensed), WebM (VP9/AV1), audio (AAC/Opus/WAV); NVENC/QSV/AMF with x264/x265/SVT fallback; presets with estimated size/time; batch queue table.
- Exit: batch of 3 presets on a 30 s project completes with correct sizes; fallback verified on iGPU machine or simulated.
- **Done (MP4 + WebM)**: presets (mp4-h264-high/balanced/web-720, mp4-hevc, mp4-audio, webm-vp9)
  with live size/time estimates; NVENC/QSV/AMF candidates with x264/x265/SVT-AV1/AOM/VP9
  software fallback (simulated via injectable encoder list); ExportQueue (worker thread,
  sequential jobs, per-job progress + cancel); Export dialog (File → Export, Ctrl+Shift+E)
  with preset checklist, scale 25–200 %, custom-quality CRF slider, audio bitrate, HW
  toggle, and a batch queue table. Batch of 3 presets exits green; 30 s real-media run
  covered by the guismoke export section (2 presets through the dialog).
  All output streams use a 1/1000 time base (Matroska muxer requirement — see DECISIONS D-019).
  Deferred: MOV/ProRes container, WAV audio export, GPU-encode benchmark on real hardware.

### M5 — Subtitles
- SRT/VTT/ASS import/export; multi-track with BCP-47; burn-in + sidecar; RTL mirroring for text; CJK/Arabic/Devanagari font fallback; reading-speed warnings (> 42 chars, > 17 cps inline red underline).
- Exit: round-trip SRT/VTT/ASS tests byte-stable on sample set; Urdu + Arabic burn-in golden frames.

### M6 — Kilo Gateway
- Client (streaming SSE, 30 s timeout, backoff + jitter on 429/5xx, queue, per-hour budget with status-bar meter, response cache keyed by hash(model, prompt, params), key via env `KILO_API_KEY` or DPAPI-secure settings).
- Features: subtitle translation (≈ 40 entries/batch, line-count validation, retry failed batch only), chapters, smart titles/descriptions, edit suggestions (accept/reject, never auto-apply).
- **Exit:** all features run against the live gateway in a recorded session logged to `AI_INTEGRATION.md`; app 100 % functional with AI disabled/offline.

### M7 — Smart features
- whisper.cpp (tiny/base/small on demand, CPU default, CUDA/Vulkan optional) with word-level timestamps; transcript feeds M6 features (text only ever leaves the machine); edit suggestions from transcript + silence map; NL command bar with strict JSON action schema, preview + confirmation, all actions undoable.
- Exit: transcription of bundled sample clip < 60 s on 4-core CPU; NL "add a fade-out to the last clip" → validated preview → undoable apply (test).

### M8 — Polish
- 60 s first-run tour (skip/replay), bundled sample project, contextual tips; full keyboard nav + focus rings + UIA names; WCAG AA high-contrast; DPI 100–300 %; font-size scaling; reduce-motion respected.
- Exit: accessibility audit checklist in `PROGRESS.md`; no UI freezes in 10-min fuzzed import soak.

### M9 — Release
- Portable single .exe (static Qt where practical, UPX off by default), Inno Setup installer with branding/credit, code-signing from env cert, benchmark harness output in `PERFORMANCE.md`, test report + known limitations.
- Exit: release checklist signed off; artifact uploaded in CI.

## Verification policy (working rules)
1. Every phase: build clean (zero warnings) → run unit tests → run targeted manual/CLI checks → log evidence in `PROGRESS.md`.
2. Gateway behavior is only documented after a real recorded response (`AI_INTEGRATION.md`); no invented API shapes.
3. Ambiguities resolved conservatively, recorded in `DECISIONS.md`.
4. Small, reviewable commits per work item.
5. Core must never depend on AI features: `core/`, `media/`, `render/` have no `#include` of `ai/` (enforced by dependency rule + test).

## Environment note
Target platform is Windows/MSVC; this repository's code is cross-platform C++ so the
skeleton, core, i18n, AI client, and validator logic are **verified on Linux (Qt 6, GCC)**
in CI. Windows-only paths (WASAPI, DPAPI, D3D11, Inno packaging) are compile-guarded and
exercised by the Windows CI job. See `DECISIONS.md` (D-003).
