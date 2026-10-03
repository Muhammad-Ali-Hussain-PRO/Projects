# Live agentic web search

`backend.studio.search.run(payload)` performs synchronous research and returns a JSON-serializable object. `iter_events(payload)` and its alias `run_stream` expose the same pipeline incrementally. Live mode makes actual HTTP requests with `httpx`; it never substitutes fixture data after a configuration or network failure.

## Configuration and usage

Set `SEARCH_API_KEY` on the server. `SEARCH_API_URL` optionally overrides the endpoint and defaults to `https://api.search.brave.com/res/v1/web/search`. The endpoint must be a public HTTPS URL on port 443 and implement Brave's response shape. The API key is sent only to the configured search endpoint in `X-Subscription-Token`; source page requests do not receive it. Environment proxy settings are disabled.

```python
from backend.studio.search import run, iter_events

result = run({
    "query": "What evidence supports hybrid retrieval?",
    "mode": "live",
    "max_steps": 3,
    "max_sources": 6,
    "max_fetches": 3,
    "use_provider": True,
})

for event in iter_events({"query": "hybrid retrieval research"}):
    print(event["event"], event["data"])
```

`query` is required; `question` is accepted as an alias. Queries are normalized to at most 500 characters and 70 words. The defaults are live mode, three search steps, six retained sources, three page fetches, four results per query, a ten-second request timeout, and a 45-second overall duration budget (`max_duration`, configurable from 1 to 120 seconds). `fetch_pages=False` uses search descriptions as explicitly labeled evidence. `use_provider=False` is the default; it uses a fixed multi-query plan and relevance-ranked source extracts.

With `use_provider=True`, the module calls `.provider.generate_json(prompt, schema)` for its initial plan, refines the remaining searches after observing evidence, and requests exact evidence quotes for synthesis. Planning or synthesis failures leave a warning and use the bounded algorithmic plan or evidence extracts. The model receives untrusted source content as data and cannot introduce URLs or uncited factual claims into the answer.

## Result and event contracts

The result contains `status`, `mode`, `query`, `answer`, `sources`, `citations`, `plan`, `steps`, `warnings`, and `metrics`. `status` is `ok`, `partial`, `empty`, or `error`. A provider failure after usable evidence has been collected returns `partial` with citations and a structured `error` containing a public `code` and `message`. Authentication and rate-limit failures are explicit errors. Error messages exclude raw upstream bodies and exception details that might disclose secrets.

Each source has a stable per-run `id` such as `S1`, a title, URL, snippet, content, originating queries, a `fetched` flag, and `evidence_origin` (`page_text`, `search_snippet`, or `fixture`). A successfully fetched source also has a `final_url`. Citations contain `source_id`, `url`, and `quote`. The answer is constructed exclusively from quotes verified as literal substrings of the associated source evidence, followed by their source IDs. This deliberately provides an extractive answer: it does not establish that a source is true, infer conclusions, or silently turn search snippets into full-page evidence.

Events use `{ "event": "name", "data": { ... } }`. Events include `plan`, `search_start`, `search_results`, `source`, `fetch_start`, `source_updated`, `plan_updated`, `warning`, `error`, `synthesis`, `answer`, and `done`. The final `done.data` is the full result. Closing an abandoned iterator immediately releases its HTTP client. Events can be forwarded as server-sent events or newline-delimited JSON by the caller.

## Limits and destination checks

The pipeline clamps search steps to five, retained sources to ten, fetched pages to six, and results per search to eight. It allows at most fifteen total HTTP requests, including redirects. A fetch follows at most two redirects. Search JSON is limited to 512 KB, a source response to 128 KB after decompression, source evidence to 8,000 characters, and selected quotes to 600 characters. Timeouts are limited to thirty seconds per request. Unsupported content types, oversized responses, fetch errors, and rejected redirects retain available search descriptions with a warning. HTML scripts, styles, templates, SVG, and noscript content are removed before evidence extraction.

The duration budget is checked before requests and model calls and between decoded response chunks; remaining time also reduces each request's timeout. It is a cooperative deadline, not a hard process deadline: synchronous DNS resolution, a model provider call, and a peer that continually drips incomplete protocol headers can exceed it before control returns. Production callers needing a strict wall-clock limit should enforce one in their worker process, and the configured model provider should enforce its own timeout. Evidence collected before the duration budget expires is retained in a partial result.

Only absolute HTTP(S) URLs on the standard ports are accepted. Credentials, local names, private/reserved IP addresses, malformed URLs, and oversized URLs are rejected. The transport checks every DNS answer, rejects mixed public/private results, pins the connection to a validated public IP, and preserves the original Host header and TLS SNI. Every redirect is independently validated. This prevents DNS resolution between validation and connection from changing the request into a local fetch. A public page's content is still untrusted and should not be executed or treated as an instruction.

## Explicit offline fixtures and verification

```python
result = run({
    "mode": "fixture",
    "query": "Brave Search API authentication",
})
```

Fixture mode uses a small documented fixed corpus or caller-supplied `fixture_sources` (up to 100 objects with `title`, `url`, `snippet`, and optional `content`). It performs lexical retrieval and the same citation validation without network access or model calls. Fixture results are deterministic and clearly labeled `mode="fixture"`; they are never represented as current research. No elapsed-time metric is included in fixture results.

Run `python -m unittest discover -s tests -p 'test_search.py' -v` from the repository root. The tests cover deterministic fixtures, caps, source-backed citations, live Brave request formatting through `httpx.MockTransport`, HTML extraction, key isolation, partial failures, unsafe redirects, DNS checks and pinning, response limits, model quote rejection, iterative planning, and iterator cleanup. They require no credentials or live network. Live integration with a configured search service remains a separate operational check.

The REST request shape follows the [official Brave web search reference](https://api-dashboard.search.brave.com/api-reference/web/search/get) and [authentication guide](https://api-dashboard.search.brave.com/documentation/guides/authentication).
