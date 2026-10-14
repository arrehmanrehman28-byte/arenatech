# Building the Windows release (exe + installer)

Arena Edit's release target is **Windows 10/11 64-bit**. Everything is wired
for a one-command release; the only hard requirement is a Windows machine
(or a Windows CI runner). This Linux dev environment cannot compile the
Windows exe (no MSVC / no Qt-for-Windows cross-toolchain — see D-044).

## One command

**Easiest way — double-click `release.bat`** (repo root). It finds
Visual Studio 2022 itself, then runs the same pipeline:

```bat
release.bat                  rem core build (no media)
release.bat C:\ffmpeg        rem full build (import/decode/export/STT)
```

From a **VS 2022 Developer Prompt** (or any pwsh with the tools on PATH):

```powershell
# Core build (no media pipeline):
tools\release.ps1

# Full product (import / decode / export / local STT):
tools\release.ps1 -WithMedia -FfmpegDir C:\ffmpeg
```

`tools/release.ps1` runs: configure → build → `ctest` (all 113) →
headless selftest → stage the portable payload → `iscc tools\package.iss`
→ zip. Artifacts land in `dist\`:

| Artifact | What |
|----------|------|
| `ArenaEdit-0.1.0-portable-win64.zip` | Portable: `ArenaEdit.exe` + **`Run Arena Edit.bat`** (double-click launcher) + Qt/FFmpeg DLLs (**with `platforms\qwindows.dll` structure** — the exe will not start without it) + `assets\` + `i18n\` |
| `ArenaEdit-0.1.0-Setup.exe` | Branded Inno Setup installer (icon, credit line, x64, Win10+, per-user install) |
| `dist\portable\` | The staged folder, ready to run in place |

**End-user flow:** send them the portable zip → unzip anywhere → double-click
`Run Arena Edit.bat`. No installer, no admin rights, no runtime needed.

The exe carries the app icon and version info via `app/arenaedit.rc`;
the installer wizard uses the same icon.

## Prerequisites

| Tool | Install |
|------|---------|
| Visual Studio 2022 (Desktop C++), CMake ≥ 3.21, Ninja | VS installer / winget |
| vcpkg (pins Qt 6 LGPL) | `git clone https://github.com/microsoft/vcpkg %VCPKG_ROOT%` and run `bootstrap-vcpkg.bat`; set `VCPKG_ROOT` |
| Inno Setup 6 | `choco install innosetup -y` |
| FFmpeg dev prefix (full build only) | any `include/lib/bin` layout: a vcpkg port (`vcpkg install ffmpeg:x64-windows` → use that triplet dir) or an [ffmpeg.org](https://ffmpeg.org/download.html) build |

## Code signing (optional)

```powershell
$env:CERT_PATH = "C:\certs\release.pfx"
$env:CERT_PASSWORD = "..."
tools\release.ps1 -WithMedia -FfmpegDir C:\ffmpeg
```

`tools/package.iss` uses `SignWithSigntoolEnv=yes`, so the installer is
signed automatically when the env vars are present and unsigned (with a
normal SmartScreen prompt) when they aren't.

## CI alternative

Pushing to `main` runs `.github/workflows/ci.yml`: the Linux job builds the
full pipeline (FFmpeg + whisper), gates on zero warnings, runs all tests +
selftest + benchmark, and uploads a Linux portable artifact. The Windows
job builds the core configuration (MSVC + vcpkg Qt), runs the tests,
selftest, and `tools\release.ps1 -SkipTests`, uploading the installer,
portable zip, and payload. The full-media Windows artifact needs the FFmpeg
prefix (a CI secret / step that drops `C:\ffmpeg` in place) — that is the
only remaining pipeline gap (D-044).

## What the exe includes (full build)

- All editing (M1–M5), AI features (M6, off by default — works fully
  offline without an API key), local STT (M7, models download on demand),
  onboarding tour + sample project (M8).
- `assets\sample_project` opens from File ▸ Open Sample Project.
- Whisper models are fetched on first use into `%LOCALAPPDATA%`
  (never bundled).
