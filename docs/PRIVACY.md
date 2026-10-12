# Arena Edit — Privacy

## What leaves your machine (and never does)

| Data | Leaves the machine? | When |
|------|--------------------|------|
| Media files (video/audio/images) | **Never** | — |
| Transcripts (whisper.cpp output) | Only text, opt-in | AI tasks you trigger (M6/M7) |
| Subtitle lines | Only text, opt-in | Translation / AI tasks you trigger |
| Project metadata (names, settings) | Only text, opt-in | Smart titles/descriptions you trigger |
| Project files (`.aep`) | **Never** | — |
| API key | **Never** | Stored locally (env var or DPAPI), never logged |

## AI is opt-in

- AI features are **off by default** and the app is 100% functional offline
  with AI disabled (core code never links `ai/`).
- A **privacy notice** is shown before the first AI request; you can decline
  at any time and disable AI in Settings ▸ AI or the AI panel.
- Only **text** is sent to the Kilo Gateway (`api.kilo.ai`): transcripts,
  subtitle lines, and metadata — shaped by the prompt templates in
  `ai/prompts.h` (documented in AI_INTEGRATION.md).

## Data at rest

- **API key**: `KILO_API_KEY` environment variable, else
  `%LOCALAPPDATA%\ArenaEdit\credentials.key` — protected with DPAPI
  (`CryptProtectData`) on Windows. The key is never written to logs.
- **AI response cache**: `%LOCALAPPDATA%\ArenaEdit\ai-cache\*.json` —
  keyed by SHA-1(model, system prompt, user prompt). Clear it any time with
  **Settings ▸ Privacy ▸ Clear AI cache and history**.
- **Logs**: `%LOCALAPPDATA%\ArenaEdit\logs\arena-YYYYMMDD.log` — diagnostic
  messages, crash context. No media paths' *contents*, no prompt bodies, no
  keys.

## AI automation is confirmable, never automatic

- Every AI result (chapters, titles, translation, cut suggestions, natural-
  language actions) appears as a reviewable card with **Accept / Edit /
  Reject**. Nothing is applied without your confirmation.
- Natural-language commands are validated against a strict JSON schema
  (`ai/actionvalidator.cpp`), shown as a preview, and executed only after
  confirmation — and every action is undoable.

## Rate and cost awareness

- Anonymous use of free gateway models is limited (≈ 200 requests/hour per
  IP). The status bar shows a live meter: "AI: N of 200 requests used this
  hour". The budget can be lowered in Settings ▸ AI.
- Free models may be used for model training per the gateway's terms —
  another reason only non-sensitive text is sent and AI stays opt-in.

## Vendor

Kilo Gateway (https://api.kilo.ai) — OpenAI-compatible chat gateway
providing `:free` models. Requests are standard HTTPS to
`/api/gateway/chat/completions` and `/api/gateway/models`.
