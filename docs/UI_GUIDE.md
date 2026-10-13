# Arena Edit — UI Guide

## Design tokens (single source: `assets/theme/theme.json`)

| Token | Dark | Light | High-contrast |
|-------|------|-------|---------------|
| surface0 (app background) | `#14161A` | `#F4F5F7` | `#000000` |
| surface1 (panels, bars) | `#1B1E24` | `#FFFFFF` | `#0D0D0D` |
| surface2 (controls) | `#232730` | `#EDEFF2` | `#1A1A1A` |
| surface3 (hover) | `#2C313C` | `#E2E5EA` | `#262626` |
| surface4 (pressed) | `#363C49` | `#D5D9E0` | `#333333` |
| text | `#E8EAF0` | `#1E2229` | `#FFFFFF` |
| textMuted | `#9AA3B2` | `#5C6470` | `#D0D0D0` |
| border | `#3A4150` | `#C9CED6` | `#FFFFFF` |
| success / warning / error | `#3DBE7B` / `#E5B454` / `#E5534B` | `#1F8A54` / `#9A6B00` / `#C23B34` | `#4ADE80` / `#FACC15` / `#F87171` |

- **Accent**: `#4C8DFF` (user-changeable in Settings ▸ Appearance).
- **AI violet**: `#9B6DFF` — every AI surface uses it so AI actions are
  always recognizable (never the accent color).
- **Radius**: 6 px controls, 10 px panels/cards. **Spacing**: 4/8/12/16/24/32.
- **Motion**: 120–180 ms easing; respects Windows "Reduce motion"
  (Settings ▸ Appearance also offers an override).
- **Shadows** only on floating elements (menus, dialogs, tooltips, palette).
- **Typography**: Segoe UI Variable → Noto Sans → (Arabic/Urdu) Noto Sans
  Arabic → CJK Noto Sans CJK → Devanagari Noto Sans Devanagari. Timecodes:
  Cascadia Mono. Font sizes: small/default/large/extra-large scale the whole
  UI (11/13/15/17 pt base).
- **High contrast** meets WCAG AA: the surface0/text pair is unit-tested at
  ≥ 4.5:1 (`tests/test_themer.cpp`).
- Color is never the only signal: status colors pair with icons or text
  (e.g. "Missing" badge = red text + label, not just color).

## Main workspace (M0 layout)

```
+---------------------------------------------------------------------------+
| Menu / Command Bar (Ctrl+K)                          AI quota   [User]    |
+---------------------------------------------------------------------------+
| Media Bin      |            Program Monitor           |   Inspector       |
| (thumbs, list, |   [ preview + safe-area/grid/thirds  |  (properties;     |
|  relink badge) |    transport, playback res ]         |   per-selection)  |
+---------------------------------------------------------------------------+
| Timeline toolbar: snap, magnet, markers, zoom, track height               |
| Ruler + lanes (V1/A1…) — always LTR, even in RTL locales                 |
+---------------------------------------------------------------------------+
| Status bar: timecode | fps | render cache | RAM | AI: N of M this hour   |
+---------------------------------------------------------------------------+
```

- **Dockable**: every panel can be dragged, floated, tabified (Inspector +
  AI Panel are tabbed by default), hidden, and (workspace presets in M8)
  saved as Edit/Color/Audio/Subtitles/Export.
- **Program Monitor**: letterboxed preview with safe-area (90/80 %), grid,
  and rule-of-thirds overlays; playback resolution Full/1/2/1/4; virtual
  JKL transport in M0 (real decode in M1).
- **Timeline**: toolbar (snap on by default; magnetic/markers light up with
  a project), Ctrl+wheel zoom, ruler with adaptive tick spacing and SMPTE
  labels.
- **Status bar** AI meter: "AI: 37 of 200 requests used this hour" (live
  from the per-hour budget).
- **Title bar** shows "Arena Edit — {project}" and a quiet "Saved {time}
  ago" autosave indicator.

## Key screens

| Screen | Contents |
|--------|----------|
| Welcome (first run) | Logo, language picker, New Project / Open, recent projects, "What's new" card, credit link |
| New Project | Name, resolution presets, fps, aspect (16:9/9:16/1:1/4:5), sample rate, 540p proxy toggle, **live preview line** |
| Command palette (Ctrl+K) | Fuzzy search over every command with shortcuts; **Tab or `/ai` prefix** switches to natural-language mode |
| Media Bin | Rows with name / type+resolution / status, **live probe** (Probing… → "Video 640×360 · 30 fps"), import, **missing-media badge + Relink** (re-points all clips, re-probes) |
| Export | Preset gallery cards with estimated size/time + batch queue table (M4) |
| Subtitle Manager | Table of tracks by BCP-47 language, add/import (M5), translate + timing checks (M5/M6), reading-speed warnings (> 42 chars / > 17 cps, red underline) |
| AI Panel | Violet status card (off/on), task cards (translate/chapters/titles/cuts), **Ask AI** with live streaming, result cards with Accept/Edit/Reject |
| Settings | Grouped sidebar (General, Appearance, Language, Editing, Playback, Export, Hardware, AI, Privacy, Keyboard) + search; live apply where possible; "Restart required" only when truly needed |
| About | "Arena Edit", version + build date, license notices, **Made by arenatechofficial.pages.dev** |

## Media workflow (M1)

1. **Import** — Media Bin ▸ Import (or double-click a file): files appear
   immediately and are probed in the background (UI never blocks).
2. **Add to timeline** — double-click a row or right-click ▸ "Add to
   timeline". Without a project, a default 1920×1080 @ 30 fps project is
   created automatically; the clip is appended to V1 (undoable).
3. **Watch** — the Program Monitor shows the selected clip's frames with a
   waveform strip and a thumbnail filmstrip underneath.
4. **Play / pause** — Space or the play button. Playback is A/V-synced:
   audio is the master clock when the file has audio; otherwise a wall
   clock drives video (and when no audio device exists at all).
5. **Scrub** — click/drag on the timeline ruler; the playhead follows, the
   monitor jumps (clips switch as the playhead crosses them), timecode
   stays SMPTE HH:MM:SS:FF in every language.
6. **Proxy** — when the source is larger than the project's proxy height
   (default 540p) and proxies are enabled, a small mp4 proxy is generated
   on a worker thread (progress in the status bar); playback and scrubbing
   then use the proxy. Failure falls back to full resolution.
7. **Missing media** — red row status; right-click ▸ Relink picks a
   replacement, all clips re-point (undoable), and the file is re-probed.

Known M1 behavior: multi-clip playback switches sources at clip
boundaries (jump cuts); per-clip in/out trimming, ripple edits, and snap
arriving in M2.

## Timeline editing (M2)

- **Select** — click a clip; click empty lane space to deselect. The
  Inspector shows the selection's In/Out/Start/Duration (SMPTE) and a
  missing-media warning.
- **Trim** — grab a clip edge (split cursor) and drag: the left edge
  moves the in point + position, the right edge moves the out point.
  Geometry is clamped to the timeline start and the source's length;
  release commits one undoable command.
- **Roll** — grab the boundary between two adjacent clips and drag:
  both clips trade frames (duration sum preserved).
- **Ripple trim** — **Q** trims the end of the clip at the playhead to
  the playhead and shifts later clips left; **W** trims the start and
  shifts later clips right.
- **Snap** — while trimming, the moving edge snaps to clip edges and
  the playhead (4 px) with a violet snap line; toggle in the timeline
  toolbar.
- **Markers** — **M** (or double-click the ruler) adds a marker;
  right-click a marker to remove it.
- **Delete** — Delete key removes the selected clip (undoable).

## Effects & rendering (M3)

- **Effects editor (Inspector)** — with a clip selected, the Inspector
  shows an Effects section: Position X/Y (% of canvas), Scale (%),
  Rotation (°), Opacity (%), Exposure (stops), Contrast, Saturation,
  Color temperature, Blur (px), Transition (None / Cross dissolve /
  Fade to black / Wipe / Push) with duration in seconds. **Apply
  effects** stores the values on the clip as one undoable command
  (Undo button label: "Set effects"). Leaving everything at defaults
  produces no effect data, so the fast path stays active.
- **Live preview** — the program monitor renders through the
  deterministic compositor as soon as any clip carries effects:
  transform → color → blur → opacity, with clip-tail transitions
  blended against the next clip (or black for fade-to-black at the
  end of the track). Decoded frames are kept in an LRU render cache
  (512 MB default, `render/cacheMB` setting); the status bar shows the
  live cache occupancy.
- **Playback resolution** — the monitor's Full / 1/2 / 1/4 selector
  scales the displayed preview only (the compositor always works at
  project resolution); useful for slow machines / long scrubs.
- **Dropped frames** — the status bar counts frames the decoder could
  not show on time during playback (reset on each play).

## Export (M4)

- **Open** — File → Export… (Ctrl+Shift+E). Requires a project; the
  dialog is disabled (status message) in builds without FFmpeg.
- **Presets** — checklist of presets (mp4-h264-high, mp4-h264-balanced,
  mp4-h264-web-720, mp4-hevc, mp4-audio, webm-vp9). Each row shows a
  live size/time estimate, and the box footer shows the total for the
  checked set. Estimates are heuristics (resolution × fps × duration ×
  quality), not a promise.
- **Options** — Scale (25–200 % of project size), Hardware encode
  (NVENC/QSV/AMF candidates with automatic software fallback),
  Custom quality (slider → CRF 1–51; off = the preset's quality), and
  Audio bitrate (preset / 96–320 kbps).
- **Output** — folder picker (defaults to the project's folder).
  Files are named `<project>-<preset>.<ext>`.
- **Queue** — **Start export** queues every checked preset and runs
  them sequentially on a background thread (the UI stays fully
  usable; the timeline stays editable because each job works on a
  snapshot of the project). The queue table shows per-job status
  (Queued → Encoding… → Done/Failed/Cancelled), a progress bar, and
  the final file size. **Cancel** stops the running job at its next
  frame boundary (partial files are removed). Closing the dialog
  while a job runs asks for confirmation and cancels.
- **Sources** — export always decodes the original files at full
  quality (never the 540 p playback proxies).

## Interaction rules

- **Keyboard first**: J/K/L shuttle, I/O in/out (M2), Q/W ripple trim,
  M marker, Delete remove clip, Space play, +/- zoom, Ctrl+Z/Ctrl+Y
  undo/redo, Ctrl+K palette, Ctrl+, settings. Remappable in Settings ▸
  Keyboard (M8); ships with "Classic" and "Simple" keymaps.
- **Undo clarity**: buttons read "Undo: Rename project" (the command's
  action name), not just "Undo".
- **Snapping feedback**: visible snap line + short highlight flash (M2).
- **Context menus** ≤ 8 items, submenus where needed.
- **Empty states**: every empty panel says what it will show + one clear
  action ("Drop media here or click Import").
- **Non-blocking feedback**: toasts for background work; long jobs are
  cancelable status-bar items. Never a modal for backgroundable work.
- **Dialogs**: primary button right (LTR) / left (RTL); plain-language
  errors with "Show details" expander + "Copy diagnostics".

## Multilingual / RTL

- All strings via the translation layer with `{placeholders}`; never
  concatenated translated fragments; timecodes always SMPTE
  (`HH:MM:SS:FF`); numbers/dates/sizes via `QLocale`.
- Arabic/Urdu mirror the whole layout; **the media timeline stays LTR**
  (time flows left → right); directional icons (undo/redo arrows) mirror,
  play/time/waveform icons do not (M8 pass).
- Layouts tolerate +30–40 % text width vs English.
- Shipped: English, Arabic, Urdu (drop-in; see `i18n/README.md` for adding
  Spanish/French/Hindi/Chinese/Japanese in M5+).

## Accessibility & scaling

- Full keyboard navigation, visible focus ring on every control (M8 audit).
- UI Automation names/roles on every control; Narrator-tested in M8.
- DPI-aware 100–300 %, per-monitor; font-size setting scales the whole UI.

## Screenshots

Captured with the offscreen platform on the CI harness (M0):

- `docs/screenshots/main-dark.png` — main workspace, dark theme (added in M8 polish; M0 verified via selftest + this guide's layout block)
- `docs/screenshots/main-light.png`, `main-rtl-arabic.png` — M8.

> M0 note: automated verification of the live UI is the `--selftest`
> headless run (14 checks: locales, RTL, themes, project round-trip,
> undo/redo, validator, palette, window construction).
