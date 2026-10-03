"""Voice state and cancellation tests; adapters are deterministic test doubles."""

import base64
import json
import threading
import unittest

from backend.studio.voice import (
    MAX_HISTORY_MESSAGES,
    MAX_IN_FLIGHT_TURNS,
    MAX_SESSIONS,
    SESSION_TTL_SECONDS,
    VoiceEngine,
)


class FakeProvider:
    def __init__(self, configured=True, block_stage=None):
        self.configured = configured
        self.block_stage = block_stage
        self.entered = threading.Event()
        self.release = threading.Event()
        self.calls = []
        self.fail_stage = None

    def is_configured(self):
        return self.configured

    def _stage(self, stage):
        self.calls.append(stage)
        if stage == self.block_stage:
            self.entered.set()
            if not self.release.wait(3):
                raise RuntimeError("test synchronization timeout")
        if self.fail_stage == stage:
            raise RuntimeError("secret API credential must never reach response")

    def transcribe_audio(self, data, mime):
        self._stage("transcription")
        self.audio_input = (data, mime)
        return "Actual test transcript"

    def generate_json(self, prompt, schema):
        self._stage("reply")
        self.prompt = prompt
        return {"reply": "Actual test reply"}

    def synthesize_speech(self, text):
        self._stage("synthesis")
        return b"test-provider-mp3"


class ConcurrentProvider(FakeProvider):
    def __init__(self):
        super().__init__(block_stage="reply")
        self.condition = threading.Condition()
        self.reply_count = 0

    def _stage(self, stage):
        if stage == "reply":
            with self.condition:
                self.reply_count += 1
                self.condition.notify_all()
        return super()._stage(stage)

    def await_replies(self, count):
        with self.condition:
            return self.condition.wait_for(lambda: self.reply_count >= count, timeout=2)


class VoiceTests(unittest.TestCase):
    def setUp(self):
        self.provider = FakeProvider()
        self.engine = VoiceEngine(provider=self.provider)
        self.start = self.engine.handle({"action": "start", "mode": "live"})
        self.session_id = self.start["session_id"]

    def event(self, action, sequence=None, **kwargs):
        data = {"action": action, "session_id": self.session_id, **kwargs}
        if sequence is not None:
            data["sequence"] = sequence
        return self.engine.handle(data)

    @staticmethod
    def audio_payload():
        return {"audio_base64": base64.b64encode(b"real-recording-placeholder-for-test").decode(), "audio_mime": "audio/webm;codecs=opus", "duration_ms": 1500}

    def test_real_adapter_pipeline_and_playback_ack(self):
        self.assertTrue(self.start["provider_configured"])
        self.assertEqual(self.event("vad_start", 1)["state"], "listening")
        self.assertEqual(self.event("vad_end", 2)["state"], "idle")
        result = self.event("turn", 3, **self.audio_payload())
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["state"], "speaking")
        self.assertEqual(result["source"], "provider")
        self.assertEqual(self.provider.calls, ["transcription", "reply", "synthesis"])
        self.assertEqual(self.provider.audio_input[1], "audio/webm")
        self.assertEqual(base64.b64decode(result["audio_base64"]), b"test-provider-mp3")
        self.assertEqual(result["audio_mime"], "audio/mpeg")
        self.assertEqual(len(result["conversation"]), 2)
        json.dumps(result, allow_nan=False)
        self.assertEqual(self.event("playback_end", 4, turn_id=result["turn_id"])["state"], "idle")

    def test_typed_transcript_skips_stt(self):
        result = self.event("turn", 1, text="Hello")
        self.assertEqual(result["input_source"], "typed")
        self.assertEqual(self.provider.calls, ["reply", "synthesis"])
        self.assertEqual(result["transcript"], "Hello")

    def test_local_mode_never_calls_live_adapters(self):
        result = self.event("turn", 1, text="Hello", mode="local")
        self.assertEqual(result["status"], "blocked_provider")
        self.assertNotIn("reply", result)
        self.assertNotIn("audio_base64", result)
        self.assertEqual(self.provider.calls, [])

    def test_unconfigured_provider_does_not_fabricate_outputs(self):
        self.provider.configured = False
        result = self.event("turn", 1, **self.audio_payload())
        self.assertEqual(result["status"], "blocked_provider")
        self.assertEqual(result["conversation"], [])
        self.assertEqual(self.provider.calls, [])
        self.assertNotIn("transcript", result)

    def test_duplicates_and_old_sequences_cannot_mutate_state(self):
        first = self.event("vad_start", 2)
        duplicate = self.event("close", 2)
        old = self.event("interrupt", 1)
        self.assertEqual(duplicate["status"], "stale_event")
        self.assertEqual(old["status"], "stale_event")
        self.assertEqual(old["epoch"], first["epoch"])
        self.assertEqual(self.event("status")["state"], "listening")

    def test_invalid_audio_does_not_consume_sequence(self):
        bad = self.event("turn", 1, audio_base64="not base64!", audio_mime="audio/webm")
        self.assertEqual(bad["status"], "invalid_input")
        self.assertEqual(self.event("turn", 1, text="Retry same sequence")["status"], "ok")

    def test_input_bounds_and_json_type_validation(self):
        cases = [
            {"text": " "}, {"text": "a" * 4001},
            {"text": "hello", "audio_base64": "YQ=="},
            {"audio_base64": "YQ==", "audio_mime": "text/plain"},
            {"audio_base64": "YQ==", "duration_ms": 60001},
            {"audio_base64": "YQ==", "duration_ms": float("nan")},
        ]
        for payload in cases:
            with self.subTest(payload_keys=list(payload)):
                self.assertEqual(self.event("turn", 1, **payload)["status"], "invalid_input")
        self.assertEqual(self.engine.handle({"action": []})["status"], "invalid_input")
        self.assertEqual(self.engine.handle(None)["status"], "invalid_input")
        self.assertEqual(self.event("vad_start", True)["status"], "invalid_input")
        self.assertEqual(self.provider.calls, [])

    def _run_blocked_turn(self, stage, cancel_action):
        provider = FakeProvider(block_stage=stage)
        engine = VoiceEngine(provider=provider)
        session_id = engine.handle({"action": "start", "mode": "live"})["session_id"]
        result = []
        thread = threading.Thread(target=lambda: result.append(engine.handle({
            "action": "turn", "session_id": session_id, "sequence": 1, **self.audio_payload(),
        })))
        thread.start()
        self.assertTrue(provider.entered.wait(2), "provider did not reach blocked stage")
        cancellation = engine.handle({"action": cancel_action, "session_id": session_id, "sequence": 2})
        self.assertTrue(cancellation["stop_playback"])
        provider.release.set()
        thread.join(3)
        self.assertFalse(thread.is_alive())
        self.assertEqual(result[0]["status"], "cancelled")
        self.assertNotIn("transcript", result[0])
        self.assertNotIn("reply", result[0])
        self.assertNotIn("audio_base64", result[0])
        self.assertEqual(result[0]["conversation"], [])
        expected = ["transcription", "reply", "synthesis"]
        self.assertEqual(provider.calls, expected[:expected.index(stage) + 1])

    def test_interrupt_rejects_late_transcription(self):
        self._run_blocked_turn("transcription", "interrupt")

    def test_vad_barge_in_rejects_late_model_reply(self):
        self._run_blocked_turn("reply", "vad_start")

    def test_close_rejects_late_synthesized_audio(self):
        self._run_blocked_turn("synthesis", "close")

    def test_new_turn_supersedes_old_turn(self):
        provider = FakeProvider(block_stage="reply")
        engine = VoiceEngine(provider=provider)
        session_id = engine.handle({"action": "start", "mode": "live"})["session_id"]
        old_result = []
        thread = threading.Thread(target=lambda: old_result.append(engine.handle({
            "action": "turn", "session_id": session_id, "sequence": 1, "text": "old",
        })))
        thread.start()
        self.assertTrue(provider.entered.wait(2))
        provider.block_stage = None
        new_result = engine.handle({"action": "turn", "session_id": session_id, "sequence": 2, "text": "new"})
        provider.release.set()
        thread.join(3)
        self.assertFalse(thread.is_alive())
        self.assertEqual(new_result["status"], "ok")
        self.assertEqual(new_result["turn_id"], 2)
        self.assertEqual(old_result[0]["status"], "cancelled")
        status = engine.handle({"action": "status", "session_id": session_id})
        self.assertEqual(status["conversation"][0]["content"], "new")
        self.assertEqual(len(status["conversation"]), 2)

    def test_old_playback_ack_cannot_clear_new_turn(self):
        first = self.event("turn", 1, text="first")
        second = self.event("turn", 2, text="second")
        old_ack = self.event("playback_end", 3, turn_id=first["turn_id"])
        self.assertEqual(old_ack["status"], "stale_event")
        self.assertEqual(old_ack["turn_id"], second["turn_id"])
        # The stale acknowledgement does not consume its sequence.
        self.assertEqual(self.event("playback_end", 3, turn_id=second["turn_id"])["status"], "ok")

    def test_session_inflight_bound_retains_interrupt_capacity(self):
        provider = ConcurrentProvider()
        engine = VoiceEngine(provider=provider)
        session_id = engine.handle({"action": "start", "mode": "live"})["session_id"]
        results = []
        threads = []
        for sequence in (1, 2):
            thread = threading.Thread(target=lambda value=sequence: results.append(engine.handle({
                "action": "turn", "session_id": session_id, "sequence": value, "text": str(value),
            })))
            threads.append(thread)
            thread.start()
            self.assertTrue(provider.await_replies(sequence))
        busy = engine.handle({"action": "turn", "session_id": session_id, "sequence": 3, "text": "third"})
        self.assertEqual(busy["status"], "busy")
        self.assertEqual(busy["last_sequence"], 2)
        interrupted = engine.handle({"action": "interrupt", "session_id": session_id, "sequence": 3})
        self.assertEqual(interrupted["status"], "ok")
        provider.release.set()
        for thread in threads:
            thread.join(3)
            self.assertFalse(thread.is_alive())
        self.assertEqual([result["status"] for result in results], ["cancelled", "cancelled"])
        self.assertEqual(engine.handle({"action": "turn", "session_id": session_id, "sequence": 4, "text": "retry"})["status"], "ok")

    def test_global_inflight_bound_releases_slots_on_cancel(self):
        provider = ConcurrentProvider()
        engine = VoiceEngine(provider=provider)
        sessions = [engine.handle({"action": "start", "mode": "live"})["session_id"] for _ in range(MAX_IN_FLIGHT_TURNS + 1)]
        threads = []
        results = []
        for index, session_id in enumerate(sessions[:-1], 1):
            thread = threading.Thread(target=lambda value=session_id: results.append(engine.handle({
                "action": "turn", "session_id": value, "sequence": 1, "text": "hello",
            })))
            threads.append(thread)
            thread.start()
            self.assertTrue(provider.await_replies(index))
        busy = engine.handle({"action": "turn", "session_id": sessions[-1], "sequence": 1, "text": "hello"})
        self.assertEqual(busy["status"], "busy")
        self.assertEqual(busy["last_sequence"], 0)
        for session_id in sessions[:-1]:
            engine.handle({"action": "close", "session_id": session_id, "sequence": 2})
        provider.release.set()
        for thread in threads:
            thread.join(3)
            self.assertFalse(thread.is_alive())
        self.assertTrue(all(result["status"] == "cancelled" for result in results))
        self.assertEqual(engine.handle({"action": "turn", "session_id": sessions[-1], "sequence": 1, "text": "retry"})["status"], "ok")

    def test_provider_errors_are_sanitized_and_not_committed(self):
        self.provider.fail_stage = "synthesis"
        result = self.event("turn", 1, text="Hello")
        self.assertEqual(result["status"], "provider_error")
        self.assertEqual(result["stage"], "synthesis")
        self.assertNotIn("secret", json.dumps(result))
        self.assertEqual(result["conversation"], [])
        self.assertEqual(result["state"], "idle")

    def test_history_is_bounded(self):
        for sequence in range(1, MAX_HISTORY_MESSAGES + 4):
            result = self.event("turn", sequence, text=f"turn {sequence}")
            self.assertEqual(result["status"], "ok")
        self.assertEqual(len(result["conversation"]), MAX_HISTORY_MESSAGES)
        self.assertEqual(result["conversation"][0]["role"], "user")

    def test_expiry_and_session_count_are_bounded(self):
        now = [0.0]
        engine = VoiceEngine(provider=self.provider, clock=lambda: now[0])
        ids = [engine.handle({"action": "start"})["session_id"] for _ in range(MAX_SESSIONS)]
        self.assertEqual(engine.handle({"action": "start"})["status"], "session_limit")
        engine.handle({"action": "close", "session_id": ids[0], "sequence": 1})
        self.assertEqual(engine.handle({"action": "start"})["status"], "ok")
        now[0] = SESSION_TTL_SECONDS
        self.assertEqual(engine.handle({"action": "status", "session_id": ids[1]})["status"], "session_not_found")
        self.assertEqual(engine.handle({"action": "start"})["status"], "ok")

    def test_closed_session_cannot_resume(self):
        self.assertEqual(self.event("close", 1)["state"], "closed")
        self.assertEqual(self.event("turn", 2, text="Hello")["status"], "session_closed")
        self.assertEqual(self.event("status")["state"], "closed")


if __name__ == "__main__":
    unittest.main()
