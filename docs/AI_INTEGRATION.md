# Arena Edit — Kilo Gateway Integration

*All responses below were **recorded live** from the real gateway on
2026-10-12 (anonymous, no API key) and are part of the test evidence.
Nothing in this document is invented.*

## Endpoint

| | |
|---|---|
| Base URL | `https://api.kilo.ai/api/gateway` |
| Models | `GET {base}/models` |
| Chat | `POST {base}/chat/completions` (OpenAI-compatible, `stream: true` supported) |
| Auth | `Authorization: Bearer <KILO_API_KEY>` when a key is set; anonymous use of `:free` models allowed at a low rate limit (~200 requests/hour per IP) |

Key resolution in Arena Edit: `KILO_API_KEY` environment variable first,
then the local secure store (DPAPI on Windows). The key is never logged and
never bundled (see `ai/keystore.cpp`, PRIVACY.md).

## 1. Model list — recorded response (truncated)

`GET https://api.kilo.ai/api/gateway/models` → **HTTP 200** in 0.28 s:

```json
{
  "data": [
    {
      "id": "kilo-auto/efficient",
      "name": "Auto Efficient",
      "created": 0,
      "description": "Routes each request to the cheapest model that gets the job done, based on continuously benchmarked accuracy and cost.",
      "architecture": {
        "input_modalities": ["text", "image"],
        "output_modalities": ["text"],
        "tokenizer": "Other"
      },
      "top_provider": {
        "is_moderated": false,
        "context_length": 1000000,
        "max_completion_tokens": 65536
      },
      "pricing": { "prompt": "-1", "completion": "-1", "request": "0", "image": "0", "web_search": "0", "internal_reasoning": "0" },
      "context_length": 1000000,
      "supported_parameters": ["max_tokens", "temperature", "tools", "reasoning", "include_reasoning"],
      "opencode": {},
      "preferredIndex": 0,
      "isFree": false,
      "mayTrainOnYourPrompts": false,
      "autoRouting": {
        "models": ["~anthropic/claude-haiku-latest", "~moonshotai/kimi-latest", "~openai/gpt-luna-latest", "z-ai/glm-latest", "deepseek/deepseek-v4.1-flash", "meta/muse-spark-1.3-contributor", "qwen/qwen3.8-flash", "qwen/qwen3.8-max-0902", "stepfun/step-5-preview", "xiaomi/mimo-v2.6-pro"]
      }
    },
    {
      "id": "stepfun/step-5-preview-free",
      "canonical_slug": "stepfun/step-5-preview-free",
      "name": "StepFun: Step 5 Preview (free)",
      "created": 1756238927,
      "description": "Step 5 Preview is StepFun's flagship model for agentic work, … 1M-token context window and up to 64K output tokens. …"
    }
    /* … */
  ]
}
```

Recorded facts:

- **390 models** in the list; **16 free** (by `isFree` flag or `:free` suffix).
- Free sample IDs observed: `nvidia/nemotron-3-ultra-550b-a55b:free`,
  `dots-studio/dots-3-note-preview:free`, `poolside/laguna-s-2.1:free`,
  `liquid/lfm-2.5-2.6b:free`, `nvidia/nemotron-3.5-lightning:free`,
  `thinkingmachines/inkling-small:free`, `poolside/laguna-xs-2.1:free`,
  `cohere/north-mini-code:free`, …
- Model objects carry `supported_parameters` (e.g. `max_tokens`,
  `temperature`, `tools`, `reasoning`) — the client only sends `model`,
  `messages`, `stream`, `max_tokens`, all universally supported.
- **Free models churn.** The client never hardcodes model IDs: default is
  `auto` → first `:free` model from the live list; users pin models from
  Settings ▸ AI (decision D-007). If a pinned model is removed, the error is
  surfaced and the user reselects — no silent swap.

## 2. Chat completion (non-streaming) — recorded response

`POST /chat/completions` with
`{"model":"liquid/lfm-2.5-2.6b:free","messages":[{"role":"user","content":"Reply with exactly: ARENA-EDIT-OK"}],"max_tokens":20}`
→ **HTTP 200** in 3.7 s:

```json
{
  "id": "gen-1791819234-822d5HaOBeFcVivVevqB",
  "object": "chat.completion",
  "created": 1791819234,
  "model": "liquid/lfm-2.5-2.6b:free",
  "provider": "Liquid",
  "system_fingerprint": "vllm-0.29.1rc1.dev9+g2671fedfc-dafbf1d2",
  "service_tier": null,
  "choices": [
    {
      "index": 0,
      "logprobs": null,
      "finish_reason": "length",
      "native_finish_reason": "length",
      "message": {
        "role": "assistant",
        "content": null,
        "refusal": null,
        "reasoning": "The user wants a specific response. The instruction is \"Reply with exactly: ARENA-EDIT",
        "reasoning_details": [
          { "type": "reasoning.text", "text": "The user wants a specific response. The instruction is \"Reply with exactly: ARENA-EDIT", "format": "unknown", "index": 0 }
        ]
      }
    }
  ],
  "usage": {
    "prompt_tokens": 21, "completion_tokens": 20, "total_tokens": 41, "cost": 0, "is_byok": false,
    "prompt_tokens_details": { "cached_tokens": 0, "cache_write_tokens": 0, "audio_tokens": 0, "video_tokens": 0 },
    "cost_details": { "upstream_inference_cost": 0, "upstream_inference_prompt_cost": 0, "upstream_inference_completions_cost": 0 },
    "completion_tokens_details": { "reasoning_tokens": 20, "image_tokens": 0, "audio_tokens": 0 }
  }
}
```

**Findings that shaped the client** (decision D-008):

1. `:free` reasoning models put their output in `message.reasoning` and leave
   `message.content: null` — clients that only read `content` see an empty
   answer. Arena Edit's client reads **both** `content` and `reasoning`.
2. `finish_reason: "length"` ends the stream when `max_tokens` is exhausted;
   completion detection must key off `finish_reason` (or `[DONE]`), not off
   non-null `content`.
3. `usage.cost` is `0` for free models — the budget meter counts requests,
   not cost.

## 3. Streaming (SSE) — recorded chunk sample

`POST /chat/completions` with `"stream": true`,
`{"model":"liquid/lfm-2.5-2.6b:free","messages":[{"role":"user","content":"Count 1 to 5, space separated."}],"max_tokens":30}`:

```
data: {"id":"gen-1791819236-dT4WOEQvAGJ5gbH2idgc","object":"chat.completion.chunk","created":1791819236,"model":"liquid/lfm-2.5-2.6b:free","provider":"Liquid","choices":[{"index":0,"delta":{"content":"","role":"assistant","reasoning":"The","reasoning_details":[{"type":"reasoning.text","text":"The","format":"unknown","index":0}]},"finish_reason":null,"native_finish_reason":null}]}

data: {"id":"gen-1791819236-dT4WOEQvAGJ5gbH2idgc","object":"chat.completion.chunk","created":1791819236,"model":"liquid/lfm-2.5-2.6b:free","provider":"Liquid","choices":[{"index":0,"delta":{"content":"","role":"assistant","reasoning":" user wants me to","reasoning_details":[{"type":"reasoning.text","text":" user wants me to","format":"unknown","index":0}]},"finish_reason":null,"native_finish_reason":null}]}

data: {"id":"gen-1791819236-dT4WOEQvAGJ5gbH2idgc","object":"chat.completion.chunk","created":1791819236,"model":"liquid/lfm-2.5-2.6b:free","provider":"Liquid","choices":[{"index":0,"delta":{"content":"","role":"assistant","reasoning":" count from","reasoning_details":[{"type":"reasoning.text","text":" count from","format":"unknown","index":0}]},"finish_reason":null,"native_finish_reason":null}]}

… (one `data:` line per token; blank line between chunks; final `finish_reason` non-null or `[DONE]`)
```

Parse rules implemented in `ai/kilogateway.cpp`:

- Buffer `readyRead` data, split on `\n`, handle lines starting `data:`.
- `data: [DONE]` → done. `choices[0].finish_reason` non-null → done.
- Append `delta.content`; if empty, append `delta.reasoning` (finding #1).
- Retry with **exponential backoff + jitter** (1 s → 2 s → 4 s, up to 3
  times) on HTTP 429 or 5xx *before the first byte*; after partial content,
  keep what arrived and report "stream interrupted" rather than dropping it.
- 30 s transfer timeout per request (`QNetworkRequest::setTransferTimeout`).
- One active request at a time; further calls queue (`m_queue`);
  `cancel()` aborts the active request and rejects queued ones.

## 4. Rate limiting & budget

- Anonymous: ~200 requests/hour per IP (gateway documentation; enforced as
  HTTP 429 once exceeded → backoff applies).
- `AIBudget` (ai/kilogateway.h) keeps a rolling hour window in settings
  (`ai/budgetWindowStart`, `ai/budgetUsed`); the status bar shows
  "AI: {used} of {limit} requests used this hour"; the limit is user-tunable
  (Settings ▸ AI) so you can stay under the anonymous ceiling with headroom.

## 5. Response cache

- Key: `SHA-1(model + "|" + systemPrompt + "|" + userPrompt)`; stored as
  `%LOCALAPPDATA%\ArenaEdit\ai-cache\<hash>.json` with the model, content,
  and timestamp.
- A cache hit streams the stored content back through the same `onDelta`
  path (UI can't tell the difference) and spends **no** budget.
- **Settings ▸ Privacy ▸ Clear AI cache and history** removes the whole
  directory.

## 6. Prompt templates (all in `ai/prompts.h`)

All prompts demand machine-checkable output; results are validated
(line counts / JSON schema) before UI use and applied only after explicit
Accept.

| Feature | Template contract |
|---------|-------------------|
| `translateSubtitles()` | Input: `{entries}` = JSON array of `{index,start,end,text}` (≈ 40 per batch), `{targetLang}` BCP-47. Output: JSON array of exactly N strings, same order; preserve line breaks and speaker tags. Client validates the returned count == N; a failed batch is retried **only** for the failed entries. |
| `generateChapters()` | Input: `{transcript}` (`HH:MM:SSFF text` lines). Output: JSON array of 3–10 `{start:"HH:MM:SS", title}` (title ≤ 6 words, user's language). |
| `smartTitleAndDescription()` | Input: `{context}` metadata. Output: `{"title"≤60 chars,"description"≤300 chars}` in the user's language. |
| `suggestCuts()` | Input: `{transcript}` + `{silences}` silence map. Output: JSON array of `{start,end,reason:"silence\|filler",confidence:0..1}`. Each suggestion is an Accept/Reject card — **never auto-applied**. |
| `naturalLanguageToActions()` | Input: `{request}`. Output: JSON array of actions from the strict schema below. |

### Natural-language command schema (validated by `ai/actionvalidator.cpp`)

```jsonc
[
  { "op": "add_transition",
    "target": { "type": "clip", "index": -1 },      // -1 = last clip
    "transition": "cross_dissolve | wipe | push | fade_to_black",
    "duration_ms": 500 },                            // 10..10000, optional
  { "op": "set_clip_speed",
    "target": { "type": "clip", "index": 0 },
    "speed": 1.5 },                                  // 0.1..16
  { "op": "add_marker",
    "time": 120,                                     // >= 0
    "label": "Drop" },                               // <= 120 chars
  { "op": "export",
    "preset": "youtube_1080p | instagram_reels | web_720p | prores_master | audio_podcast | webm_web",
    "subtitles": "burn | sidecar | none" }           // optional
]
```

Validation rules: unknown `op` → reject with named error; wrong
`target.type`, out-of-range `speed`/`duration_ms`, unknown `preset`,
oversized label → reject. The extractor (`extractJsonArray`) pulls the first
parseable top-level JSON array out of raw model text (code fences / prose
tolerated, brackets inside strings handled). Unit tests in
`tests/test_actions.cpp` cover all reject paths.

## 7. Security rules (enforced in code)

- Key: env var or DPAPI store; **never logged** (the request builder only
  puts it in the `Authorization` header).
- Text only: prompt builders receive transcripts/subtitle text/metadata —
  no media paths' contents, no project bytes (PRIVACY.md table).
- Strict output validation + user confirmation before any action executes;
  every executed action is an undoable command.
- AI disabled ⇒ `KiloClient::chat` refuses locally ("AI is disabled") and
  the network is never touched.
