"""Integration tests for the authenticated local API using actual project modules."""
import json
import os
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "backend"))
from fastapi.testclient import TestClient
from starlette.websockets import WebSocketDisconnect
from studio import app as application


class APITests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.db_path = Path(self.temp.name) / "runs.sqlite3"
        self.environment = mock.patch.dict(os.environ, {
            "STUDIO_API_TOKEN": "integration-test-token", "STUDIO_DB": str(self.db_path),
            "OPENAI_API_KEY": "", "OPENAI_MODEL": "", "BRAVE_SEARCH_API_KEY": "", "SEARCH_API_KEY": "",
        })
        self.environment.start()
        application._rates.clear()
        self.client = TestClient(application.app)
        self.client.__enter__()
        self.headers = {"Authorization": "Bearer integration-test-token"}

    def tearDown(self):
        self.client.__exit__(None, None, None)
        self.environment.stop()
        self.temp.cleanup()

    def test_health_is_public_and_truthful(self):
        response = self.client.get("/api/health")
        self.assertEqual(response.status_code, 200)
        self.assertFalse(response.json()["provider_configured"])
        self.assertTrue(response.json()["authentication_configured"])
        self.assertEqual(len(response.json()["projects"]), 10)

    def test_authentication_required_and_missing_setup_is_explicit(self):
        self.assertEqual(self.client.get("/api/metrics").status_code, 401)
        self.assertEqual(self.client.get("/api/metrics", headers={"Authorization": "Bearer incorrect"}).status_code, 401)
        with mock.patch.dict(os.environ, {"STUDIO_API_TOKEN": ""}):
            self.assertEqual(self.client.get("/api/metrics", headers=self.headers).status_code, 503)

    def test_cors_preflight_passes_without_credentials_for_allowed_origin(self):
        headers = {"Origin": "http://localhost:5173", "Access-Control-Request-Method": "POST", "Access-Control-Request-Headers": "authorization,content-type"}
        response = self.client.options("/api/run/model", headers=headers)
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.headers.get("access-control-allow-origin"), "http://localhost:5173")
        headers["Origin"] = "https://untrusted.example.test"
        rejected = self.client.options("/api/run/model", headers=headers)
        self.assertEqual(rejected.status_code, 400)
        self.assertNotIn("access-control-allow-origin", rejected.headers)

    def test_all_ten_catalog_samples_execute_actual_modules(self):
        catalog = json.loads((ROOT / "data/catalog.json").read_text())
        self.assertEqual({p["id"] for p in catalog}, application.PROJECTS)
        for project in catalog:
            with self.subTest(project=project["id"]):
                response = self.client.post("/api/run/" + project["id"], json=project["sample"], headers=self.headers)
                self.assertEqual(response.status_code, 200, response.text)
                result = response.json()
                self.assertIn(result["status"], {"ok", "complete", "completed", "needs_review"}, response.text)
                self.assertIsInstance(result["run_id"], str)
                self.assertGreaterEqual(result["duration_ms"], 0)
                if project["id"] == "voice" and result.get("session_id"):
                    self.client.post("/api/run/voice", json={"action": "close", "session_id": result["session_id"], "sequence": 1}, headers=self.headers)
        metrics = self.client.get("/api/metrics", headers=self.headers).json()
        self.assertEqual({row["project"] for row in metrics["runs"]}, application.PROJECTS)
        # The activity store retains metrics only, not submitted prompts/results.
        with sqlite3.connect(self.db_path) as db:
            columns = [row[1] for row in db.execute("PRAGMA table_info(runs)")]
        self.assertEqual(columns, ["id", "project", "status", "duration_ms", "created"])

    def test_unknown_project_and_invalid_input(self):
        self.assertEqual(self.client.post("/api/run/unknown", json={}, headers=self.headers).status_code, 404)
        self.assertEqual(self.client.post("/api/run/model", json={"request": ""}, headers=self.headers).status_code, 422)
        self.assertEqual(self.client.post("/api/run/model", json=[], headers=self.headers).status_code, 422)

    def test_request_size_and_rate_limits(self):
        oversized = self.client.post("/api/run/model", content=b"{}", headers={**self.headers, "Content-Type": "application/json", "Content-Length": "24000001"})
        self.assertEqual(oversized.status_code, 413)
        malformed = self.client.post("/api/run/model", content=b"{}", headers={**self.headers, "Content-Type": "application/json", "Content-Length": "invalid"})
        self.assertEqual(malformed.status_code, 400)
        application._rates.clear()
        for _ in range(40):
            self.assertEqual(self.client.get("/api/metrics", headers=self.headers).status_code, 200)
        self.assertEqual(self.client.get("/api/metrics", headers=self.headers).status_code, 429)

    def test_chunked_body_is_bounded_and_cors_covers_auth_errors(self):
        body = json.dumps({"request": "a" * 2048}).encode()
        def chunks():
            for offset in range(0, len(body), 512):
                yield body[offset:offset + 512]
        with mock.patch.object(application, "MAX_REQUEST_BYTES", 1024):
            response = self.client.post("/api/run/model", content=chunks(), headers={**self.headers, "Content-Type": "application/json"})
            false_length = self.client.post("/api/run/model", content=body, headers={**self.headers, "Content-Type": "application/json", "Content-Length": "2"})
        self.assertEqual(response.status_code, 413)
        self.assertEqual(false_length.status_code, 413)
        unauthorized = self.client.get("/api/metrics", headers={"Origin": "http://localhost:5173"})
        self.assertEqual(unauthorized.status_code, 401)
        self.assertEqual(unauthorized.headers.get("access-control-allow-origin"), "http://localhost:5173")

    def test_feedback_validation_and_durable_aggregation(self):
        self.assertEqual(self.client.post("/api/feedback", json={"project": "model", "rating": True}, headers=self.headers).status_code, 422)
        for rating in [2, 4]:
            response = self.client.post("/api/feedback", json={"project": "model", "rating": rating, "comment": "integration fixture"}, headers=self.headers)
            self.assertEqual(response.status_code, 200)
            self.assertEqual(response.json()["status"], "saved")
        response = self.client.get("/api/metrics", headers=self.headers).json()
        self.assertEqual(response["feedback"], [{"project": "model", "responses": 2, "mean_rating": 3.0}])

    def test_stage_event_stream_carries_actual_result(self):
        response = self.client.post("/api/stream/model", json={"request": "count all customers"}, headers=self.headers)
        self.assertEqual(response.status_code, 200)
        self.assertIn("text/event-stream", response.headers["content-type"])
        self.assertIn("event: stage", response.text)
        self.assertIn("event: result", response.text)
        result_chunk = response.text.split("event: result\ndata: ", 1)[1].split("\n\n", 1)[0]
        self.assertEqual(json.loads(result_chunk)["intent"], "count")

    def test_search_event_stream_exposes_observable_pipeline_steps(self):
        project = next(p for p in json.loads((ROOT / "data/catalog.json").read_text()) if p["id"] == "search")
        response = self.client.post("/api/stream/search", json=project["sample"], headers=self.headers)
        self.assertEqual(response.status_code, 200)
        for name in ["plan", "search_start", "search_results", "source", "synthesis", "answer", "result"]:
            self.assertIn("event: " + name + "\n", response.text)
        result_chunk = response.text.split("event: result\ndata: ", 1)[1].split("\n\n", 1)[0]
        self.assertEqual(json.loads(result_chunk)["mode"], "fixture")
        metrics = self.client.get("/api/metrics", headers=self.headers).json()
        self.assertEqual(metrics["runs"][0]["project"], "search")
        self.assertEqual(metrics["runs"][0]["runs"], 1)

    def test_provider_mode_does_not_fabricate_results_without_configuration(self):
        response = self.client.post("/api/run/analyst", json={"question": "total revenue by region", "mode": "live"}, headers=self.headers)
        self.assertEqual(response.status_code, 503)
        safety_response = self.client.post("/api/run/safety", json={"mode": "provider"}, headers=self.headers)
        self.assertEqual(safety_response.status_code, 200)
        self.assertEqual(safety_response.json()["status"], "provider_unavailable")

    def test_voice_websocket_authentication_and_real_session_controls(self):
        with self.client.websocket_connect("/api/voice") as websocket:
            websocket.send_json({"token": "integration-test-token"})
            websocket.send_json({"action": "start", "mode": "local", "sequence": 0})
            started = websocket.receive_json()
            self.assertEqual(started["status"], "ok")
            self.assertEqual(started["mode"], "local")
            websocket.send_json({"action": "close", "session_id": started["session_id"], "sequence": 1})
            closed = websocket.receive_json()
            self.assertEqual(closed["state"], "closed")

    def test_voice_websocket_rejects_invalid_first_message(self):
        for first in [{"token": "incorrect"}, 42]:
            with self.subTest(first=first):
                with self.client.websocket_connect("/api/voice") as websocket:
                    websocket.send_json(first)
                    with self.assertRaises(WebSocketDisconnect) as rejected:
                        websocket.receive_json()
                    self.assertEqual(rejected.exception.code, 1008)


if __name__ == "__main__":
    unittest.main()
