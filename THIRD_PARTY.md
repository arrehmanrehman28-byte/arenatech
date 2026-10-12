# Third-Party Dependencies & Licenses

| Dependency | Version (M0) | Used for | License |
|------------|--------------|----------|---------|
| Qt 6 (Core, Gui, Widgets, Network, Concurrent) | 6.8.2 (Debian) / pinned ≥ 6.5 via vcpkg on Windows | UI, networking, i18n plumbing | **LGPL 3.0** (dynamic linking; GPL option not used) |
| FFmpeg | not linked in M0 (M1, LGPL build, dynamic DLLs) | demux/decode/encode, HW accel | **LGPL 2.1** (with GPL components excluded) |
| whisper.cpp | not linked in M0 (M7, on-demand models) | local speech-to-text | **MIT** |
| GoogleTest | 1.16 | unit tests (test-only, not shipped) | **BSD-3-Clause** |
| Inno Setup | 6 (packaging tool, not shipped) | installer | **Inno Setup License** (freeware) |
| Ninja, CMake | build tools | build | MIT / BSD-3-Clause |
| Lucide-style icons | `assets/icons/` (21 SVGs, hand-authored in Lucide's 24 px/2 px stroke style) | UI icons | **CC BY 4.0** (Lucide) — attribution kept in this file |
| Segoe UI Variable, Cascadia Mono | Windows system fonts (no copy bundled) | typography | Microsoft fonts, licensed with Windows |
| Noto Sans / Noto Sans Arabic / Noto Sans CJK / Noto Sans Devanagari | system-installed (no copy bundled in M0) | script fallbacks | **OFL 1.1** |
| Kilo Gateway | service (api.kilo.ai), OpenAI-compatible | AI automation (opt-in, text-only) | service terms of https://api.kilo.ai; no code shipped |

## License compatibility notes

- **LGPL (Qt, FFmpeg)**: dynamic linking only; no static linking of LGPL
  code without the relink mechanism. M9 will document the exact DLL set and
  the LGPL notice file shipped in the installer (`README` / license folder).
- **MIT (whisper.cpp, GTest, CMake, Ninja)**: permissive; notices retained
  upstream, summarized here.
- **CC BY 4.0 icons**: Lucide attribution satisfied by this file (and the
  About dialog credit block in M9).
- **OFL fonts**: no redistribution in M0 (system fonts); if M9 bundles
  subsets, the OFL and font notices ship alongside.
- **Code**: the Arena Edit source (this repository) is provided by
  arenatechofficial.pages.dev; license terms to be finalized at M9
  (working rule: keep it compatible with the LGPL linkage above — e.g. a
  permissive or LGPL-compatible source license).
