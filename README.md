# Arena Edit

*A professional, lightweight, multi-language, AI-assisted video editor for
Windows 10/11 (64-bit) — built from scratch.*

**Made by [arenatechofficial.pages.dev](https://arenatechofficial.pages.dev)**

- Dark, calm, professional NLE workspace — easy for beginners, fast for experts
- Multi-track timeline, JKL transport, snapping, markers, undo/redo with named actions
- Multi-language UI with full RTL mirroring (Arabic, Urdu shipped; drop-in more)
- AI automation through the Kilo Gateway — **optional, off by default, text-only**
- Fast: sub-2 s cold start, low idle RAM, hardware acceleration where available

> **Status: M9 release state** — milestones M0–M8 fully verified
> (113/113 tests, 108-check GUI smoke, 10-minute fuzzed import soak with
> zero UI freezes), 130 ms cold start, 63 MB idle, 28 FPS software export.
> M9 packaging/benchmark work is verified for the Linux scope; the Windows
> artifact build + signing run in the release pipeline. See
> [docs/PROGRESS.md](docs/PROGRESS.md),
> [docs/TEST_REPORT.md](docs/TEST_REPORT.md) and
> [docs/PERFORMANCE.md](docs/PERFORMANCE.md). The full build plan is in
> [docs/PLAN.md](docs/PLAN.md).

## Features (by phase)

| Phase | What you get |
|-------|--------------|
| **M0 (done)** | Workspace + docks, themes (dark/light/high-contrast, live accent), 8-language UI + full RTL, command palette (Ctrl+K, `/ai` mode), project model + undo/redo, autosave + crash recovery, logging, AI panel with **working streaming Ask AI** + privacy notice + hourly budget meter, WCAG-verified high contrast, CI, benchmarks |
| **M1 (done)** | FFmpeg import/decode, A/V-synced playback, thumbnails, waveforms, 540p proxies, missing-media relink |
| **M2 (done)** | Clips, ripple/roll/slip/slide trims, snap feedback, markers, I/O, Q/W |
| **M3 (done)** | Compositor, transforms/opacity/transitions/color (incl. .cube LUTs), speed, Ken Burns, LRU frame cache |
| **M4 (done)** | Export presets (YouTube/Reels/Web/ProRes/podcast), NVENC/QSV/AMF + software fallback, batch queue |
| **M5 (done)** | SRT/VTT/ASS, multi-language subtitle tracks (BCP-47), burn-in/sidecar, RTL text, reading-speed warnings |
| **M6 (done)** | Subtitle translation (40/batch, validated), chapters, smart titles/descriptions, cut suggestions — all accept/reject, never auto-applied |
| **M7 (done)** | Local whisper.cpp transcription (tiny/base/small on demand), transcript-linked features, NL command bar with strict JSON schema |
| **M8 (done)** | First-run tour (60 s, replayable), bundled sample project, contextual tips, focus rings + UIA names, WCAG AA audit, DPI 100–300 %, font scaling, reduce-motion |
| **M9** | Portable exe + Inno Setup installer (signed from env cert when provided), release benchmarks (this state), Windows artifact via release pipeline |

## System requirements

**Target (release):** Windows 10/11 64-bit. 8 GB RAM recommended. Any GPU
with D3D11 works; NVENC/QSV/AMF used when present with automatic software
fallback (x264/x265/SVT-AV1).

**Development:** CMake ≥ 3.21, Ninja, MSVC 2022 (Windows) or GCC/Clang
(Linux), Qt 6.5+ (LGPL), GTest. FFmpeg (LGPL) and whisper.cpp arrive in
M1/M7 behind build options.

## Build

### Windows (one command)

**Release (easiest):** double-click `release.bat` in the repo root — it
finds Visual Studio 2022 and produces `dist\ArenaEdit-0.1.0-portable-win64.zip`
(unzip → double-click `Run Arena Edit.bat`) plus the branded installer.
Full details: [docs/RELEASE_WINDOWS.md](docs/RELEASE_WINDOWS.md).

```powershell
# From a VS 2022 Developer Prompt, with vcpkg available:
tools\build.ps1 -Tests
```

Produces `build\app\Release\ArenaEdit.exe`, runs the tests, and runs the
headless selftest. The full-media build (import/decode/export/STT) adds
`-WithMedia -FfmpegDir <ffmpeg-dev-prefix>` (a vcpkg port dir or
ffmpeg.org build with `include/lib/bin`; whisper.cpp compiles from the
vendored tree). `tools\release.ps1` (what `release.bat` runs) adds the
portable staging + installer + zip. Code-signing: set `CERT_PATH` /
`CERT_PASSWORD` for an authenticated build; without them the package is
unsigned.

### Linux (dev/CI — same sources)

```bash
sudo apt install cmake ninja-build qt6-base-dev libgtest-dev libgl1-mesa-dev \
  pkg-config libavformat-dev libavcodec-dev libavutil-dev \
  libswscale-dev libswresample-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DARENA_BUILD_TESTS=ON \
  -DARENA_ENABLE_FFMPEG=ON -DARENA_ENABLE_WHISPER=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/app/arenaedit --selftest     # headless end-to-end check
bash tools/benchmark.sh              # start time / RAM / size / export FPS
```

(Without the FFmpeg dev packages the app builds and tests with media
stubs — the core never depends on media or AI.)

### CI

`.github/workflows/ci.yml`: Linux (build + zero-warning gate + tests +
selftest + benchmark + artifact) and Windows (vcpkg Qt + MSVC build + tests
+ selftest + portable payload + Inno installer + artifact).

## Adding a language

1. Copy `i18n/en.json` → `i18n/<bcp47>.json` (e.g. `fr.json`).
2. Set `meta.bcp47`, `meta.nativeName`, `meta.dir` (`ltr`/`rtl`).
3. Translate every string; keep `{placeholders}`; keep the product name
   **Arena Edit** and the credit **Made by arenatechofficial.pages.dev**
   untranslated.
4. Save as UTF-8. The language appears in the picker automatically — no code
   change, no build step. Details: [i18n/README.md](i18n/README.md).

## Using AI (Kilo Gateway)

AI is **off by default** and the app works 100 % offline without it.

1. **Optional API key** (raises limits): set the `KILO_API_KEY` environment
   variable, or store one via the secure store (`ai/keystore.cpp` — DPAPI on
   Windows). Anonymous use of `:free` models works at ~200 requests/hour/IP.
2. **Enable AI**: View ▸ AI ▸ Enable AI (a one-time privacy notice is shown
   — only *text* is ever sent, never media).
3. **Choose a model**: Settings ▸ AI. Default `auto` = first `:free` model
   from the live `GET /models` list. Free models change over time; pick any
   model from the list, or leave `auto`. The status bar shows your hourly
   request budget: "AI: N of M requests used this hour".
4. **Use it**: AI panel tasks (translation, chapters, titles, cuts as their
   phases land) or "Ask AI" / the palette's `/ai` mode. Every result is a
   reviewable card — **Accept / Edit / Reject** — and nothing is applied
   without your confirmation. NL commands are validated against a strict
   JSON schema, previewed, then executed as undoable actions.

Full protocol details, prompt templates, the NL action schema, rate-limit
strategy, and **real recorded gateway responses**:
[docs/AI_INTEGRATION.md](docs/AI_INTEGRATION.md). Privacy:
[docs/PRIVACY.md](docs/PRIVACY.md).

## Repository layout

```
app/     UI shell: window, panels, dialogs, themer, command palette
core/    project model, commands/undo, settings, logging, autosave, util
media/   FFmpeg boundary (M1): probe, proxies, waveforms
render/  compositor + effect registry (M3)
ai/      Kilo Gateway client, prompts, action validator, key store
stt/     whisper.cpp boundary (M7)
i18n/    translation catalogs (en, ar, es, fr, hi, ja, ur, zh) + drop-in loader
assets/  theme tokens, fonts notes, sample project
tests/   GTest unit tests (113) + GUI smoke + fuzzed import soak
tools/   build.ps1, benchmarks, Inno Setup script, export bench driver
docs/    plan, architecture, UI guide, performance, AI, privacy, progress,
         decisions, test report
```

## Documentation

- [docs/PLAN.md](docs/PLAN.md) — milestones & exit criteria
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — layering + Mermaid diagram
- [docs/UI_GUIDE.md](docs/UI_GUIDE.md) — design tokens, layout, shortcuts
- [docs/PERFORMANCE.md](docs/PERFORMANCE.md) — benchmarks & method
- [docs/AI_INTEGRATION.md](docs/AI_INTEGRATION.md) — gateway protocol, prompts, schema, recorded responses
- [docs/PRIVACY.md](docs/PRIVACY.md)
- [docs/PROGRESS.md](docs/PROGRESS.md) / [docs/DECISIONS.md](docs/DECISIONS.md)
- [docs/TEST_REPORT.md](docs/TEST_REPORT.md) — results + known limitations

## License

Arena Edit is distributed under the terms described in
[THIRD_PARTY.md](THIRD_PARTY.md) (Qt & FFmpeg LGPL, whisper.cpp MIT,
icons CC, code as stated there).

**Arena Edit — Made by [arenatechofficial.pages.dev](https://arenatechofficial.pages.dev)**
