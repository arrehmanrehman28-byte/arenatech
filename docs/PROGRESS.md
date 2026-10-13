# Arena Edit — Progress Log

*Verified-phase log (working rule #2). Every entry: build clean (zero
warnings) → tests → targeted checks → evidence.*

## 2026-10-13 — M2 Timeline editing: verified ✅

**Scope delivered**

- **Every edit is a command** (M2 exit criterion): AddClip, Relink,
  DeleteClip, TrimClips (trim + roll), AddMarker, RemoveMarker — all
  undoable with human-readable action names on the Undo/Redo buttons.
- **Autosave wired to every command**: `UndoStack::onChanged` →
  `setProjectChanged()` → 60 s autosave + crash-recovery sidecar covers
  every edit, undo, and redo.
- **Clip selection**: click to select (accent outline), click empty
  space to deselect; Inspector shows the selected clip (source, type,
  In/Out/Start/Duration in SMPTE, missing-media warning); Delete key
  removes the selected clip (re-inserts at the same position on undo).
- **Trimming**: clip edges are grabbable (split cursor, 5 px tolerance)
  with live preview; left edge moves in+offset (clamped to timeline
  start and min duration), right edge moves out (clamped to source
  length); a shared boundary drags as a **roll** (grabbed from either
  side, duration sum preserved, clamped to both sources).
- **Q/W ripple trim**: W trims the start of the clip at the playhead to
  the playhead and shifts later clips right; Q trims the end and shifts
  later clips left.
- **Snapping with visible feedback** (M2 exit criterion): while
  trimming, the moving edge is pulled to nearby clip edges or the
  playhead (4 px) and a violet snap line is drawn; toggle in the
  timeline toolbar.
- **Markers** (ruler, schema v1 — already serialized): M key or
  double-click on the ruler adds a violet marker; right-click a marker
  removes it; all undoable; `ProjectModel.MarkersRoundTrip` test.

**Verification (g++ 14.2, Qt 6.8.2, FFmpeg 7.1.5)**

- Clean rebuild: **0 errors, 0 warnings**.
- ctest: **45/45 pass** — new: `UndoStack.HundredStepRoundTrip`
  (M2 exit criterion: 100 steps undo → redo, state verified at every
  step), `UndoStack.ChangedCallbackFires`,
  `ProjectModel.MarkersRoundTrip`, and the extended
  `Guismoke.MediaPipeline` (24 checks: select/delete/undo/redo/
  deselect, W/Q trims, second-clip append, roll drag +4 frames, marker
  add/remove/undo, marker painted on the ruler via pixel check).
- `--selftest` with media fixture: **PASS** (19 checks).

**Bugs found & fixed**

- Q-trim source math: `out = in + (playhead − offset)` (was ignoring a
  non-zero in point).
- Roll grab detection from the right clip's left edge (shared boundary).
- Roll/trim clamps: timeline start (left edge), source length (right
  edge and both clips in a roll), in ≥ 0.
- Drag patches were captured after the drag state reset → empty
  patches, no-op edits.
- Canvas signals (scrub/select/trim/markers) had to be forwarded through
  `TimelineWidget` — previously declared but never connected.

**Not done / next**

- Slip/slide trims (in/out or offset without length change) — M2.5 or
  M3 if time-box allows.
- M3 Render: compositor, transitions, frame cache.

## 2026-10-13 — M1 Media: verified ✅

**Scope delivered**

- `media/` (FFmpeg 7.1, LGPL, dynamic): `MediaProbe` (format probe:
  resolution, fps, duration, codecs, sample rate/channels), `VideoDecoder`
  (seek + frame decode → QImage, 4-packet h264 look-ahead), `AudioReader`
  (decode + resample to 48 kHz interleaved float), `WaveformGenerator`
  (min/max peaks per time slice), `Thumbnailer` (evenly spaced frames),
  `ProxyGenerator` (mp4/mpeg4+aac transcode scaled to project proxy height,
  progress callback, per-user cache dir, deterministic name from
  source path + size).
- `app/playback.h/.cpp` — `PlaybackEngine`: A/V-synced preview. Audio is
  the master clock when the file has audio (QAudioSink, 48 kHz/2ch);
  wall clock for audio-less sources **and** as fallback when no audio
  device exists (headless CI, muted machines). Video decoded on demand
  from the clock (scrub-safe). Pure clock math is unit-tested.
- App wiring: Media Bin import → background probe (worker thread → queued
  signals) → status columns (Video 640×360 · 30 fps / Audio · 48 kHz /
  Missing); double-click or right-click "Add to timeline"; Relink
  re-points all clips (undoable) and re-probes; per-source proxy
  generation on demand (skipped when source is already ≤ proxy height);
  Program Monitor shows decoded frames + waveform strip + thumbnail
  filmstrip; Timeline draws clips (waveform texture, selection, missing
  badge) with a playhead; ruler scrubbing + click-to-seek; Space =
  play/pause; frame step; clip switching on seek.
- All new user-facing strings in en/ar/ur (212 keys per locale, parity
  checked).

**Verification (this machine: g++ 14.2, Qt 6.8.2, FFmpeg 7.1.5)**

- Clean rebuild from scratch: **0 errors, 0 warnings** (-Wall -Wextra).
- ctest: **42/42 pass** — 7 new `MediaM1.*` GTests against
  `tests/fixtures/clip01.mp4` (probe, wav, waveform range, thumbnails,
  seek+frame, interleaved audio, proxy 320×180 replayable) plus the new
  `Guismoke.MediaPipeline` (headless offscreen: import → probe →
  add-clip → first frame → play 500 ms → seek → aspect, **2.0 s**).
- `--selftest` with `ARENA_SELFTEST_MEDIA=…/clip01.mp4`: **19/19 PASS**
  (probe/waveform/thumbnail checks included).
- Media-layer smoke (probe/video/wave/thumbs/proxy/audio) from M1 media
  phase: all PASS; proxy ffprobe: 1.97 s video / 2.01 s audio @320×180.

**Bugs found & fixed (logged in DECISIONS.md)**

- D-015 re-entrant `retranslateUi()` → infinite recursion (latent since
  M0; first hit by auto-project creation) → stack overflow SEGV.
- D-016 Qt 6.8 QAudioSink in this environment is the QIODevice model
  (`start()` returns the write device, `processedUSecs()`): playback
  adapted; write loop is non-blocking (bytesFree check) so shutdown can
  never deadlock on a stalled device.
- D-017 no-audio fallback: playback keeps working (wall clock) when the
  sink has no device.
- Encoder flush (`avcodec_send_frame(enc, nullptr)`), proxy framerate /
  time_base adoption, and a pump-loop control-flow bug (dangling `if`
  swallowed the loop body → 2-frame proxy) from the media phase.

**Not done / next**

- M2 Timeline: ripple/roll/slip/slide edits, snap line + flash, markers,
  I/O handles, multi-clip preview (jump-cut switching is M1's behavior),
  per-clip in/out trimming, autosave on every command.
- M3 Render, M4 Export as planned.

## 2026-10-12 — M0 Skeleton: verified ✅

**Scope delivered**

- CMake project, C++20, zero warnings (`-Wall -Wextra` GCC here; `/W4` on
  MSVC path), CMake options `ARENA_BUILD_TESTS`, `ARENA_ENABLE_FFMPEG` (OFF),
  `ARENA_ENABLE_WHISPER` (OFF), `ARENA_UPX` (OFF).
- `MainWindow`: menu bar (File/Edit/View/AI/Help), command bar with
  Ctrl+K palette button, dockable panels (Media Bin, Program Monitor,
  Inspector, AI Panel, Subtitles), status bar (timecode, fps, render
  cache, RAM, AI budget meter).
- Program Monitor: letterboxed preview surface with safe-area/grid/thirds
  overlays, playback-resolution selector, virtual JKL transport + frame
  step (real decode in M1).
- Timeline: toolbar (snap on, magnetic/markers enabled with a project),
  Ctrl+wheel zoom, ruler with adaptive ticks + SMPTE labels, V1/A1 lanes.
  **Always LTR in RTL locales** (spec 3.6).
- Media Bin: import, file list, missing-media badge + Relink (file-level).
- Subtitle Manager: track table by BCP-47, add-track from SRT/VTT/ASS file.
- AI Panel: violet status card, task cards, **working "Ask AI"** with live
  streaming from the real Kilo Gateway, Accept/Edit/Reject result cards,
  one-time privacy notice, Enable/Disable.
- Command Palette (Ctrl+K): fuzzy search + shortcuts, `/ai` or Tab →
  natural-language mode.
- Dialogs: New Project (live preview line), Welcome (first run, language
  picker), Settings (10 groups, search, live-apply theme/accent/font/AI
  settings), About (branding + credit), Error dialog ("Show details" +
  "Copy diagnostics").
- Theming: tokens in `assets/theme/theme.json`; dark (default) / light /
  high-contrast; accent user-changeable; OS theme probed on first launch
  (Windows registry, guarded).
- i18n: drop-in JSON catalogs; **English + Arabic + Urdu** shipped; RTL
  detection + immediate switch + app-wide mirroring (timeline exempt);
  branding/credit stable across locales (unit-tested).
- Core: Project model (schema v1 + future-version gate + corrupt-file
  handling), Command/UndoStack (count + byte caps, named actions),
  Settings, Logger (daily rotation in `%LOCALAPPDATA%\ArenaEdit\logs`),
  Autosave (60 s) + crash-recovery prompt flow.
- AI layer: KiloClient (streaming SSE, 30 s timeout, backoff + jitter on
  429/5xx, queue, per-hour budget, SHA-1 response cache, key via env or
  DPAPI store), prompt templates, strict NL action validator.
- Honest boundaries: `media/` (FFmpeg), `render/` (compositor), `stt/`
  (whisper.cpp) ship interfaces + availability checks, verified by tests.
- CI: GitHub Actions — Linux job (build, zero-warning gate, 34 tests,
  selftest, benchmark) + Windows job (vcpkg Qt, MSVC, tests, selftest,
  Inno Setup packaging, artifact upload).
- Tools: `build.ps1` (one-command), `benchmark.ps1` / `benchmark.sh`,
  `package.iss` (Inno, branding + credit, signtool-ready).
- Docs: README, ARCHITECTURE (Mermaid), UI_GUIDE, PERFORMANCE,
  AI_INTEGRATION (real recorded gateway responses), PRIVACY, DECISIONS,
  PLAN, this file, TEST_REPORT, THIRD_PARTY.

**Verification evidence**

| Check | Result |
|-------|--------|
| `cmake --build build` | **zero errors, zero warnings** |
| `ctest` (34 tests) | **34/34 pass** (0.34 s) |
| `arenaedit --selftest` (headless) | **14/14 PASS** |
| Cold start avg (5 runs) | 92 ms (target < 2000 ms) |
| Idle RSS (empty project) | 46 MB (target < 150 MB) |
| Binary size (core) | 0.75 MB (target < 60 MB) |
| Kilo Gateway live check | `/models` 200 (390 models, 16 free); chat 200; SSE stream verified (see AI_INTEGRATION.md) |

**Bugs found & fixed during M0 verification** (log of real catches):

1. `QList` cannot hold move-only `unique_ptr` → UndoStack switched to
   owned raw pointers.
2. Qt 6.8 logger signature is `(QtMsgType, const QMessageLogContext&,
   const QString&)` — handler updated.
3. `it.value().toObject()` called twice for begin/end iterators → UB
   crash in i18n loader and themer (fixed with locals; would have been a
   shipping crash on theme load).
4. **`Q_OBJECT` injects `static tr(const char*)` into every QObject
   subclass** — a `tr(const QString&)` member silently loses
   string-literal calls to the injected static (returns untranslated
   text). Lookup renamed to `translate()` (D-004).
5. `QString::arg(QString, int)` ambiguity (int read as field width) left a
   literal `%2px` in the QSS → Qt rejected the whole stylesheet at polish
   time (fixed; all arg chains audited).
6. `QFile::close()` returns void in Qt 6; `QTabWidget::Rounded` (not
   `RoundedTabs`); `QMessageBox::setButtonText` deprecated → `addButton`.
7. Autosave recovery test flake: same-second mtimes → deterministic
   past/now utime in the test.

**Not done / next**

- M1 Media: FFmpeg LGPL dynamic link (`ARENA_ENABLE_FFMPEG=ON`), decode
  thread pool, A/V-synced playback (WASAPI), thumbnails, waveforms, 540p
  proxies, relink wired to decoded probe.
- M2 Timeline: clips, ripple/roll/slip/slide, snap line + flash, markers
  on the ruler, I/O in-out, Q/W, autosave wired to every command.
- M3 Render: D3D11 compositor + LRU frame cache + golden-frame tests.
- M4 Export: presets + batch queue + HW encode fallback + export-FPS
  benchmark.
- M5 Subtitles: full SRT/VTT/ASS round-trip, burn-in, RTL text, reading-
  speed warnings; +es/fr/hi/zh/ja catalogs.
- M6 AI features: subtitle translation batching (40/batch, count
  validation, failed-batch retry), chapters, titles, cut suggestions —
  all against the live gateway with recorded sessions.
- M7 STT: whisper.cpp binding + NL command bar end-to-end.
- M8 Polish: onboarding tour, a11y audit, DPI 100–300% pass, icon
  16/20/24 set, screenshots.
- M9 Release: static-Qt portable .exe, signed Inno package, final
  benchmark table.
