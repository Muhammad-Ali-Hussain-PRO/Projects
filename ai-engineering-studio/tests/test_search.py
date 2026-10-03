"""Network-free tests of the live request path and explicit fixture mode."""

import json
import socket
import unittest
from unittest.mock import patch

from backend.studio import search

try:
    import httpx
except ImportError:
    httpx = None


SOURCES = [
    {"title": "Alpha evidence", "url": "https://alpha.example.org/report", "snippet": "The research system uses bounded searches.", "content": "The research system uses bounded searches. Citations identify source evidence."},
    {"title": "Beta evidence", "url": "https://beta.example.org/report", "snippet": "Search systems retrieve evidence from multiple sources.", "content": "Search systems retrieve evidence from multiple sources. Unsupported claims must be discarded."},
]


class FixtureSearchTests(unittest.TestCase):
    def test_fixture_is_deterministic_serializable_and_never_networks(self):
        payload = {"mode": "fixture", "query": "search evidence", "fixture_sources": SOURCES}
        with patch.object(search, "_new_client", side_effect=AssertionError("network")), patch.object(search, "_provider_json", side_effect=AssertionError("model")):
            first = search.run(payload)
            second = search.run({**payload, "use_provider": True})
        self.assertEqual(first, second)
        self.assertEqual(first["status"], "ok")
        self.assertEqual(first["metrics"]["requests"], 0)
        self.assertEqual(len(first["steps"]), 3)
        json.dumps(first)
        for citation in first["citations"]:
            source = next(s for s in first["sources"] if s["id"] == citation["source_id"])
            self.assertIn(citation["quote"], source["content"])
            self.assertEqual(citation["url"], source["url"])
            self.assertIn(f"[{source['id']}]", first["answer"])

    def test_source_and_step_caps_are_enforced(self):
        result = search.run({"mode": "fixture", "query": "search", "fixture_sources": SOURCES, "max_steps": 999, "max_sources": 1, "results_per_query": 100})
        self.assertEqual(result["metrics"]["searches"], search.MAX_STEPS)
        self.assertEqual(len(result["sources"]), 1)
        self.assertLessEqual(len(result["plan"]), search.MAX_STEPS)

    def test_empty_results_do_not_invent_an_answer(self):
        result = search.run({"mode": "fixture", "query": "unrelatedword", "fixture_sources": SOURCES})
        self.assertEqual(result["status"], "empty")
        self.assertEqual(result["citations"], [])
        self.assertIn("No usable source evidence", result["answer"])

    def test_fixture_requires_valid_public_source_urls(self):
        result = search.run({"mode": "fixture", "query": "research", "fixture_sources": [{**SOURCES[0], "url": "http://127.0.0.1/secrets"}]})
        self.assertEqual(result["sources"], [])
        self.assertIn("An unsafe source URL was discarded.", result["warnings"])

    def test_invalid_input_is_a_json_error(self):
        for payload in (None, [], {}, {"query": "   "}, {"query": "x", "mode": []}, {"query": "x", "max_steps": True}, {"query": "x", "timeout": float("nan")}, {"query": "x", "mode": "fixture", "fixture_sources": "invalid"}):
            with self.subTest(payload=payload):
                result = search.run(payload)
                self.assertEqual(result["status"], "error")
                self.assertEqual(result["error"]["code"], "invalid_input")
                json.dumps(result, allow_nan=False)

    def test_missing_key_does_not_silently_use_fixtures(self):
        with patch.dict("os.environ", {"SEARCH_API_KEY": ""}), patch.object(search, "_new_client", side_effect=AssertionError("network")):
            result = search.run({"query": "Brave search"})
        self.assertEqual(result["mode"], "live")
        self.assertEqual(result["error"]["code"], "missing_search_key")
        self.assertEqual(result["sources"], [])

    def test_stream_finishes_with_the_same_result(self):
        payload = {"mode": "fixture", "query": "search", "fixture_sources": SOURCES, "max_steps": 1}
        events = list(search.iter_events(payload))
        self.assertEqual(events[0]["event"], "plan")
        self.assertEqual(events[-1]["event"], "done")
        self.assertIn("search_start", [e["event"] for e in events])
        self.assertIn("answer", [e["event"] for e in events])
        self.assertEqual(events[-1]["data"], search.run(payload))

    def test_url_filter_blocks_local_private_credentials_and_non_http(self):
        blocked = ["file:///etc/passwd", "http://localhost/a", "http://example.local/a", "http://127.0.0.1/a", "http://169.254.169.254/latest/meta-data", "http://10.0.0.1/a", "http://[::1]/a", "http://[::ffff:127.0.0.1]/a", "http://example.org:8080/a", "https://user:pass@example.org/a", "https://example.org\\@127.0.0.1/a", "http://2130706433/a", "http://0x7f000001/a", "https://example.org/\nsecret"]
        for url in blocked:
            with self.subTest(url=url), self.assertRaises(search.SearchError):
                search.validate_url(url, resolve_dns=False)
        self.assertEqual(search.validate_url("https://EXAMPLE.org/path#fragment", resolve_dns=False), "https://example.org/path")

    def test_dns_public_and_private_mixture_is_rejected(self):
        public = (socket.AF_INET, socket.SOCK_STREAM, 6, "", ("93.184.216.34", 443))
        private = (socket.AF_INET, socket.SOCK_STREAM, 6, "", ("192.168.1.8", 443))
        with patch.object(socket, "getaddrinfo", return_value=[public, private]), self.assertRaises(search.SearchError) as raised:
            search.validate_url("https://public-looking.example.org/")
        self.assertEqual(raised.exception.code, "unsafe_url")


@unittest.skipUnless(httpx is not None, "httpx runtime is required for live-path tests")
class LiveSearchTests(unittest.TestCase):
    def run_mocked(self, payload, handler, **environment):
        client = httpx.Client(transport=httpx.MockTransport(handler), follow_redirects=False)
        with patch.dict("os.environ", {"SEARCH_API_KEY": "test-secret", "SEARCH_API_URL": search.DEFAULT_SEARCH_URL, **environment}), patch.object(search, "_new_client", return_value=client):
            return search.run({"query": "research evidence", "max_steps": 1, "max_fetches": 1, **payload})

    def test_live_brave_requests_fetch_html_and_keep_key_server_side(self):
        calls = []

        def handler(request):
            calls.append(request)
            if request.url.host == "api.search.brave.com":
                self.assertEqual(request.method, "GET")
                self.assertEqual(request.headers["X-Subscription-Token"], "test-secret")
                self.assertEqual(request.url.params["q"], "research evidence")
                return httpx.Response(200, json={"web": {"results": [{"title": "Research", "url": "https://evidence.example.org/paper", "description": "Search snippet evidence."}]}})
            self.assertNotIn("X-Subscription-Token", request.headers)
            return httpx.Response(200, headers={"Content-Type": "text/html"}, text="<html><script>Invented evil claim.</script><p>Research evidence is retrieved from public sources.</p><p>Requests are bounded.</p></html>")

        result = self.run_mocked({}, handler)
        self.assertEqual(result["status"], "ok")
        self.assertEqual(len(calls), 2)
        self.assertTrue(result["sources"][0]["fetched"])
        self.assertEqual(result["sources"][0]["evidence_origin"], "page_text")
        self.assertNotIn("Invented evil", result["answer"])
        self.assertNotIn("test-secret", json.dumps(result))
        self.assertIn("Research evidence", result["answer"])

    def test_duplicate_urls_and_unsafe_results_are_filtered(self):
        def handler(request):
            return httpx.Response(200, json={"web": {"results": [
                {"title": "A", "url": "https://evidence.example.org/report?utm_source=first", "description": "Public research evidence is available."},
                {"title": "A duplicate", "url": "https://evidence.example.org/report?utm_source=second", "description": "Duplicate."},
                {"title": "Secret", "url": "http://127.0.0.1/secrets", "description": "Private research evidence."},
            ]}})
        result = self.run_mocked({"fetch_pages": False}, handler)
        self.assertEqual(len(result["sources"]), 1)
        self.assertEqual(result["sources"][0]["url"], "https://evidence.example.org/report")
        self.assertIn("An unsafe source URL was discarded.", result["warnings"])

    def test_redirect_to_private_target_never_requests_target(self):
        calls = []

        def handler(request):
            calls.append(str(request.url))
            if request.url.host == "api.search.brave.com":
                return httpx.Response(200, json={"web": {"results": [{"title": "A", "url": "https://evidence.example.org/redirect", "description": "Search evidence remains available."}]}})
            return httpx.Response(302, headers={"Location": "http://127.0.0.1/secret"})
        result = self.run_mocked({}, handler)
        self.assertEqual(result["status"], "ok")
        self.assertEqual(len(calls), 2)
        self.assertFalse(result["sources"][0]["fetched"])
        self.assertTrue(any("Private" in warning for warning in result["warnings"]))
        self.assertIn("Search evidence remains available", result["answer"])

    def test_redirect_loop_is_bounded(self):
        def handler(request):
            if request.url.host == "api.search.brave.com":
                return httpx.Response(200, json={"web": {"results": [{"title": "A", "url": "https://evidence.example.org/loop", "description": "Evidence exists."}]}})
            return httpx.Response(302, headers={"Location": "/loop"})
        result = self.run_mocked({}, handler)
        self.assertEqual(result["metrics"]["requests"], 4)
        self.assertTrue(any("redirect limit" in warning for warning in result["warnings"]))

    def test_provider_rate_limit_is_explicit_and_never_fixture_data(self):
        result = self.run_mocked({}, lambda request: httpx.Response(429, text="test-secret"))
        self.assertEqual(result["status"], "error")
        self.assertEqual(result["error"]["code"], "rate_limited")
        self.assertEqual(result["sources"], [])
        self.assertNotIn("test-secret", json.dumps(result))

    def test_success_before_provider_failure_returns_partial_evidence(self):
        calls = []

        def handler(request):
            calls.append(request)
            if len(calls) == 1:
                return httpx.Response(200, json={"web": {"results": [{"title": "A", "url": "https://evidence.example.org/report", "description": "Research evidence exists in this source."}]}})
            return httpx.Response(503)
        result = self.run_mocked({"max_steps": 2, "fetch_pages": False}, handler)
        self.assertEqual(result["status"], "partial")
        self.assertEqual(result["error"]["code"], "search_provider_error")
        self.assertEqual(len(result["citations"]), 1)

    def test_search_response_size_is_bounded(self):
        result = self.run_mocked({}, lambda request: httpx.Response(200, content=b"x" * (search.SEARCH_BODY_LIMIT + 1)))
        self.assertEqual(result["error"]["code"], "response_too_large")
        self.assertEqual(result["metrics"]["requests"], 1)

    def test_total_duration_budget_stops_slow_stream_and_retains_prior_evidence(self):
        clock = [0.0]
        calls = []

        class SlowStream(httpx.SyncByteStream):
            def __iter__(self):
                clock[0] = 2.0
                yield b"source bytes"

        def handler(request):
            calls.append(request)
            if request.url.host == "api.search.brave.com":
                return httpx.Response(200, json={"web": {"results": [{"title": "A", "url": "https://evidence.example.org/report", "description": "Research evidence remains available."}]}})
            return httpx.Response(200, headers={"Content-Type": "text/plain"}, stream=SlowStream())
        with patch.object(search.time, "monotonic", side_effect=lambda: clock[0]):
            result = self.run_mocked({"max_duration": 1}, handler)
        self.assertEqual(result["status"], "partial")
        self.assertEqual(result["error"]["code"], "research_timeout")
        self.assertEqual(len(calls), 2)
        self.assertIn("Research evidence remains available", result["answer"])

    def test_malformed_provider_json_is_handled(self):
        result = self.run_mocked({}, lambda request: httpx.Response(200, json={"web": None}))
        self.assertEqual(result["error"]["code"], "invalid_provider_response")

    def test_model_can_only_select_verified_source_quotes(self):
        exact = "Research evidence is public and verifiable."

        def handler(request):
            return httpx.Response(200, json={"web": {"results": [{"title": "A", "url": "https://evidence.example.org/report", "description": exact}]}})
        outputs = [
            {"queries": ["research evidence"]},
            {"claims": [{"source_id": "S1", "quote": "An unsupported fabricated fact."}, {"source_id": "S999", "quote": exact}, {"source_id": "S1", "quote": exact}]},
        ]
        with patch.object(search, "_provider_json", side_effect=outputs):
            result = self.run_mocked({"use_provider": True, "fetch_pages": False}, handler)
        self.assertEqual(result["answer"], exact + " [S1]")
        self.assertEqual(len(result["citations"]), 1)
        self.assertNotIn("fabricated", result["answer"])
        self.assertIn("An unsupported model quote was discarded.", result["warnings"])

    def test_agent_refines_the_plan_after_observing_evidence(self):
        queries, prompts = [], []

        def handler(request):
            queries.append(request.url.params["q"])
            return httpx.Response(200, json={"web": {"results": [{"title": "Observed evidence", "url": "https://evidence.example.org/report", "description": "Research evidence is public and verifiable."}]}})

        def model(prompt, schema):
            prompts.append(prompt)
            if schema is search.QUOTE_SCHEMA:
                return {"claims": []}
            return {"queries": ["initial research" if len(prompts) == 1 else "observed gap research"]}
        with patch.object(search, "_provider_json", side_effect=model):
            result = self.run_mocked({"max_steps": 2, "use_provider": True, "fetch_pages": False}, handler)
        self.assertEqual(queries, ["initial research", "observed gap research"])
        self.assertIn("Observed evidence", prompts[1])
        self.assertEqual(result["metrics"]["searches"], 2)

    def test_model_outage_uses_real_search_and_source_extracts(self):
        def handler(request):
            return httpx.Response(200, json={"web": {"results": [{"title": "A", "url": "https://evidence.example.org/report", "description": "Research evidence is retained after model failure."}]}})
        with patch.object(search, "_provider_json", side_effect=RuntimeError("test-secret")):
            result = self.run_mocked({"use_provider": True, "fetch_pages": False}, handler)
        self.assertEqual(result["status"], "ok")
        self.assertIn("Research evidence", result["answer"])
        self.assertNotIn("test-secret", json.dumps(result))
        self.assertEqual(len(result["warnings"]), 2)

    def test_public_transport_pins_dns_and_preserves_host_and_tls_name(self):
        seen = []
        transport = search._PublicTransport()
        transport._inner.close()
        transport._inner = httpx.MockTransport(lambda request: (seen.append(request), httpx.Response(200))[1])
        public = (socket.AF_INET, socket.SOCK_STREAM, 6, "", ("93.184.216.34", 443))
        with patch.object(socket, "getaddrinfo", return_value=[public]):
            with httpx.Client(transport=transport) as client:
                response = client.get("https://public.example.org/path")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(seen[0].url.host, "93.184.216.34")
        self.assertEqual(seen[0].headers["Host"], "public.example.org")
        self.assertEqual(seen[0].extensions["sni_hostname"], "public.example.org")

    def test_closing_stream_releases_http_client(self):
        client = httpx.Client(transport=httpx.MockTransport(lambda request: httpx.Response(200)))
        with patch.dict("os.environ", {"SEARCH_API_KEY": "test-secret"}), patch.object(search, "_new_client", return_value=client):
            events = search.iter_events({"query": "research"})
            self.assertEqual(next(events)["event"], "plan")
            self.assertFalse(client.is_closed)
            events.close()
        self.assertTrue(client.is_closed)


if __name__ == "__main__":
    unittest.main()
