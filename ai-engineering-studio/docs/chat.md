# Portfolio Copilot

Versioned candidate statements in `data/portfolio.json` are the only corpus. The service retrieves page/document excerpts through Atlas RAG and returns inspectable citations. Unknown achievements must not be invented. The public browser uses its local excerpt retriever; provider synthesis is optional on a configured backend.

`POST /api/feedback` records an explicitly submitted 1–5 rating and comment in backend SQLite. Run metadata is stored without raw documents or prompts. `GET /api/metrics` counts runs and feedback; it does not infer unique people. No 500-user claim, external usage count, or active feedback is claimed.
