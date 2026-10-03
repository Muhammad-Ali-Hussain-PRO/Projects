# Voice workspace

`backend.studio.voice.run(payload)` applies one JSON event and returns a JSON object. It manages bounded microphone turns through the configured provider's actual transcription, structured generation, and speech synthesis adapters. It does not claim to be a WebRTC connection or fabricate speech in local mode.

## Provider and modes

The shared `backend.studio.provider` module must implement:

```python
is_configured() -> bool
transcribe_audio(data: bytes, mime: str) -> str
generate_json(prompt: str, schema: dict) -> dict
synthesize_speech(text: str) -> bytes  # MP3, audio/mpeg
```

`mode: "local"` is the default. Session, VAD, interrupt, status, and close events remain inspectable, while submitted turns return `blocked_provider` without transcript, reply, or generated audio. `mode: "live"` requires a configured provider. `"model"` is accepted as an alias for `"live"`. Mode can be selected when starting a session or overridden by a turn. No network adapter is called unless a live turn has a configured provider.

The browser owns microphone permission, recording, VAD measurement, and audio playback. The server accepts completed recordings and explicitly orders client events. Display the transport as bounded HTTP turns; do not display fabricated streaming partials, latency measurements, or connected-to-provider status for a local session.

## Events

Start a session:

```python
from backend.studio.voice import run

opened = run({"action": "start", "mode": "live"})
session_id = opened["session_id"]
```

The response contains `session_id`, `provider_configured`, `state`, `mode`, `epoch`, `last_sequence`, `turn_id`, `conversation`, and input limits. A start event normally uses sequence `0`; it can optionally supply a nonnegative starting sequence.

Every mutation after start requires `session_id` and a strictly increasing integer `sequence`. Duplicate or older events return `stale_event` without changing state. Gaps are accepted because HTTP events can be lost. `status` is read-only and does not require a sequence.

| Action | Behavior | Additional fields |
| --- | --- | --- |
| `status` | Read current state and committed conversation | None |
| `vad_start` | Start listening; invalidate any active reply or playback | None |
| `vad_end` | Move listening to idle; does not fabricate a transcript | None |
| `turn` | Submit a completed recording or explicitly typed transcript | `audio_base64`, `audio_mime`, optional `duration_ms`; or `text` |
| `interrupt` | Invalidate active work and return to listening | None |
| `playback_end` | Acknowledge playback of the current completed turn | `turn_id` from that turn's response |
| `close` | Invalidate active work and close the session | None |

An audio turn should follow the client VAD end or manual stop event:

```python
run({"action": "vad_start", "session_id": session_id, "sequence": 1})
run({"action": "vad_end", "session_id": session_id, "sequence": 2})
result = run({
    "action": "turn",
    "session_id": session_id,
    "sequence": 3,
    "audio_base64": recording_base64,  # Raw base64, not a data URL
    "audio_mime": "audio/webm;codecs=opus",
    "duration_ms": 2400,
})
```

For keyboard accessibility or a text-only provider check, replace the audio fields with `text: "Hello"`. A typed transcript skips transcription but still uses actual model generation and speech synthesis. Supplying both text and audio is rejected.

A completed live turn returns `status: "ok"`, `state: "speaking"`, `source: "provider"`, `input_source: "audio" | "typed"`, `transcript`, `reply`, `audio_base64`, `audio_mime: "audio/mpeg"`, the current `turn_id`, and measured `timing_ms`. Only fully successful turns enter `conversation`. Playback completion moves the session to idle:

```python
if result["status"] == "ok":
    # Play the decoded MP3 in the browser, then acknowledge its completion.
    run({"action": "playback_end", "session_id": session_id,
         "sequence": 4, "turn_id": result["turn_id"]})
```

## Interruptions and UI integration

The client must stop its current audio element immediately when microphone VAD detects new speech or the user presses Interrupt. Send `vad_start` or `interrupt` with the next sequence. These responses include `stop_playback: true` and `cancelled_turn_id`. The server increases the session epoch and discards late transcription, model, or synthesized audio responses. It checks cancellation between each provider stage. Already-running provider HTTP requests may finish and incur their normal cost; their outputs are never published as the current turn.

A submitted newer turn also supersedes an older in-flight turn. `playback_end` must include the correct turn ID so an old audio element's `ended` event cannot clear a newer reply. The client should maintain its latest requested sequence and ignore responses from earlier turn requests even if they arrived before the server received the interrupt. Successful results must also match the latest expected turn ID before playback.

`cancelled` responses contain no transcript, reply, or audio from discarded work. `blocked_provider` means local mode or unavailable configuration. `provider_error` identifies the failing `stage` (`transcription`, `reply`, or `synthesis`) with a sanitized error. `busy` means the bounded provider request slots are occupied. None of these statuses contains a fabricated fallback reply. Input errors, busy requests, duplicate/old events, and mismatched playback acknowledgements do not consume a sequence.

## Bounds and deployment

The implementation uses only the Python standard library. It holds at most 64 sessions, expires sessions after 15 minutes without a mutating event or completed turn, retains at most 24 committed messages, limits recordings to 8 MiB, and limits declared duration to 60 seconds. At most eight live turns can be in flight across the process, with at most two per session; superseded requests still hold their slot until the adapter returns. Transcripts and typed text are limited to 4,000 characters and replies to 1,500. Supported inputs are WebM, Ogg, WAV, MP3, MP4, and FLAC; MIME codec parameters are normalized. The browser must stop its recorder at 60 seconds; the server checks byte bounds and any declared duration without pretending to infer duration from arbitrary compressed media.

Sessions are process-local. Run a single API process for the supplied prototype. A multi-worker deployment needs a shared state store and consistent cancellation ownership. `close` releases a slot when the next session starts. An expired session returns `session_not_found` and must be restarted.

## Verification

Run `python -m unittest discover -s tests -p 'test_voice.py' -v` from the project root. Tests use explicitly named adapter test doubles; no mocked output is exposed by the application. They cover input limits, sequence handling, successful STT/model/TTS ordering, unconfigured/local blocking, bounded history and sessions, and deterministic interruptions during transcription, generation, and synthesis. They also verify newer-turn supersession and stale playback acknowledgements.
