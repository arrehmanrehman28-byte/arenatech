# Adding a language

1. Copy `en.json` to `<bcp47>.json` (e.g. `fr.json`, `hi.json`, `ja.json`, `zh-Hans.json`).
2. Set `meta.bcp47`, `meta.nativeName`, and `meta.dir` (`ltr` or `rtl`).
3. Translate every string under `strings`. Keep:
   - **Placeholders** exactly as-is (`{count}`, `{action}`, `{width}` …).
   - The **product name** `Arena Edit` and the **credit line** `Made by
     arenatechofficial.pages.dev` untranslated in every locale (branding rule).
   - Timecodes SMPTE-style (`HH:MM:SS:FF`) — they are formatted in code, not
     translated.
4. Save as UTF-8. Done — the language appears in View ▸ Language and
   Settings ▸ Language automatically (the loader scans this directory for
   `*.json`). No code change required.

Rules of thumb:
- Never concatenate translated fragments; use one key per sentence with
  placeholders.
- Assume the UI may be 30–40 % wider than English (long words in German,
  French, Urdu); layouts use expanding widgets and wrap where possible.
- RTL locales: the whole layout mirrors; the **media timeline stays LTR**
  (time always flows left to right), but text tracks mirror.
- Number/date/file-size formatting goes through `QLocale`, not the string
  files.
