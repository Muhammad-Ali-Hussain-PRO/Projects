"""Multimodal correctness tests; no credentials or live network calls required."""

import base64
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
from types import ModuleType
import unittest
from unittest.mock import Mock, patch
import wave
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))
from studio import multimodal


def png(width=2, height=1):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    pixels = zlib.compress(b"\x00" + b"\xff\x00\x00" * width)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", pixels) + chunk(b"IEND", b"")


def binary(kind, data, mime=None, name="sample"):
    result = {"type": kind, "name": name, "data_base64": base64.b64encode(data).decode("ascii")}
    if mime:
        result["mime_type"] = mime
    return result


def provider(configured=True, result=None, error=None):
    fake = ModuleType("studio.provider")
    fake.is_configured = Mock(return_value=configured)
    fake.generate_multimodal = Mock(return_value=result, side_effect=error)
    return fake


class MultimodalTests(unittest.TestCase):
    def test_text_hashes_exact_utf8_and_has_stable_evidence_ids(self):
        text = "Café\nline two\n"
        result = multimodal.run({"inputs": [{"type": "text", "text": text, "name": "note.txt"}]})
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["mode"], "local")
        entry = result["evidence_manifest"][0]
        self.assertEqual(entry["id"], "input-1")
        self.assertEqual(entry["sha256"], hashlib.sha256(text.encode()).hexdigest())
        self.assertEqual(entry["bytes"], len(text.encode()))
        self.assertEqual(entry["metadata"]["lines"], 2)
        self.assertNotIn("text", entry)
        json.dumps(result, allow_nan=False)

    def test_png_dimensions_without_fabricating_visual_description(self):
        image = png(3, 1)
        result = multimodal.run({"prompt": "What objects are visible?", "inputs": [binary("image", image, "image/png")]})
        entry = result["evidence_manifest"][0]
        self.assertEqual((entry["metadata"]["width"], entry["metadata"]["height"]), (3, 1))
        self.assertEqual(entry["sha256"], hashlib.sha256(image).hexdigest())
        self.assertIn("has not been examined", result["findings"][0]["observation"])
        self.assertTrue(any("cannot describe objects" in x for x in result["limitations"]))

    def test_image_header_formats(self):
        fixtures = [
            (b"GIF89a" + struct.pack("<HH", 4, 5) + b"\x00\x00\x00", "image/gif", (4, 5)),
            (b"\xff\xd8\xff\xc0\x00\x08\x08\x00\x05\x00\x04\x00\xff\xd9", "image/jpeg", (4, 5)),
            (b"RIFF\x16\x00\x00\x00WEBPVP8X\x0a\x00\x00\x00\x00\x00\x00\x00\x03\x00\x00\x04\x00\x00", "image/webp", (4, 5)),
            (b"BM" + bytes(12) + struct.pack("<Iii", 40, 4, -5) + bytes(28), "image/bmp", (4, 5)),
        ]
        for data, mime, expected in fixtures:
            with self.subTest(mime=mime):
                result = multimodal.run({"inputs": [binary("image", data, mime)]})
                self.assertEqual(result["status"], "ok", result)
                metadata = result["evidence_manifest"][0]["metadata"]
                self.assertEqual((metadata["width"], metadata["height"]), expected)

    def test_mime_mismatch_is_rejected_before_any_model_request(self):
        fake = provider(result={"summary": "wrong", "findings": [], "limitations": []})
        with patch.dict(sys.modules, {"studio.provider": fake}):
            result = multimodal.run({"mode": "live", "inputs": [binary("image", png(), "image/jpeg")]})
        self.assertEqual(result["status"], "error")
        self.assertIn("does not match", result["errors"][0]["message"])
        fake.generate_multimodal.assert_not_called()

    def test_data_url_support_and_declared_mime_check(self):
        encoded = base64.b64encode(png()).decode()
        result = multimodal.run({"attachments": [{"kind": "image", "data": "data:image/png;base64," + encoded}]})
        self.assertEqual(result["status"], "ok")
        bad = multimodal.run({"inputs": [{"type": "image", "data": "data:image/jpeg;base64," + encoded}]})
        self.assertEqual(bad["status"], "error")

    def test_pcm_wav_duration_and_truncation(self):
        recording = io.BytesIO()
        with wave.open(recording, "wb") as stream:
            stream.setnchannels(1)
            stream.setsampwidth(2)
            stream.setframerate(8000)
            stream.writeframes(bytes(16000))
        data = recording.getvalue()
        result = multimodal.run({"inputs": [binary("audio", data, "audio/x-wav")]})
        self.assertEqual(result["status"], "ok")
        metadata = result["evidence_manifest"][0]["metadata"]
        self.assertEqual(metadata["duration_seconds"], 1)
        self.assertEqual(metadata["sample_rate_hz"], 8000)
        self.assertIn("not been listened", result["findings"][0]["observation"])
        truncated = multimodal.run({"inputs": [binary("audio", data[:-10])]})
        self.assertEqual(truncated["status"], "error")

    def test_audio_signature_does_not_claim_transcription(self):
        result = multimodal.run({"inputs": [binary("audio", b"fLaC\x80\x00\x00\x00", "audio/flac")]})
        self.assertEqual(result["status"], "ok")
        self.assertNotIn("duration_seconds", result["evidence_manifest"][0]["metadata"])
        self.assertTrue(any("does not listen, transcribe" in x for x in result["limitations"]))

    def test_aac_header_and_ogg_video_are_not_mislabeled_mp3_or_audio(self):
        aac = multimodal.run({"inputs": [binary("audio", b"\xff\xf1" + bytes(20))]})
        self.assertEqual(aac["status"], "error")
        theora = b"OggS" + bytes(22) + b"\x01\x07\x80theora"
        result = multimodal.run({"inputs": [binary("audio", theora)]})
        self.assertEqual(result["status"], "error")
        self.assertIn("video", result["errors"][0]["message"])

    def test_python_static_analysis_never_executes_submitted_code(self):
        target = Path(__file__).with_name("must_not_be_created_by_submitted_code.txt")
        code = f"import pathlib\npathlib.Path({str(target)!r}).write_text('executed')\nclass A:\n    async def f(self):\n        return 1\n"
        self.assertFalse(target.exists())
        result = multimodal.run({"inputs": [{"type": "code", "name": "review.py", "content": code}]})
        metadata = result["evidence_manifest"][0]["metadata"]
        self.assertEqual(metadata["syntax"], "valid")
        self.assertEqual((metadata["functions"], metadata["classes"], metadata["import_statements"]), (1, 1, 1))
        self.assertFalse(target.exists())

    def test_python_syntax_error_is_grounded_to_line(self):
        result = multimodal.run({"inputs": [{"type": "code", "language": "python", "text": "def broken(:\n pass"}]})
        metadata = result["evidence_manifest"][0]["metadata"]
        self.assertEqual(metadata["syntax"], "invalid")
        self.assertEqual(metadata["syntax_error_line"], 1)

    def test_python_structural_counts_are_not_published_when_ast_bound_is_exceeded(self):
        with patch.object(multimodal, "MAX_AST_NODES", 4):
            result = multimodal.run({"inputs": [{"type": "code", "language": "python", "text": "def f():\n    return 1\n"}]})
        metadata = result["evidence_manifest"][0]["metadata"]
        self.assertEqual(metadata["syntax"], "valid")
        self.assertEqual(metadata["structural_inspection"], "not_inspected")
        self.assertNotIn("functions", metadata)
        self.assertTrue(any("AST nodes" in x for x in result["limitations"]))

    def test_json_text_validation_and_non_python_language_limit(self):
        result = multimodal.run({"inputs": [
            {"type": "text", "mime_type": "application/json", "text": '{"ok": true}'},
            {"type": "text", "mime_type": "application/json", "text": "NaN"},
            {"type": "code", "name": "sample.js", "text": "not valid js"},
        ]})
        metadata = [x["metadata"] for x in result["evidence_manifest"]]
        self.assertTrue(metadata[0]["json_valid"])
        self.assertFalse(metadata[1]["json_valid"])
        self.assertNotIn("syntax", metadata[2])

    def test_partial_acceptance_and_ids_keep_original_positions(self):
        result = multimodal.run({"inputs": [
            {"type": "image", "data_base64": "bad!"},
            {"type": "text", "text": "valid"},
        ]})
        self.assertEqual(result["status"], "partial")
        self.assertEqual(result["evidence_manifest"][0]["id"], "input-2")
        self.assertEqual(result["errors"][0]["input_id"], "input-1")

    def test_per_input_and_aggregate_limits(self):
        with patch.object(multimodal, "MAX_TEXT_BYTES", 4):
            result = multimodal.run({"inputs": [{"type": "text", "text": "ééé"}]})
            self.assertEqual(result["status"], "error")
        with patch.object(multimodal, "MAX_TOTAL_BYTES", 5):
            result = multimodal.run({"inputs": [{"type": "text", "text": "abc"}] * 2})
            self.assertEqual(result["status"], "partial")
            self.assertEqual(len(result["evidence_manifest"]), 1)
        with patch.object(multimodal, "MAX_BINARY_BYTES", 4):
            result = multimodal.run({"inputs": [binary("image", png())]})
            self.assertEqual(result["status"], "error")

    def test_malformed_request_returns_json_safe_validation_result(self):
        requests = [None, {}, {"inputs": []}, {"inputs": [{}] * 9}, {"prompt": 2, "inputs": [{}]},
                    {"prompt": "\ud800", "inputs": [{}]}, {"prompt": "x" * 8193, "inputs": [{}]},
                    {"use_model": "yes", "inputs": [{}]}, {"mode": [], "inputs": [{}]},
                    {"inputs": [None]}, {"inputs": [{"type": "text", "text": "\ud800"}]}]
        for request in requests:
            with self.subTest(request=repr(request)[:80]):
                result = multimodal.run(request)
                self.assertEqual(result["status"], "error")
                json.dumps(result, allow_nan=False)

    def test_local_mode_never_calls_provider(self):
        fake = provider(error=AssertionError("must not call"))
        with patch.dict(sys.modules, {"studio.provider": fake}):
            result = multimodal.run({"inputs": [{"type": "text", "text": "hello"}]})
        self.assertEqual(result["model"]["status"], "not_requested")
        fake.is_configured.assert_not_called()
        fake.generate_multimodal.assert_not_called()

    def test_live_unconfigured_is_explicitly_blocked_with_local_evidence(self):
        fake = provider(configured=False)
        with patch.dict(sys.modules, {"studio.provider": fake}):
            result = multimodal.run({"mode": "model", "inputs": [{"type": "text", "text": "hello"}]})
        self.assertEqual(result["status"], "blocked_provider")
        self.assertEqual(result["mode"], "local")
        self.assertEqual(result["model"]["status"], "blocked_provider")
        self.assertTrue(result["evidence_manifest"])
        fake.generate_multimodal.assert_not_called()

    def test_configured_model_receives_attachments_and_schema_and_returns_separate_result(self):
        model = {"summary": "The text says hello.", "findings": [{"input_id": "input-1", "observation": "Contains a greeting.", "confidence": "high"}], "limitations": ["Only one short input."]}
        fake = provider(result=model)
        with patch.dict(sys.modules, {"studio.provider": fake}):
            result = multimodal.run({"use_model": True, "prompt": "Compare evidence.", "inputs": [{"type": "text", "text": "hello"}]})
        self.assertEqual(result["mode"], "live")
        self.assertEqual(result["model"]["status"], "completed")
        args = fake.generate_multimodal.call_args.args
        self.assertIn("Compare evidence.", args[0])
        self.assertEqual(args[1][0]["text"], "hello")
        self.assertEqual(args[1][0]["sha256"], hashlib.sha256(b"hello").hexdigest())
        self.assertEqual(args[2]["required"], ["summary", "findings", "limitations"])
        self.assertEqual(result["model"]["result"]["findings"][0]["source"], "configured_model")
        self.assertEqual(result["findings"][0]["source"], "offline_inspection")
        json.dumps(result, allow_nan=False)

    def test_provider_error_never_leaks_sensitive_exception_details(self):
        fake = provider(error=RuntimeError("secret-token-for-test"))
        with patch.dict(sys.modules, {"studio.provider": fake}):
            result = multimodal.run({"mode": "live", "inputs": [{"type": "text", "text": "hello"}]})
        self.assertEqual(result["status"], "provider_error")
        self.assertNotIn("secret-token-for-test", json.dumps(result))
        self.assertEqual(result["mode"], "local")

    def test_unreferenced_or_invalid_model_output_is_not_published(self):
        responses = [
            {"summary": "unknown source", "findings": [{"input_id": "input-99", "observation": "guess", "confidence": "high"}], "limitations": []},
            {"summary": "invalid", "findings": [{"input_id": "input-1", "observation": "guess", "confidence": 0.99}], "limitations": []},
            {"summary": "invalid", "findings": [{"input_id": [], "observation": "guess", "confidence": "high"}], "limitations": []},
            {"summary": "invalid", "findings": [{"input_id": "input-1", "observation": "guess", "confidence": []}], "limitations": []},
            {"summary": "invalid", "findings": [], "limitations": [None]},
            "raw text",
        ]
        for value in responses:
            with self.subTest(value=value), patch.dict(sys.modules, {"studio.provider": provider(result=value)}):
                result = multimodal.run({"mode": "live", "inputs": [{"type": "text", "text": "hello"}]})
                self.assertEqual(result["status"], "invalid_model_response")
                self.assertEqual(result["model"]["status"], "invalid_response")
                self.assertNotIn("result", result["model"])
                self.assertEqual(result["mode"], "local")


if __name__ == "__main__":
    unittest.main()
