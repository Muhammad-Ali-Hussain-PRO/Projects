"""Mock-HTTP contract tests: no network or real account credentials required."""

import base64
import io
import json
import os
import unittest
from contextlib import ExitStack
from unittest.mock import patch

import httpx
from PIL import Image

from backend.studio import provider


SCHEMA = {"type": "object", "properties": {"answer": {"type": "string"}}, "required": ["answer"], "additionalProperties": False}


def structured_response(answer="fixture answer"):
    return {"output": [{"type": "message", "content": [{"type": "output_text", "text": json.dumps({"answer": answer})}]}]}


def image_bytes(format_name, *, animated=False):
    output = io.BytesIO()
    first = Image.new("RGB", (8, 7), (220, 30, 60))
    if animated:
        first.save(output, format=format_name, save_all=True, append_images=[Image.new("RGB", (8, 7), (0, 180, 90))], duration=50, loop=0)
    else:
        first.save(output, format=format_name)
    return output.getvalue()


class ProviderContractTests(unittest.TestCase):
    def setUp(self):
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.stack.enter_context(patch.dict(os.environ, {"OPENAI_API_KEY": "unit-test-fixture-key", "OPENAI_MODEL": "unit-test-fixture-model"}, clear=True))
        self.requests = []
        original_client = httpx.Client

        def handler(request):
            self.requests.append(request)
            return self.respond(request)

        def client_factory(**kwargs):
            return original_client(**kwargs, transport=httpx.MockTransport(handler))

        self.stack.enter_context(patch.object(provider.httpx, "Client", side_effect=client_factory))
        self.respond = lambda request: httpx.Response(200, json=structured_response())

    def test_configuration_requires_key_and_model(self):
        self.assertTrue(provider.is_configured())
        with patch.dict(os.environ, {"OPENAI_MODEL": ""}):
            self.assertFalse(provider.is_configured())
        with patch.dict(os.environ, {"OPENAI_API_KEY": ""}):
            self.assertFalse(provider.is_configured())
            with self.assertRaises(provider.ProviderUnavailable):
                provider.generate_json("fixture", SCHEMA)
        self.assertEqual(self.requests, [])

    def test_responses_request_schema_and_aggregation(self):
        self.respond = lambda request: httpx.Response(200, json={"output": [{"content": [{"type": "output_text", "text": '{"answer":'}, {"type": "output_text", "text": '"fixture"}'}]}]})
        result = provider.generate_json("review fixture", SCHEMA)
        self.assertEqual(result, {"answer": "fixture"})
        request = self.requests[0]
        self.assertEqual(request.method, "POST")
        self.assertEqual(str(request.url), "https://api.openai.com/v1/responses")
        body = json.loads(request.content)
        self.assertEqual(body["model"], "unit-test-fixture-model")
        self.assertFalse(body["store"])
        self.assertEqual(body["input"][0]["content"], [{"type": "input_text", "text": "review fixture"}])
        self.assertEqual(body["text"]["format"]["schema"], SCHEMA)
        self.assertTrue(body["text"]["format"]["strict"])

    def test_refusal_invalid_json_and_schema_errors_do_not_become_results(self):
        responses = [
            {"output": [{"content": [{"type": "refusal", "refusal": "fixture refusal"}]}]},
            {"output": [{"content": [{"type": "output_text", "text": "not json"}]}]},
            {"output": [{"content": [{"type": "output_text", "text": '{"answer": 123}'}]}]},
        ]
        for response in responses:
            with self.subTest(response=response):
                self.respond = lambda request, response=response: httpx.Response(200, json=response)
                with self.assertRaises(provider.ProviderUnavailable):
                    provider.generate_json("fixture", SCHEMA)

    def test_http_error_redacts_raw_provider_data(self):
        self.respond = lambda request: httpx.Response(401, json={"error": {"message": "sensitive-unit-test-fixture"}})
        with self.assertRaises(provider.ProviderUnavailable) as error:
            provider.generate_json("private source fixture", SCHEMA)
        self.assertIn("HTTP 401", str(error.exception))
        self.assertNotIn("sensitive-unit-test-fixture", str(error.exception))

    def test_transport_error_redacts_raw_exception(self):
        def fail(request):
            raise httpx.ConnectError("sensitive-unit-test-fixture", request=request)
        self.respond = fail
        with self.assertRaises(provider.ProviderUnavailable) as error:
            provider.generate_json("fixture", SCHEMA)
        self.assertNotIn("sensitive-unit-test-fixture", str(error.exception))

    def test_incomplete_response_is_explicitly_rejected(self):
        self.respond = lambda request: httpx.Response(200, json={"status": "incomplete", **structured_response()})
        with self.assertRaises(provider.ProviderUnavailable):
            provider.generate_json("fixture", SCHEMA)

    def test_all_local_image_formats_have_supported_live_content(self):
        attachments = [
            {"id": f"input-{index}", "type": "image", "name": format_name, "mime_type": mime, "data_base64": base64.b64encode(image_bytes(format_name)).decode("ascii")}
            for index, (format_name, mime) in enumerate([("PNG", "image/png"), ("JPEG", "image/jpeg"), ("GIF", "image/gif"), ("WEBP", "image/webp"), ("BMP", "image/bmp")], 1)
        ]
        provider.generate_multimodal("inspect fixture", attachments, SCHEMA)
        content = json.loads(self.requests[0].content)["input"][0]["content"]
        images = [item for item in content if item["type"] == "input_image"]
        self.assertEqual(len(images), 5)
        for image, mime in zip(images, ("image/png", "image/jpeg", "image/gif", "image/webp", "image/png")):
            self.assertTrue(image["image_url"].startswith(f"data:{mime};base64,"))
        converted = base64.b64decode(images[-1]["image_url"].split(",", 1)[1])
        self.assertTrue(converted.startswith(b"\x89PNG"))
        self.assertTrue(any('"attachment_id": "input-5"' in item.get("text", "") for item in content))

    def test_animated_gif_is_explicitly_rejected_before_http(self):
        attachment = {"type": "image", "mime_type": "image/gif", "data_base64": base64.b64encode(image_bytes("GIF", animated=True)).decode("ascii")}
        with self.assertRaises(provider.ProviderUnavailable) as error:
            provider.generate_multimodal("fixture", [attachment], SCHEMA)
        self.assertIn("Animated", str(error.exception))
        self.assertEqual(self.requests, [])

    def test_bmp_conversion_pixel_bound_before_http(self):
        attachment = {"type": "image", "mime_type": "image/bmp", "data_base64": base64.b64encode(image_bytes("BMP")).decode()}
        with patch.object(provider,"MAX_IMAGE_PIXELS",1):
            with self.assertRaises(provider.ProviderUnavailable):
                provider.generate_multimodal("fixture", [attachment], SCHEMA)
        self.assertEqual(self.requests, [])

    def test_text_and_code_are_preserved_beyond_previous_slice(self):
        text = "x" * 40_000 + "🧪 unique final evidence"
        code = "# fixture\n" + "a = 1\n" * 5000
        provider.generate_multimodal("fixture", [{"id": "input-1", "type": "text", "text": text}, {"id": "input-2", "type": "code", "text": code}], SCHEMA)
        content = json.loads(self.requests[0].content)["input"][0]["content"]
        transmitted = [item["text"] for item in content if item["type"] == "input_text"]
        self.assertIn(text, transmitted)
        self.assertIn(code, transmitted)

    def test_base64_text_alias_is_decoded_without_truncation(self):
        text = "value\n" * 7000
        attachment = {"kind": "code", "mime": "text/plain", "content_base64": base64.b64encode(text.encode()).decode()}
        provider.generate_multimodal("fixture", [attachment], SCHEMA)
        content = json.loads(self.requests[0].content)["input"][0]["content"]
        self.assertEqual(content[-1]["text"], text)

    def test_text_bounds_fail_before_http(self):
        with self.assertRaises(provider.ProviderUnavailable):
            provider.generate_multimodal("fixture", [{"type": "text", "text": "x" * (provider.MAX_TEXT_ATTACHMENT_BYTES + 1)}], SCHEMA)
        self.assertEqual(self.requests, [])

    def test_audio_mime_maps_to_correct_extension_and_json_response(self):
        self.respond = lambda request: httpx.Response(200, json={"text": "actual fixture transcript"})
        formats = [("audio/webm;codecs=opus", "webm"), ("audio/wav", "wav"), ("audio/x-wav", "wav"), ("audio/mpeg", "mp3"), ("audio/mp4", "m4a"), ("audio/ogg", "ogg"), ("audio/flac", "flac")]
        for mime, extension in formats:
            with self.subTest(mime=mime):
                self.assertEqual(provider.transcribe_audio(b"fixture audio bytes", mime), "actual fixture transcript")
                request = self.requests[-1]
                self.assertEqual(request.url.path, "/v1/audio/transcriptions")
                self.assertIn(f'filename="recording.{extension}"'.encode(), request.content)
                self.assertIn(b'"response_format"\r\n\r\njson', request.content)
                self.assertIn(b"whisper-1", request.content)

    def test_multimodal_audio_is_transcribed_then_grounded_as_speech_only(self):
        self.respond = lambda request: httpx.Response(200, json={"text": "fixture transcript"} if request.url.path.endswith("transcriptions") else structured_response())
        provider.generate_multimodal("inspect", [{"id": "input-3", "type": "audio", "mime_type": "audio/ogg", "data_base64": base64.b64encode(b"fixture").decode()}], SCHEMA)
        self.assertEqual([request.url.path for request in self.requests], ["/v1/audio/transcriptions", "/v1/responses"])
        content = json.loads(self.requests[-1].content)["input"][0]["content"]
        self.assertIn("speech only", content[-1]["text"])
        self.assertIn("fixture transcript", content[-1]["text"])

    def test_audio_missing_transcript_and_invalid_mime_are_explicit_failures(self):
        self.respond = lambda request: httpx.Response(200, json={"text": ""})
        with self.assertRaises(provider.ProviderUnavailable):
            provider.transcribe_audio(b"fixture", "audio/wav")
        with self.assertRaises(provider.ProviderUnavailable):
            provider.transcribe_audio(b"fixture", "audio/unknown")
        with self.assertRaises(provider.ProviderUnavailable):
            provider.transcribe_audio(b"", "audio/wav")
        self.assertEqual(len(self.requests), 1)

    def test_speech_request_returns_actual_bytes_with_no_silent_truncation(self):
        self.respond = lambda request: httpx.Response(200, content=b"fixture-mp3-bytes", headers={"Content-Type": "audio/mpeg"})
        self.assertEqual(provider.synthesize_speech("Spoken fixture text"), b"fixture-mp3-bytes")
        request = self.requests[0]
        self.assertEqual(request.url.path, "/v1/audio/speech")
        body = json.loads(request.content)
        self.assertEqual(body["input"], "Spoken fixture text")
        self.assertEqual(body["response_format"], "mp3")
        self.assertEqual(body["voice"], "alloy")
        self.assertEqual(body["model"], "tts-1")
        with self.assertRaises(provider.ProviderUnavailable):
            provider.synthesize_speech("x" * 4001)
        self.assertEqual(len(self.requests), 1)

    def test_empty_speech_response_is_explicit_failure(self):
        self.respond = lambda request: httpx.Response(200, content=b"")
        with self.assertRaises(provider.ProviderUnavailable):
            provider.synthesize_speech("fixture")

    def test_invalid_attachment_base64_does_not_reach_http(self):
        with self.assertRaises(provider.ProviderUnavailable):
            provider.generate_multimodal("fixture", [{"type": "image", "mime_type": "image/png", "data_base64": "not base64!!"}], SCHEMA)
        self.assertEqual(self.requests, [])


if __name__ == "__main__":
    unittest.main()
