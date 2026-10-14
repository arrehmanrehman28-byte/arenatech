# Arena Edit — Test Report (M0)

Date: 2026-10-12 · Harness: Linux/Debian 13, GCC 14.2, Qt 6.8.2, GTest 1.16
Windows verification: covered by the CI Windows job (MSVC 2022 + vcpkg Qt);
Windows-only paths (WASAPI/DPAPI/D3D11/Inno) are compile-guarded and not
executable in this environment.

## 1. Unit tests — 34/34 PASS

```
 1. ProjectModel.JsonRoundTrip                Pass
 2. ProjectModel.FileSaveLoad                 Pass
 3. ProjectModel.SchemaVersionGate            Pass   (future version rejected, error named)
 4. UndoStack.RenameCommandRoundTrip          Pass
 5. UndoStack.MemoryCapEvictsOldest           Pass
 6. Settings.DefaultsAreConservative          Pass   (AI off, snap on, 60 s autosave, budget 200)
 7. Timecode.FramesToSmpteAndBack             Pass   (25/30 fps round-trips)
 8. Autosave.RecoveryFlow                     Pass   (clean marker / newer autosave / clear)
 9.  I18nTest.DiscoversShippedLanguages       Pass
10.  I18nTest.RtlDetection                    Pass   (ar, ur → RTL; en → LTR)
11.  I18nTest.TranslationsResolveAndFallBack  Pass
12.  I18nTest.PlaceholdersSubstitute          Pass   (en + ar)
13.  I18nTest.BrandingStableAcrossLocales     Pass   (name + credit identical in en/ar/ur)
14.  I18nTest.MissingKeyReturnsKeyItself      Pass
15.  I18nTest.DropInLanguageAppears           Pass   (new catalog picked up with no code change)
16.  ActionValidator.AcceptsValidTransitionAction        Pass
17.  ActionValidator.AcceptsExportWithSubtitles          Pass
18.  ActionValidator.AcceptsMultiActionList              Pass
19.  ActionValidator.RejectsUnknownOp                    Pass
20.  ActionValidator.RejectsBadTargetType                Pass
21.  ActionValidator.RejectsOutOfRangeSpeedAndDuration   Pass
22.  ActionValidator.RejectsEmptyList                    Pass
23.  ActionValidator.ExtractsArrayFromProseAndFences     Pass
24.  ActionValidator.ExtractorHandlesStringsContainingBrackets Pass
25.  ActionValidator.ExtractorReturnsEmptyOnGarbage      Pass
26.  ThemerFixture.LoadsAllThreeThemes                   Pass
27.  ThemerFixture.AccentColorPropagatesToQss            Pass (incl. AI violet presence)
28.  ThemerFixture.SurfacesHaveFiveElevationLevels       Pass
29.  ThemerFixture.HighContrastTextMeetsWcagOnBackground Pass (ratio 21:1 ≥ 4.5:1)
30.  MediaProbe.DetectsContainersByExtension     Pass
31.  MediaProbe.ProbesExistingFile               Pass
32.  MediaProbe.MissingFileFlagged               Pass
33.  RenderBoundary.HonestAvailability           Pass (registry contract, compositor OFF until M3)
34.  SttBoundary.HonestAvailability              Pass (whisper OFF until M7, clear error)
```

## 2. Headless app selftest (`arenaedit --selftest`) — 14/14 PASS

```
[PASS] i18n loaded 3 locales
[PASS] ar detected as RTL
[PASS] ar translation resolves
[PASS] placeholder substitution
[PASS] three themes present
[PASS] project save
[PASS] project round-trip
[PASS] command executes
[PASS] command undoes
[PASS] command redoes
[PASS] validator accepts valid action
[PASS] validator rejects unknown op
[PASS] main window constructed
[PASS] ur switches app layout to RTL
SELFTEST PASS
```

## 3. Live gateway verification (recorded 2026-10-12)

| Request | Result |
|---------|--------|
| `GET /api/gateway/models` | 200, 0.28 s, 390 models / 16 free |
| `POST /chat/completions` (non-stream, free model) | 200, 3.7 s; `content:null` + `reasoning` populated; `finish_reason:"length"` |
| `POST /chat/completions` (`stream:true`) | 200, SSE `chat.completion.chunk` frames, `delta.reasoning` tokens, final `finish_reason` |

Full captured payloads: `docs/AI_INTEGRATION.md` §1–3.

## 4. Performance (see PERFORMANCE.md)

| Metric | Result | Target |
|--------|--------|--------|
| Cold start avg (5) | 92 ms | < 2000 ms ✅ |
| Idle RSS (empty project) | 46 MB | < 150 MB ✅ |
| Core binary | 0.75 MB | < 60 MB ✅ |

## 5. Manual checks (headless/offscreen)

- Normal launch stays alive, writes daily log to
  `%LOCALAPPDATA%\ArenaEdit\logs\arena-YYYYMMDD.log` ✅
- No stylesheet parse errors after the `%2px` QSS fix (re-verified) ✅
- RTL: `ur` switch flips app layout direction; timeline stays LTR (code) ✅

## 6. Known limitations (honest list)

1. **No media yet (M1)**: Media Bin imports file paths and flags missing
   files, but there is no decode/preview/thumbnails/waveforms. The monitor
   transport is a virtual playhead.
2. **No clips on the timeline (M2)**: ruler/lanes render; clip
   manipulation, ripple/roll/slip/slide, and snap feedback are M2.
3. **No rendering pipeline (M3)**: effect registry is the contract;
   compositor is a stub reporting `isAvailable() == false`.
4. **No export (M4)**: Export menu item shows a status-bar note;
   batch queue is M4.
5. **AI automation features are UI stubs (M6)**: Ask AI works end-to-end
   against the live gateway; translate/chapters/titles/cuts cards are
   inert until their phase (prompts + validator already built and tested).
6. **STT not compiled (M7)**: `Transcriber` returns a clear
   "not compiled in this build" error; whisper.cpp binding is M7.
7. **Settings pages partially populated**: Appearance/Language/AI/Privacy
   are live; Editing/Playback/Export/Hardware/Keyboard show "available in
   a later phase" (they become live with their milestones).
8. **Icon set is 24 px source SVGs**; 16/20 px variants + app icon are M8/M9.
9. **Onboarding tour** (60 s, skip/replay) is M8.
10. **Windows-only verification pending CI runner**: DPAPI key storage,
    WASAPI, D3D11, registry theme probe, and the Inno package compile
    behind guards but are only executed on the Windows CI job.
11. **Arabic/Urdu translations** were authored by the agent and benefit
    from native-speaker review before M9 release.
12. **CI Windows job** is defined (vcpkg Qt + MSVC + Inno) but has not
    executed in this environment (no Windows runner here); the Linux job
    is fully green.

---

## Addendum — M5 Subtitles (2026-10-13)

Per-milestone verification evidence lives in `PROGRESS.md`; this report
keeps the M0 snapshot above. Current totals after M5:

- **ctest: 88/88 pass** (87 unit + guismoke), **0 warnings** on a clean
  rebuild; `Guismoke.MediaPipeline` includes 4 M5 checks (panel exists,
  panel attached, burn-in renders, burn-in changes monitor pixels).
- New suites: `Subtitles.*` (byte-stable SRT/VTT/ASS round-trips, time
  codes, frame conversion, reading-speed thresholds, RTL detection,
  parse errors), `ProjectModel.SubtitleTrackRoundTripWithCues`,
  `BurnIn.*` (Urdu + Arabic golden frames, multi-line, disabled track),
  `I18nTest.ShippedCatalogsHaveKeyParity` (all 8 catalogs in lockstep,
  branding never localized).
- i18n now ships all 8 spec languages (en, ur, ar, es, fr, hi, zh, ja).

New known limitations (M5):

- Burn-in goldens are environment-specific (bundled Noto Sans Arabic +
  this sandbox's fontconfig); they skip honestly where no
  Arabic-capable font resolves. Windows uses Arial (ships an Arabic
  subset).
- The ASS encoder emits a minimal fixed scaffold (one `Default`
  style); exotic style setups round-trip cues but not original styling.
- Burn-in style is fixed (bottom-center, white text, black outline,
  canvas/20 font size); per-track styling is deferred to M8 polish.
- es/fr/hi/zh/ja translations are agent-authored and benefit from
  native-speaker review before M9 (same caveat as ur/ar, #11 above).
## Addendum — M6 AI features (2026-10-13)

Per-milestone verification evidence lives in `PROGRESS.md`; this report
keeps the M0 snapshot above. Current totals after M6:

- **ctest: 102/102 pass** (101 unit + guismoke), **0 warnings** on a clean
  rebuild. Test count grew 88 → 99 (M6.1: 11 `AiFeatures.*` tests) → 102
  (M6.3: 3 format-echo extraction regression tests; the cut EOF-boundary
  expectation was corrected for D-030).
- New suites (M6.1): `AiFeatures.BatchRangesCoverEverythingExactlyOnce`,
  `ParseClockVariants`, `TranslationParsesFencedArray` / `TranslationRejections`,
  `ChaptersParsesAndValidates` / `ChaptersRejections`, `TitleParsesAndValidates`
  / `TitleRejections`, `CutsParsesAndValidates` / `CutsRejections`,
  `PromptBuildersSubstitutePlaceholders`.
- New suites (M6.3, from live findings): `TranslationIgnoresEchoedTemplate`,
  `TitleIgnoresEchoedTemplate`, `CutsIgnoresEchoedTemplate` (extractors must
  return the model's final answer, not the format template echoed in its
  chain-of-thought — D-028).
- guismoke: 70 checks including the M6 section (AI panel present, four
  task cards, Accept applies an undoable edit, undo/redo round-trip);
  6/6 direct offscreen runs clean.
- **Live gateway session (recorded, in-repo):**
  `docs/live_sessions/m6_ai_live_session_2026-10-13.md` — all four M6
  tasks (title/description, 7 chapters, 45/45-line Spanish translation in
  40+5 batches, 4 cut suggestions) passed offline validation against real
  `nvidia/nemotron-3-super-120b-a12b:free` responses; 0 network errors;
  driver exit 0. Model-selection evidence for the other free models
  (`kilo-auto/free` 503, `lfm-2.5-2.6b` 39/40 entries, `ling-3.1-flash`
  429) is in `AI_INTEGRATION.md` §8.

New known limitations (M6):

- Free-tier model quality varies by day and by model; the recorded session
  used the model verified stable that day. The driver default and the
  client's `auto` resolution can drift (D-007) — re-run
  `tools/arena_ai_live` when pinning changes.
- The AI **description** has no project field to persist into; Accept
  applies the title (undoable project rename) only (D-023).
- Cuts apply to the video primary track only; audio/subtitle tracks are
  not auto-shifted (D-024).
- The response cache stores the gateway's raw reply; a reply that fails
  validation is invalidated before any resend (D-029), so a "retry" always
  re-queries the gateway.

## Addendum — M7.1 Local STT (whisper.cpp, 2026-10-14)

Per-milestone verification evidence lives in `PROGRESS.md`; this report
keeps the M0 snapshot above. Current totals after M7.1:

- **ctest: 103/103 pass** (102 unit + guismoke), **0 warnings** on a clean
  full rebuild (including the vendored whisper.cpp + ggml CPU sources).
  Test count grew 102 → 103.
- New suite: `Stt.TranscribeJfkFixture` — end-to-end local transcription of
  the bundled `jfk.wav` (11 s, public domain, shipped with whisper.cpp)
  through the real `AudioReader` → `whisper_full` path. It **skips
  honestly** when no model has been downloaded (the suite must stay green
  offline / no network in CI); when a model is present (via
  `ARENA_WHISPER_MODELS` or a prior `tools/arena_stt_check` run) it asserts:
  `< 60 s` wall time, detected language `en`, ≥ 1 segment, non-empty text,
  word spans within their segment and non-overlapping, and **no leaked
  whisper control/timestamp tokens** (`[_BEG_]`, `[_TT_*]`, `_`).
- The pre-existing `SttBoundary.HonestAvailability` now has a
  build-aware branch (`ARENA_ENABLE_WHISPER`): with the engine compiled in
  it asserts `isAvailable()==true`; without it, `false` (the M0 boundary
  contract is preserved in both configurations).
- Targeted STT runs (`tools/arena_stt_check`, 2 threads on this box):
  **tiny ≈ 1.48 s**, **base ≈ 3.0 s** for the 11 s clip — far inside the
  M7 acceptance bar (< 60 s on 4-core CPU). Correct output text, monotonic
  word timestamps, clean language.

New known limitations (M7.1):

- whisper v1.9.5's `detect_language=true` returns zero segments on short
  English audio (verified); the app therefore uses an **explicit** language
  (default `en`) and does not auto-detect (D-032).
- Models are fetched on first use from Hugging Face; the *first* run needs
  network (size + SHA-256 pinned, D-033). Subsequent runs are fully
  offline. The default `base` model is a 147.9 MB download.
- CPU-only: no CUDA/Vulkan in this build (spec: CPU default); thread count
  is capped at `min(4, idealThreadCount)`.

