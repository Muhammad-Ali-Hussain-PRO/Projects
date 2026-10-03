# Multimodal evidence pipeline

`backend/studio/multimodal.py` exposes `run(payload: dict) -> dict`. It accepts
supplied image, audio, text, and code evidence and returns JSON-serializable
inspection results. Local mode requires only the Python standard library.

## Request contract

```json
{
  "mode": "local",
  "prompt": "Compare these inputs and state what the evidence supports.",
  "inputs": [
    {"type": "text", "name": "note.txt", "text": "The recording is one second long."},
    {"type": "code", "name": "example.py", "text": "def double(x):\n    return x * 2\n"}
  ]
}
```

`mode` defaults to `local`. `live` requests the configured provider; `model` is
an alias for `live`. Optional boolean `use_model` overrides the mode choice.
`prompt` defaults to an inspection request. `attachments` is an alias for
`inputs`.

| Input field | Meaning |
| --- | --- |
| `type` | `image`, `audio`, `text`, or `code`; `kind` is an alias |
| `name` | Optional display name; `filename` is an alias; no path is opened |
| `mime_type` | Optional declared MIME; `mime` is an alias; parameters such as `;codecs=opus` are removed |
| `text` | Exact UTF-8 text or source code; `content` is an alias |
| `data_base64` | Binary bytes encoded with standard base64; `base64` and `data` are aliases |
| `language` | Optional code language; common source filename extensions are recognized |

For binary inputs, `data_base64` can also contain a base64 data URL such as
`data:image/png;base64,...`. Declared MIME and data-URL MIME must agree with the
recognized binary signature. Arbitrary URLs, filesystem paths, video files,
archives, and non-base64 data URLs are not loaded. If the type is omitted, the
pipeline can infer image/audio from a supported MIME, or text/code from a text
field and filename extension.

| Evidence | Supported formats and local inspection |
| --- | --- |
| Image | PNG, JPEG, GIF, WebP, BMP; dimensions and format from headers |
| Audio | PCM WAV: channel count, sample rate, frame count, sample width, duration, and declared frame completeness |
| Audio | MP3, Ogg, FLAC: container/header signature only; duration unavailable |
| Text | UTF-8 text; character, word, line counts; JSON syntax check for `application/json` |
| Code | Text statistics; Python AST syntax, definition counts, and import counts; other languages receive text statistics |

These binary checks do not decode pixels or sound and do not validate every
byte of an image or compressed audio file. Known Ogg video is rejected. The
pipeline never executes code or locally transcribes audio.

## Limits

| Limit | Value |
| --- | --- |
| Inputs per request | 1–8 |
| Binary bytes per image/audio input | 8 MiB |
| UTF-8 bytes per text/code input | 256 KiB |
| Accepted bytes across the request | 16 MiB |
| UTF-8 prompt bytes | 8 KiB |
| Published structured model result | 128 KiB |
| Python AST nodes inspected for structural counts | 20,000 |

Binary limits are checked before and after base64 decoding. Empty evidence,
malformed encodings, unsupported signatures, conflicting MIME declarations,
and invalid request types produce explicit errors. Valid evidence is retained
when another input fails; its ID preserves the original position, such as
`input-2` after the first input was rejected.

## Result contract

The response contains `status`, `mode`, `summary`, `evidence_manifest`,
`findings`, `limitations`, `errors`, and `model`.

Each accepted evidence manifest entry includes `id`, `name`, `type`,
`mime_type`, `bytes`, `sha256`, and `metadata`. SHA-256 covers the exact decoded
binary bytes or exact UTF-8 text bytes. The manifest omits submitted source
text and base64 data. Local findings carry an `input_id`, an observation,
`confidence: "high"` for the verified metadata observation, and
`source: "offline_inspection"`. That confidence describes metadata certainty,
not semantic understanding of the image/audio or runtime correctness of code.

| Status | Meaning |
| --- | --- |
| `ok` | All supplied evidence was accepted |
| `partial` | At least one input was accepted and at least one rejected |
| `error` | Request invalid or no evidence accepted |
| `blocked_provider` | Live interpretation requested but provider unavailable/unconfigured; local evidence remains available |
| `provider_error` | Configured provider call failed; local evidence remains available |
| `invalid_model_response` | Configured provider returned invalid or ungrounded structured output; model result omitted |

Local results clearly state that image content has not been examined and audio
has not been listened to or transcribed. A request such as “What objects are
visible?” cannot be answered by local header inspection. Static Python syntax
validation does not establish program behavior or security.
Structural counting stops at the AST node bound and omits counts rather than
publishing incomplete totals. Syntax validation can still be reported when
parsing succeeded.

## Configured model interpretation

Live mode imports the shared `.provider`, checks `is_configured()`, then calls:

```python
generate_multimodal(prompt: str, attachments: list, schema: dict) -> dict
```

Attachments are normalized as `{id, type, name, mime_type, sha256, text}` for
text/code and `{id, type, name, mime_type, sha256, data_base64}` for binary
evidence. Only accepted inputs are sent. The prompt includes the evidence
manifest and requests observations grounded in attachment IDs, with explicit
limitations and no code execution. Submitted text/code is described as
untrusted evidence. The actual provider must support the submitted modalities;
recognizing an audio container locally does not establish that a configured
model can interpret that audio format.

The provider must return this structured shape:

```json
{
  "summary": "A grounded interpretation from the configured model.",
  "findings": [
    {"input_id": "input-1", "observation": "An observation tied to this input.", "confidence": "medium"}
  ],
  "limitations": ["Uncertainty or unsupported evidence."]
}
```

Findings must reference accepted IDs, confidence must be `low`, `medium`, or
`high`, and the result must fit the output bound. Published model findings gain
`source: "configured_model"`. Successful interpretation sets `mode: "live"`
and `model: {status: "completed", result: ...}`. The original local findings
remain separate from `model.result`.

Unconfigured/unavailable providers produce `blocked_provider` without invented
model output. A configured call failure produces `provider_error`. A malformed
provider result is omitted and reported as top-level `invalid_model_response`
and `model.status: "invalid_response"`; `mode` stays `local`. Provider exception
messages are not exposed because they can contain credentials or raw requests.
This module does not read credentials, call `generate_json` as a substitute for
examining binary evidence, store uploads, fetch external resources, or run a
network request in local mode.

## Examples and checks

From the project root:

```python
from backend.studio.multimodal import run

local = run({
    "inputs": [{"type": "code", "name": "math.py", "text": "def add(a, b):\n    return a + b\n"}]
})
assert local["mode"] == "local"
assert local["evidence_manifest"][0]["metadata"]["functions"] == 1

# Requests an actual configured provider. Without configuration this returns
# blocked_provider alongside the grounded local manifest and findings.
live = run({
    "mode": "live",
    "prompt": "Explain the function and state any limits of this review.",
    "inputs": [{"type": "code", "name": "math.py", "text": "def add(a, b):\n    return a + b\n"}]
})
```

Run the offline correctness suite with:

```bash
python -m unittest discover -s tests -p 'test_multimodal.py' -v
```

Tests cover exact UTF-8 content hashes, image headers, MIME mismatch, PCM WAV
duration and truncation, unsupported audio/video signatures, static code
inspection, JSON syntax, request bounds, partial acceptance, provider gating,
grounded model IDs, malformed model responses, and exception-detail redaction.
Provider tests use injected test doubles; they do not claim a live model has
been exercised.
