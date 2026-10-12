# Arena Edit — Progress Log

*Verified-phase log (working rule #2). Every entry: build clean (zero
warnings) → tests → targeted checks → evidence.*

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
