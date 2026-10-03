# AI Engineering Studio browser workspace

The React/TypeScript app in `frontend/` builds under `/projects/`. It provides ten keyboard-accessible workspaces, a filterable catalog, sample inputs, inspectable/exportable JSON results, implementation links, source downloads, and safely rendered Markdown documentation. It uses system fonts and requests no external font service.

## Actual browser capabilities

| Workspace | Browser operation | Python/backend extension |
| --- | --- | --- |
| Atlas RAG | Page-aware UTF-8 text chunking, BM25 retrieval, exact extractive quotes | PDF extraction, cosine vector fusion, optional learned embeddings/reranking, labeled evaluation, optional live generation |
| Scout Search | Explicit cached educational evidence fixture | Real Brave API search, adaptive planning, bounded page fetching, verified extracts |
| Pulse Voice | Real microphone RMS/silence detection, interruption, browser speech synthesis of supplied text | Ordered audio sessions and configured STT/reasoning/TTS adapters |
| Prism Multimodal | File byte size, SHA-256, browser-decoded image dimensions/audio duration, readable text metadata | Container inspection, Python AST, optional grounded model interpretation |
| Review Swarm | Named line-pattern roles with source evidence and test suggestions | Python AST checks, security/test roles, bounded model proposals |
| DomainLab | Inference from the actual saved TF-IDF/SGD classifier weights | Identical Python inference and recorded held-out experiment artifacts |
| Guardrail Lab | Injection-pattern evidence and synthetic PII redaction | Labeled synthetic benchmark and optional configured-model probes |
| QueryLens | Validated CSV parsing and limited count/sum/average/min/max/group/preview grammar | Restricted read-only SQLite execution and optional model planner |
| ContextBridge MCP | Inspectable explanation of the protocol boundary and tools | Actual MCP initialization, discovery, and tool call in a stdio session |
| Portfolio Copilot | Extractive retrieval over versioned candidate/project facts | Python retrieval and optional live synthesis; opt-in stored feedback |

Browser CSV analytics never evaluates code or executes SQL. Model confidence is uncalibrated, and the fixed SQL templates are never executed. The optional causal language model training entrypoint is explicitly unrun. Search fixture results are never labeled live. Browser metadata does not imply image interpretation or audio transcription. Local checks do not establish general security or model-safety guarantees.

The browser model is an actual port of `studio.model.vectorize` and `predict`, using the authoritative `artifacts/model/adapted_model.json`. Its test checks every one of the 128 recorded held-out predictions and confidence values to an absolute tolerance of `1e-12`.

## Connected backend

The connection dialog accepts an HTTPS base URL or an HTTP loopback address, verifies Studio health and authenticated metrics, and holds `STUDIO_API_TOKEN` only in React memory. It does not persist credentials in local/session storage, URLs, or exports. Reloading or disconnecting clears the connection. Provider/search keys remain server-side. Requests use bearer auth and reject redirects. The user selects browser, Python local, or configured live execution explicitly. Inputs are submitted only when running a tool; feedback is saved only after its explicit connected-backend submission.

The microphone requires browser permission and a secure origin. Browser mode measures actual RMS amplitude; it does not claim transcription. Connected recording uses `MediaRecorder`, a real 60-second stop, ordered mutation sequence numbers, VAD-onset playback cancellation, stale-response epochs, and server playback completion events. Browser text-to-speech availability and voices vary by platform. Provider calls require server configuration; microphone or model availability failures are shown.

## Build and verification

Use Node 22.18 or later for the TypeScript-stripping test runner; verification used Node 24.19.0. The Vite build transpiles TypeScript/JSX without a separate `tsc` step. Dependencies are React 19.1.1, React DOM 19.1.1, Vite 6.4.1, and jsdom 26.1.0; the repository includes their npm lockfile.

```bash
cd frontend
npm ci
npm test
npm run build
```

`scripts/prepare-assets.mjs` copies authoritative catalog/facts, trained model/evaluation, verification evidence, and Markdown docs into `public/`; no server secrets are included. `dist/` is the deployable output. The runtime uses four initial static JSON GET requests. Local sample runs do not make provider or backend requests.

The tests cover malformed and quoted CSV, real grouping/aggregation, hostile SQL rejection, exact page citations, no-evidence abstention, static review, injection/PII spans, all 128 model-parity cases, and a React/jsdom smoke run of all ten workspaces. `artifacts/verification/frontend_tests.json` records the actual local build and test execution. No authenticated external AI or search-provider integration is claimed by this report.
