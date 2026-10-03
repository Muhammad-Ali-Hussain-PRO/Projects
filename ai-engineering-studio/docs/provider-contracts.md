# Configured provider contracts

`backend/studio/provider.py` implements server-only OpenAI HTTP adapters. Set
`OPENAI_API_KEY` and `OPENAI_MODEL` on the backend to enable model features. Never
put the key into browser source. `OPENAI_STT_MODEL` and `OPENAI_TTS_MODEL` optionally
override the existing `whisper-1` and `tts-1` defaults; this project does not choose
a replacement model on the caller's behalf.

`generate_json(prompt, schema)` uses the Responses endpoint with JSON schema
output and `store: false`, then independently validates the returned JSON. A
refusal, missing structured output, schema failure, or HTTP error fails explicitly.
Raw provider error bodies are not returned to the UI. `store: false` requests
response-storage behavior; it is not a claim about account-level data retention.

`generate_multimodal(prompt, attachments, schema)` accepts at most eight supplied
attachments and labels each with its evidence ID. Text/code is preserved up to
256 KiB UTF-8 per attachment. Images are bounded to 8 MiB. PNG/JPEG/WebP and still
GIF use image inputs. BMP is converted to PNG; conversions are capped at twenty
million pixels. Animated GIF is rejected explicitly rather than silently using
one frame. Header-only offline inspection does not imply that every binary byte
is valid; image preparation or the remote endpoint can still reject the file.

Audio attachments are actually transcribed before the structured model request.
They enter model reasoning as speech transcripts, not as a claim that environmental
sounds, speakers, emotion, or recording quality were assessed. WAV, WebM, MP3,
MP4/M4A, Ogg, and FLAC use matching extension-bearing multipart filenames and
appropriate MIME types. `transcribe_audio(data, mime)` returns real adapter text,
or fails explicitly for missing output. `synthesize_speech(text)` requests MP3
bytes with the configured speech model and alloy voice; text beyond 4,000
characters is rejected rather than truncated.

The current format contracts were checked against official OpenAI documentation:

- [Vision input requirements](https://developers.openai.com/api/docs/guides/images-vision)
- [Transcription API file formats](https://developers.openai.com/api/reference/cli/resources/audio/subresources/transcriptions/methods/create)
- [Speech API request contract](https://developers.openai.com/api/reference/cli/resources/audio/subresources/speech/methods/create)

Tests use `httpx.MockTransport` for actual request serialization, while all returned
transcripts, structured answers, and MP3 bytes are clearly deterministic fixtures.
They verify request/schema handling, format mapping and conversion, full text
transmission, speech-only audio interpretation, errors, and bounded inputs. No real
provider account, actual network call, or credential is used:

```bash
python -m unittest discover -s tests -p test_provider.py -v
```

Provider dependencies are `httpx`, `jsonschema`, and Pillow. Local review, voice
session control, and multimodal evidence inspection use the standard library.
