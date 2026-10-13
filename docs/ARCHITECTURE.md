# Arena Edit — Architecture

*A professional, lightweight, multi-language video editor for Windows. Made by
[arenatechofficial.pages.dev](https://arenatechofficial.pages.dev).*

## Layering

Strict dependency direction: `app → {ai, render, media, stt, i18n} → core`.
**`core` never depends on `ai`** — the editor must run 100% offline with AI
disabled (enforced by the build graph and by tests that link `arena_core`
without `arena_ai`).

```mermaid
flowchart TB
    subgraph UI["app/ (Qt 6 Widgets)"]
        MW[MainWindow<br/>menu, docks, status bar]
        TB[Timeline<br/>ruler, lanes, snap]
        MON[Program Monitor<br/>transport, overlays]
        MB[Media Bin]
        INS[Inspector]
        SUB[Subtitle Manager]
        AIP[AiPanel]
        PAL[Command Palette Ctrl+K]
        SET[Settings / Dialogs]
        THEME[Themer<br/>tokens → QSS]
    end

    subgraph CORE["core/ (Qt Core, no UI)"]
        PROJ[Project model<br/>schema v1 + migrations]
        CMD[Command / UndoStack<br/>memory-capped]
        SET2[Settings]
        LOG[Logger]
        AS[Autosave<br/>60s + recovery]
        UTIL[util: timecode, RSS, fmt]
    end

    subgraph MEDIA["media/ (M1)"]
        MP[MediaProbe]
        VD[VideoDecoder seek+frame]
        AR[AudioReader resample 48k]
        PG[ProxyGenerator 540p]
        WF[WaveformGenerator]
        TH[Thumbnailer]
        PE[PlaybackEngine<br/>A/V sync (QAudioSink / wall clock)]
        FFM[FFmpeg LGPL<br/>demux/decode/encode<br/>NVDEC/QSV/AMF/D3D11VA]
    end

    subgraph RENDER["render/ (M3)"]
        REG[EffectRegistry]
        COMP[Compositor D3D11/OpenGL<br/>LRU frame cache 512MB]
    end

    subgraph AI["ai/ (opt-in)"]
        KG[KiloClient<br/>streaming SSE, backoff,<br/>budget, cache]
        BV[AIBudget per-hour]
        VAL[ActionValidator<br/>NL JSON schema]
        KEYS[Keystore<br/>env or DPAPI]
        PROMPTS[Prompt templates]
    end

    subgraph STT["stt/ (M7)"]
        WH[whisper.cpp local<br/>tiny/base/small on demand]
    end

    subgraph I18N["i18n/"]
        I18[I18n loader<br/>drop-in JSON, RTL]
        CAT[en, ar, ur, …]
    end

    KW[Kilo Gateway<br/>api.kilo.ai — text only]

    MW --> TB & MON & MB & INS & SUB & AIP & PAL & SET & THEME
    MW --> CORE
    TB --> PROJ & CMD
    MON --> COMP
    MB --> MP
    MB --> PG & WF
    PG & WF --> FFM
    AIP --> KG
    KG --> BV & KEYS & VAL & PROMPTS
    KG -. "text-only requests" .-> KW
    WH -. "local only, never uploaded" .-> SUB
    PROJ --> AS
    CORE -. "never imports ai/" .-> AI
    I18N --> CAT
    MW --> I18N
```

## Key components

### Project model (`core/project.h`)
- JSON with `schemaVersion` + a migration path (`loadFile` runs
  migrations oldest → newest; future versions are rejected with a clear
  error, never crashed on).
- Non-destructive: clips are *operations over source media* (`in/out/
  offset/speed/effects`); source files are never modified.
- Tracks: video, audio, subtitle, adjustment. Markers. Subtitle tracks carry
  BCP-47 language codes.

### Commands (`core/command.h`)
- Command pattern; every UI edit and every accepted AI action is a command.
- `UndoStack` is capped by count **and** bytes (default 100 / 4 MB); the
  Undo/Redo buttons show the action name ("Undo: Rename project").

### Themer (`app/themer.h`)
- `assets/theme/theme.json` is the single token source: 5 surface levels ×
  3 themes, accent + AI violet, radii, font stacks.
- `buildQss()` turns tokens into the application stylesheet; accent changes
  apply live. High-contrast theme is unit-tested to WCAG AA (≥ 4.5:1).

### i18n (`i18n/i18n.h`)
- Drop-in JSON catalogs (`<bcp47>.json`), auto-discovered; instant language
  switch; RTL detection; `{placeholder}` substitution only (no translated
  fragment concatenation).
- **Naming note:** the lookup method is `translate()`, not `tr()` — Qt's
  `Q_OBJECT` macro injects a `static tr(const char*)` into every QObject
  subclass, which would hijack string-literal calls (verified, Qt 6.8).

### Kilo Gateway client (`ai/kilogateway.h`)
- OpenAI-compatible: `GET /models`, `POST /chat/completions` (SSE streaming).
- 30 s timeout; exponential backoff + jitter on 429/5xx before first byte;
  request queue (one active call); per-hour budget persisted in settings;
  response cache keyed by SHA-1(model, system, user).
- **Gateway reality check** (recorded 2026-10-12, see AI_INTEGRATION.md):
  free models stream `delta.reasoning` and may leave `delta.content` null —
  the client surfaces both, and treats a non-null `finish_reason` or
  `[DONE]` as completion.
- Keys: `KILO_API_KEY` env var, else the local secure store
  (DPAPI on Windows). Never logged, never bundled.

### Transcription boundary (`stt/`)
- whisper.cpp runs **locally** (tiny/base/small on demand, word-level
  timestamps). Only resulting *text* may reach the gateway features.
  Media files never leave the machine.

### Rendering/export (M3/M4)
- Compositor owns frame production (D3D11 preferred, OpenGL fallback) with
  an LRU frame cache (512 MB default). Decode never runs on the UI thread:
  worker pool sized physical-cores − 1.
- Export: MP4 (H.264/H.265/AV1), MOV (ProRes where licensed), WebM (VP9/
  AV1), audio (AAC/Opus/WAV); hardware encoders with automatic software
  fallback; batch queue with progress/ETA/cancel.

## Thread model

| Thread | Work |
|--------|------|
| UI | Layout, input, status bar (1 Hz RSS tick) |
| Worker pool (cores − 1) | Decode, proxy generation, waveforms, export encode |
| Network | Kilo Gateway requests (QNetworkAccessManager) |
| STT worker | whisper.cpp decode with progress callbacks |

Rule: **no blocking decode/export/AI call on the UI thread** — long jobs are
cancelable items in the status bar, never modals.

## File formats

| File | Purpose |
|------|---------|
| `.aep` / `.json` | Project (schema v1) |
| `.autosave` | 60 s autosave next to the project |
| `.clean` | Clean-exit marker (drives crash-recovery prompt) |
| `%LOCALAPPDATA%\ArenaEdit\logs\arena-YYYYMMDD.log` | Daily log + crash context |
| `%LOCALAPPDATA%\ArenaEdit\ai-cache\*.json` | Gateway response cache (clearable) |
| `%LOCALAPPDATA%\ArenaEdit\credentials.key` | DPAPI-protected API key |
