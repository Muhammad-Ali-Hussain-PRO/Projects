# Local API and integration verification

The backend runs the actual project modules. The public frontend can connect to this backend using a session-entered bearer token; provider credentials stay on the server.

```bash
python -m pip install -r backend/requirements.txt
# Configure STUDIO_API_TOKEN with a private token before starting.
python -m uvicorn studio.app:app --app-dir backend --host 127.0.0.1 --port 8000
```

`STUDIO_ALLOWED_ORIGINS` is a comma-separated allowlist, defaulting to `http://localhost:5173,http://127.0.0.1:5173`. A deployed frontend needs its exact origin in this list. CORS preflight permits allowed origins without credentials; actual API requests require `Authorization: Bearer <STUDIO_API_TOKEN>`. Missing server token returns 503, and an incorrect token returns 401.

| Endpoint | Behavior |
| --- | --- |
| `GET /api/health` | Public health, provider configuration status and ten project IDs |
| `POST /api/run/{project}` | Execute a project, record metadata and return its actual result |
| `POST /api/stream/{project}` | Observable stage/result SSE; search additionally emits its actual plan, search, source and synthesis events |
| `POST /api/feedback` | Save an explicitly supplied 1–5 rating and optional comment |
| `GET /api/metrics` | Aggregate backend runs and opt-in feedback |
| `WS /api/voice` | First JSON message supplies a token; subsequent messages operate ordered voice sessions |

The HTTP boundary enforces 40 authenticated requests per minute per client IP and a 24,000,000-byte request body cap before JSON parsing. It checks both declared length and actual received chunks, including missing or falsely small Content-Length. Individual projects also enforce their own bounds. Provider-dependent modes return explicit unavailable/error states when credentials or models are missing.

`STUDIO_DB` chooses the SQLite activity database; the default is `var/studio.sqlite3`. Run rows retain only ID, project, status, duration and creation time. Feedback includes the user-supplied comment. Run counts are activity counts, not unique-user counts. Search SSE communicates observable work stages and source evidence; no token-by-token model streaming is claimed.

## Actual complete test report

```bash
python training/verify_backend.py
```

This executes every backend test and writes actual per-case outcomes, dependency versions, timings and per-file counts to `artifacts/verification/backend_tests.json`, with the full runner log beside it. It includes FastAPI HTTP/WebSocket integration, all ten catalog samples, real MCP subprocess negotiation, restricted tool calls, domain-model artifact inference, and local safety benchmarks. Provider transport/model tests use explicit mocks. Tests use a temporary SQLite database and do not contact live accounts.

Standalone CI at `.github/workflows/ci.yml` installs pinned Python dependencies, reproduces the small classifier training and safety report, runs the full suite, and tests/builds the frontend. The optional causal-LM path remains unrun unless local weights and its dependencies are deliberately supplied.
