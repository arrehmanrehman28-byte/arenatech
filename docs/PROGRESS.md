# Arena Edit — Progress Log

*Verified-phase log (working rule #2). Every entry: build clean (zero
warnings) → tests → targeted checks → evidence.*

## 2026-10-14 — M7.1 Local STT (whisper.cpp): verified ✅ (M7 part 1)

**Scope delivered — `stt/` becomes a real, on-demand local transcriber**

- **Vendored + integrated whisper.cpp v1.9.5 (MIT)** as a CMake
  subproject, CPU-only, built static (`libwhisper.a` + `ggml`). New CMake
  option `ARENA_ENABLE_WHISPER` (default **OFF**) — when ON, `arena_stt`
  links the vendored target; when OFF, the same interface file compiles to
  honest "not available" results (no fake output, per the STT boundary
  rule). No CUDA/Vulkan — this build is CPU by design (spec: CPU default).
- **On-demand model download + verification** (`Transcriber::ensureModel`):
  `tiny` (77.7 MB), `base` (147.9 MB), `small` (487.6 MB) are fetched on
  first use from the official `ggerganov/whisper.cpp` Hugging Face repo,
  written to a `.part` file, then **size- and SHA-256-checked against
  pinned constants** before being moved into the model dir
  (`AppLocalDataLocation/whisper`, overridable via `ARENA_WHISPER_MODELS`
  for tests/CI). A corrupt/partial file is deleted and re-fetched. No
  network call is made to *transcribe* — only to obtain the model once.
- **`Transcriber::transcribe`** decodes any audio through the existing
  `AudioReader` → 16 kHz mono float32, runs `whisper_full` on a
  CPU-only context, and returns **segment-level and word-level**
  timestamps (`TranscriptSegment` + `WordSpan`). Word spans come from
  whisper's per-token `t0/t1` (centisecs → ms), filtered to real words
  (control + `[_TT_*]` timestamp tokens dropped). The model's own stderr
  is routed to the `arenaLog` **debug** level, not the user console.
- **Language is explicit, never auto-detected** (see D-032). Default
  language `en`.

**Verification**

- Build: full tree (all targets incl. `arena_tests`, `arena_guismoke`,
  `arena_ai_live`, `arena_stt_check`), **0 warnings** (`-Wall -Wextra`),
  whisper + ggml compiled from the vendored sources.
- ctest: **103/103** — was 102/102; `+1` new
  `Stt.TranscribeJfkFixture` (skips honestly when no model is downloaded,
  so the suite stays green offline; runs end-to-end when a model is
  present).
- Targeted STT runs (`tools/arena_stt_check`, the bundled `jfk.wav`,
  11.00 s, 2 threads on this box):
  - **tiny**: 1 segment, 22-word span, `en`, **≈ 1.48 s** wall.
  - **base** (product default): 1 segment, 22-word span, `en`,
    **≈ 3.0 s** wall.
  - Output: *"And so my fellow Americans ask not what your country can do
    for you, ask what you can do for your country."* — correct, with sane
    monotonic word timestamps and **no** leaked `[_BEG_]/[_TT_*]` tokens.
- **M7 acceptance target "transcription of bundled sample clip < 60 s on
  4-core CPU": MET with wide margin.** This sandbox has only 2 cores and
  even the default `base` model finishes in ~3 s (≈ 1/20 of the budget);
  a 4-core machine will be faster still.

**Bugs found + fixed by the real run**

- whisper v1.9.5 `detect_language=true` returned **zero segments** on the
  11 s English sample (rc=0, lang detected as `en`) for both tiny and base
  — an upstream behavior, not our decode. Worked around by using an
  explicit `language` and `detect_language=false` (D-032); decode was
  verified correct in isolation (176 000 samples, healthy ±0.78 range)
  before the workaround.
- v1.9.x special-token layout: `beg` (50364) is *above* `eot` (50257), so
  the classic `id >= beg && id <= eot` range test is empty and let
  `[_BEG_]` through as a "word". Filter is now `id >= beg` (upper-open)
  plus the individual control tokens (D-034).
- `QVector`/`QList` (Qt 6) has no pointer-pair `append`; sample
  accumulation uses `std::vector` (D-035).

**Not done / next**

- M7.2 — feed the transcript into the M6 features (chapter/cut/title
  suggestions from *local* text) and surface a "Transcribe" action in the
  UI with a model picker + progress.
- M7.3 — NL command bar (`/ai`) end-to-end against the transcript +
  silence map, with the strict JSON action schema, preview + confirmation,
  and undoable apply (acceptance: "add a fade-out to the last clip" →
  validated preview → undoable apply, tested).
- Wire `Transcriber` into the AI panel so a clip's local transcript can
  seed the cut/chapter/title prompts (text only ever leaves the machine).

## 2026-10-14 — M7.2/M7.3 Transcript→features + NL command bar: verified ✅ (M7 complete)

**Scope delivered**

- **M7.2 — local transcript feeds the M6 features** (this entry):
  - AI panel **Transcribe** section: model picker (tiny/base/small, with
    size), one-click local transcription of the first video clip on a
    worker thread (on-demand SHA-256-pinned model, download + progress
    reported), read-only transcript view. Honest availability: the section
    is disabled with an explanation when whisper.cpp is not compiled in,
    or when there is no project/clip.
  - `transcriptLines()` now **prefers the local STT transcript** over
    subtitle cues for the cut/chapter/title prompts — "edit suggestions
    from transcript + silence map" from the plan. The transcript is
    in-memory per project (cleared on project switch; never persisted —
    D-036). Pure formatter `transcriptLines(segments, fps)` unit-tested.
- **M7.3 — NL command bar (`/ai`)** (this entry):
  - Palette `/ai <request>` is now the command bar (the panel's own input
    keeps free-form Ask AI). Flow: `actionPrompt` (schema + machine
    project context: fps, clip list, duration, markers) → streamed model
    response → `parseActions` (last-valid-candidate extract + strict
    `actionValidator` schema) → **preview card** with translatable lines
    per action → Accept → `applyAiActions` → **one undoable command per
    applicable action**.
  - Apply mapping (D-037): `add_transition` → `effects.transition`
    `{type, duration:frames}` — the exact shape the M3 compositor and
    exporter already render (`fade_to_black` on the last clip is a fade-out
    with no next clip); `set_clip_speed` → `Clip.speed`; `add_marker` →
    labeled marker (time in project frames); `export` → opens the export
    dialog with the preset preselected (`preselectPreset`), the user still
    confirms. Two gates: validation at parse, grounding at apply (D-038).
  - 24 new i18n keys × 8 catalogs (355 keys each).

**Verification**

- Build: full tree, **0 warnings**.
- ctest: **110/110** (was 103 after M7.1): +6 NL unit tests
  (`ActionPromptSubstitutesContextAndRequest`, `ParseActionsAcceptsValidArray`,
  `ParseActionsAcceptsAllSupportedOps`, `ParseActionsRejections` (8 cases),
  `ParseActionsIgnoresEchoedTemplate`, `ParseActionsSurvivesUnbalancedEcho`)
  + `Stt.TranscriptLinesFormat` (M7.2 formatter).
- guismoke: **+16 M7 checks** (85 total), 6/6 direct runs clean —
  including the **M7 acceptance flow**: "add a fade out to the last clip"
  response → applied → last clip carries `fade_to_black` @ 0.5 s frames →
  **the compositor's final frame is (near-)black** → undo removes it;
  plus unknown-op no-op, speed 1.5× + undo, labeled marker + undo,
  out-of-range index skip.
- **Live gateway session, all tasks green** (`exit 0`, 0 network errors):
  [`docs/live_sessions/m7_smart_live_session_2026-10-14.md`](live_sessions/m7_smart_live_session_2026-10-14.md)
  — all four M6 tasks re-passed, and the three NL commands produced valid,
  correct actions: fade-out on the last clip (index -1), 1.5× speed on
  clip 1, marker at frame 360 ("12 seconds" at 30 fps). Model:
  `nvidia/nemotron-3-super-120b-a12b:free` (AI_INTEGRATION.md §9).

**Bugs found + fixed by the live run** (regression tests each)

- `extractJsonArray` aborted at the first *unbalanced* `[` (a half-written
  CoT template echo: `..."duration_ms":???}`) and never saw the real
  answer array that followed — now resumes scanning (D-028 extension).
- The NL prompt made `duration_ms` look required, so a conservative model
  emitted `[]` for "add a fade out to the last clip" (no duration given) —
  prompt now documents it as optional (editor default 0.5 s).

**Not done / next**

- M8 Polish: onboarding tour, a11y audit, DPI 100–300 %, icon set,
  screenshots.
- M9 Release: portable .exe, Inno package, benchmark report.
- Product decision (anytime): whether a project's STT transcript should
  ever be persisted to the `.aep` (currently deliberately not — D-036).

## 2026-10-13 — M6 AI features: verified ✅

**Scope delivered**

- **M6.1 — offline parse/validate** (commit `cbeb644`):
  `ai/aifeatures.{h,cpp}` — `batchRanges` (40/batch, exact cover),
  `parseTranslation` (exactly N non-empty strings, same order),
  `parseChapters` (3–10, non-decreasing `HH:MM:SS` ≤ duration, title ≤ 6
  words), `parseTitle` (≤ 60 / ≤ 300), `parseCuts` (≤ 20, frame-exact
  SMPTE, reason ∈ {silence, filler}, confidence ∈ [0,1]), prompt builders
  with placeholder substitution; `parseClock` accepts `HH:MM:SS`,
  `HH:MM:SS:FF` / `,FF` and fused `SSFF` variants. 11 unit tests;
  99/99 ctest.
- **M6.2 — GUI wiring** (commit `18a0778`): `AiPanel` runs the four tasks
  + free-form "Ask AI" against `KiloClient` (streaming preview, per-task
  result cards, Accept/Reject, **never auto-applied**); `MainWindow`
  applies accepted results as undoable commands — translation → new
  subtitle track cues, chapters → markers, title → project rename
  (description shown only, no project field — D-023), cuts → per-cut
  ripple deletes on the video primary track (D-024). 29 i18n keys × 8
  catalogs (331 keys each); guismoke M6 checks. Also fixed the export
  QThread teardown race found by guismoke (D-025). 99/99 ctest.
- **M6.3 — live gateway session + docs** (this entry):
  - `tools/arena_ai_live` dev driver: runs all four tasks against the live
    gateway on a synthetic 45-cue, 30 fps, text-only session (batches of
    40+5 with one failed-batch retry available), logs every prompt + raw
    response to markdown, exits non-zero on any validation/network
    failure. Model = CLI argument (default: last model verified stable);
    settings isolated to a temp dir (D-031).
  - **Recorded session, all four tasks green** (`exit 0`):
    [`docs/live_sessions/m6_ai_live_session_2026-10-13.md`](live_sessions/m6_ai_live_session_2026-10-13.md)
    — title 44 ch + description 177 ch; 7 chapters; 45/45 lines → Spanish
    in 2 batches (40 + 5); 4 cuts (3 silences + the "um," filler at
    `00:00:24:27 → 00:00:25:12`). Model: `nvidia/nemotron-3-super-120b-a12b:free`
    (model-selection evidence in AI_INTEGRATION.md §8: `kilo-auto/free`
    routed to a 503-overloaded upstream; `lfm-2.5-2.6b` dropped one of 40
    entries; `ling-3.1-flash` 429).
  - **Shipped bugs found only by the live run** (each with regression
    tests): SSE `indexOf('\n')`-as-boolean dropped every line after the
    first data line (D-026); flat 1500 `max_tokens` truncated
    reasoning-model JSON (D-027); extractors returned the model's echoed
    *format template* instead of the answer (D-028); cache served an
    unparseable response to retries and delivered empty text on cache hit
    (D-029); valid EOF-ending cuts rejected (D-030). Prompt hardening:
    frame field < fps, end strictly after start, filler ≈ 0.5 s (the
    model emitted frame 30 at 30 fps and zero-length filler cuts).
  - Docs: `AI_INTEGRATION.md` §8 (recorded session + findings),
    `DECISIONS.md` D-023…D-031, `TEST_REPORT.md` M6 addendum, PLAN M6
    verified.

**Verification**

- Build: 100 % of targets, **0 warnings** (`-Wall -Wextra`).
- ctest: **102/102** (M6.1/6.2: 99 → +3 format-echo regression tests;
  cut EOF boundary expectation updated for D-030).
- guismoke: 70 checks; 6/6 direct runs clean (offscreen).
- Live: 4/4 tasks passed validation, **0 network errors**, recorded in
  repo (above); app remains 100 % functional with AI disabled/offline
  (no AI code path in core editing; `chat()` refuses locally when
  disabled).

**Not done / next**

- NL command bar (M7, with STT/whisper) — `actionvalidator` + prompt
  ready.
- Project description field: product decision needed before the AI
  description can be persisted (D-023).
- Free-tier model quality varies per day; re-verify the driver default
  when pinning changes (D-007, AI_INTEGRATION.md §8).

## 2026-10-13 — M5 Subtitles: verified ✅

**Scope delivered**

- **Subtitle core** (`core/subtitles.{h,cpp}`): lenient parsers +
  canonical encoders for SRT, WebVTT, ASS; auto-detection by content
  (WEBVTT header / `[Events]`/`[Script Info]`) with extension fallback;
  time codes `HH:MM:SS,mmm` / `HH:MM:SS.mmm` (`MM:SS` accepted) /
  `H:MM:SS.cc`; `secToFrame`/`frameToSec` at project fps; reading-speed
  check (> 42 visible chars or > 17 cps → `CueWarning`); RTL helpers
  (`isRtlLanguage` BCP-47, `textIsRtl` first-strong-character).
- **Project layer** (`core/project.{h,cpp}`): `SubtitleTrack`
  (id/language/name/`burnIn`/cues) + `TimelineCue` (startFrame/endFrame
  exclusive/text), additive JSON fields — old files load unchanged
  (no schema bump, D-021).
- **Burn-in** (`render/compositor.cpp`): `renderFrame` now bakes every
  enabled subtitle track after the video tracks — bottom-center, white
  with black outline, size = canvas/20, word-wrapped at 90 % width,
  RTL via `QTextLayout` bidi (no per-language layout code). Font family
  is a process-wide default (`Compositor::setDefaultSubtitleFont`,
  "Arial" — Windows ships an Arabic subset); the sandbox/CI points it
  at the bundled Noto Sans Arabic fixture (D-022). The preview fast
  path now forces the compositor when any track has `burnIn` on.
- **Panel** (`app/panels.{h,cpp}` M5.3): track list (name / BCP-47 /
  burn-in checkbox), cue table (SMPTE `HH:MM:SS:FF` start/end + text),
  add/delete track, add/delete/edit cue (dialog with live
  reading-speed warning), SRT/VTT/ASS import (auto-detect + language
  prompt) and export (byte-identical to the canonical encoders);
  flagged cues are underlined + tinted with the reason in the tooltip.
  Panel mutates the project and emits `subtitlesChanged` → autosave.
- **i18n**: all M5 strings translated; **five new catalogs** (es, fr,
  hi, zh, ja) for the full spec set of 8 languages; new
  `I18nTest.ShippedCatalogsHaveKeyParity` enforces identical key sets
  and untranslated branding in every catalog; discovery test now
  asserts all 8.

**Exit criteria (PLAN M5)**

- Round-trip SRT/VTT/ASS byte-stable on the sample set:
  `Subtitles.SrtRoundTripByteStable` / `VttRoundTripByteStable` /
  `AssRoundTripByteStable` — fixtures in `tests/fixtures/subs`
  (4 cues: multi-line text, hour cue, VTT NOTE region, ASS override
  tags), `encode(parse(bytes)) == bytes`.
- Urdu + Arabic burn-in golden frames: `BurnIn.UrduBurnInGolden` /
  `BurnIn.ArabicBurnInGolden` (Noto Sans Arabic, deterministic in this
  environment; regenerated with `ARENA_GOLDEN_UPDATE=1`), plus
  `BurnIn.MultiLineCueBurnIn` and `BurnIn.DisabledTrackIsNotBaked`.
- End-to-end through the real UI: `Guismoke.MediaPipeline` M5 section
  (4 checks) verifies the panel exists, re-attaches to the project,
  renders a burn-in frame, and that toggling burn-in changes the
  monitor pixels at the same playhead.

**Verification (g++ 14.2, Qt 6.8.2, FFmpeg 7.1)**

- Clean rebuild: **0 errors, 0 warnings**.
- ctest: **88/88 pass** — new: 12 `Subtitles.*`/`ProjectModel.*` +
  4 `BurnIn.*` + 1 `I18nTest.ShippedCatalogsHaveKeyParity`; all 71
  pre-M5 tests still pass.
- `Guismoke.MediaPipeline`: **PASS** (M5: panel exists / attached /
  burn-in renders / burn-in changes monitor pixels).

**Bugs found & fixed**

- Canonical line endings: the first encoder pass emitted lone `\r`
  where the fixtures (and the formats) use CRLF; ASS section
  separators same class. Fixed and pinned by the byte-stable tests.
- Multi-line cue text: fixtures keep bare `\n` *inside* cue text while
  record lines end in CRLF — encoders must not "normalize" in-text
  newlines (byte-stable tests catch any drift).
- `QTextLine::setWidth` does not exist in Qt 6.8 — it is
  `setLineWidth`; `QFontDatabase::addApplicationFont` is broken in
  this build (returns 0 for every file, even system DejaVu) — the
  burn-in font is loaded through a workspace-local `FONTCONFIG_FILE`
  pointing at the bundled font directory (D-022), with a clean
  `GTEST_SKIP` when no Arabic-capable font exists.
- Preview fast path skipped the compositor (and thus burn-in) when no
  clip had effects — condition extended with `anyBurnIn`.
- Name collision: project cue renamed `SubtitleCue` → `TimelineCue`
  (the format-level `arena::SubtitleCue` in seconds stays as-is).
- Guismoke burn-in diff needed (a) `seekToFrame` +
  `applyPlaybackResolution` — the paused engine delivers no frames on
  a same-position seek — and (b) size normalization: the burn-in pass
  composites at canvas resolution while the plain pass uses the native
  source frame on the fast path.

**Known limitations**

- Burn-in goldens are environment-specific (font/hinting); they run
  in this sandbox and skip honestly elsewhere. On Windows the default
  font is Arial (Arabic subset) — no goldens run there.
- The ASS encoder writes a minimal fixed scaffold (`[Script Info]`,
  one `Default` style, `[Events]`); exotic style setups round-trip
  their cues but not the original styling.
- Burn-in position/style is fixed (bottom-center, white/black,
  canvas/20 font) — per-track styling lands with M8 polish if needed.

**Not done / next**

- M6 AI features (subtitle translation batching, chapters, titles,
  cut suggestions) against the live gateway.
- M7 STT (whisper.cpp) + NL command bar end-to-end.

## 2026-10-13 — M4 Export: verified ✅

**Scope delivered**

- **Core pipeline** (`render/ffmpeg_export.cpp`, `Exporter::exportProject`):
  project frames through the M3 compositor → YUV420P → encoder (CRF
  quality); audio timeline from clip spans with tail-crossfades and
  silence padding to the project end, 48 kHz stereo; encoder
  frame-size aware (1024 AAC / 960 Opus); progress callback + cancel
  token.
- **Presets & selection** (`render/exporter.cpp`): mp4-h264-high /
  balanced / web-720, mp4-hevc, mp4-audio, webm-vp9; video fallback
  chain (NVENC/QSV/AMF candidates → x264/x265/SVT-AV1/AOM/VP9), audio
  selection (AAC/Opus); size/time estimates for the UI.
- **Export queue** (`render/exportqueue.{h,cpp}`): sequential jobs on
  a worker thread from deep-copied snapshots (D-020); state returns
  via queued signals; cancel stops the running job at the next frame
  boundary; no UI freezes (spec).
- **Export dialog** (`app/exportdialog.{h,cpp}`, File → Export,
  Ctrl+Shift+E): preset checklist with live per-preset and total
  size/time estimates, scale 25–200 %, custom-quality slider
  (CRF 1–51), audio bitrate, hardware-encode toggle, output folder,
  and a batch queue table (status / progress bar / size per job).
- **Real media fetchers**: export decodes the *original* file (never
  the proxy) with a frame-accurate one-shot `VideoDecoder` per call;
  audio via `AudioReader` with resampling, source fps from the probe.

**Exit criteria (PLAN M4)**

- Batch of 3 presets completes with correct sizes:
  `Exporter.BatchThreePresets` (mp4-h264-high, mp4-h264-balanced,
  webm-vp9) — all probe back at 1.0 s ± 0.06; higher quality not
  smaller.
- Fallback verified by simulation (injectable available-encoder list,
  `Exporter.CodecSelectionFallback`); a real iGPU hardware run stays
  for M9.
- End-to-end through the real UI: `Guismoke.MediaPipeline` M4 section
  opens the dialog, queues two presets at 25 % scale, runs the queue
  to completion, and checks the output files.

**Verification (g++ 14.2, Qt 6.8.2, FFmpeg 7.1)**

- Clean rebuild: **0 errors, 0 warnings**.
- ctest: **71/71 pass** — new: `QueueApp.RunsTwoJobsInOrderAndSucceeds`,
  `QueueApp.CancelStopsRunningJob`, `QueueApp.ProgressBoundedAndCompletes`,
  the 6 core export tests, and the guismoke M4 section (4 checks);
  all 62 pre-M4 tests still pass.
- `--selftest`: **PASS**.

**Bugs found & fixed**

- **WebM segment duration 48× too long (the big one)**: the Matroska
  muxer writes packet pts verbatim into millisecond EBML fields; with
  sample-based pts a 1 s audio track reported 48.312 s (47040 last
  audio pts in sample units + 1272 last video pts in ms). Fix: both
  output streams use a 1/1000 time base and every encoder packet is
  rescaled from its encoder time base before muxing (D-019).
  Isolated with a minimal two-variant writer (sample pts → 48.312 s;
  ms pts → 1.008 s) after source-level confirmation in
  `libavformat/matroskaenc.c`.
- AAC NaN frames: `AudioTimeline::nextChunk` tracked the interleaved
  buffer as per-channel → half the slots unwritten. Rewritten with
  per-channel positioning, L/R pair filling, and silence-padded tails
  (final partial frame now encoded; the 21 ms truncation is gone).
- libopus frame size is 960 samples at 48 kHz (1024 is AAC's) — chunk
  size now comes from `aenc->frame_size` after `avcodec_open2`.
- Video pts: encoders normalize `time_base` (VP9 → 1/1000, x264 →
  1/15360); frame pts must be rescaled into the *encoder's* time
  base, packet pts into the *stream's* (1/1000). The mp4 video
  stream silently carried the same bug (metadata duration 3.8 ms,
  masked by the audio-driven container duration).
- Leftover TEMP DEBUG scan with a stale buffer size caused OOB "NaN"
  reads after the chunk-size change — instrumentation always removed
  with the size change.

**Not done / next**

- MOV/ProRes container, WAV audio export, subtitle burn-in into
  exports (M5/M8); GPU-encode benchmark on real hardware (M9).
- M5 Subtitles.

## 2026-10-13 — M3 Rendering: verified ✅

**Scope delivered**

- **Deterministic software compositor** (`render/compositor.cpp`):
  the single place a timeline frame becomes pixels — track stacking
  (bottom → top), per-clip effect graph (transform, opacity, color
  LUTs, box blur, Ken Burns, speed), tail transitions
  (cross_dissolve, fade_to_black, wipe, push). Deterministic: golden
  frame tests compare pixels exactly (tolerance 0).
- **LRU frame cache**: 512 MB default cap (`render/cacheMB`),
  thread-safe, byte-accounted; feeds the compositor fast path and the
  monitor; live occupancy in the status bar.
- **Inspector effects editor**: spinboxes for all effect parameters;
  Apply → undoable `SetEffectsCommand`; defaults serialize to empty
  JSON so the fast path (no effects) skips the compositor entirely.
- **Monitor compositing**: when any clip has effects, the monitor
  renders through the compositor (engine-decoded frame for the
  current source, one-shot `VideoDecoder` for cross-file
  transitions).
- **Playback resolution** (Full / 1/2 / 1/4, display-only scaling)
  and a **dropped-frame counter** in the status bar.
- Preview seeking uses trimmed clips' in points
  (`in + (playhead − offset)`); fast and compositor paths agree.

**Verification (g++ 14.2, Qt 6.8.2, FFmpeg 7.1.5)**

- Clean rebuild: **0 errors, 0 warnings**.
- ctest: **62/62 pass** — new: golden-frame tests (pixel-exact,
  tolerance 0, hash `eea751c5…`), `FrameCache` LRU/eviction tests,
  and the guismoke M3 section (12 checks: apply → command →
  brighter frame → undo, 1/2 resolution, dropped label).
- `--selftest`: **PASS**.

**Not done / next**

- GPU path (D3D11/OpenGL) behind `isGpuAvailable()` — real GPU
  hardware at M8/M9; sharpen/blend/.cube effects deferred.
- M4 Export.

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
