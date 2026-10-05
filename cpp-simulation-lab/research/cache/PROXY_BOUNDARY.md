# VectorGate proxy boundary — In progress

This is an adapter design. No HTTP listener, provider integration, or deployed proxy exists in this project.

## Request flow to implement

1. Authenticate the inbound request and derive tenant and authorization scope on the server. Do not trust a client-provided cache namespace.
2. Decide cache eligibility before embedding. Bypass requests with side effects, live tools, unversioned context, secrets disallowed by retention policy, streaming, or stochastic settings requiring fresh sampling. Honor provider and HTTP cache-control requirements.
3. Canonicalize the complete conversation and request parameters. Hash system messages, tools, and decoding settings using an unambiguous serialization; include model, model revision, retrieval snapshot, and embedding revision. Populate every `Namespace` field.
4. Compute the query embedding and call `Cache::get` with server-derived metadata and time. Similarity is a candidate heuristic, not a proof of response equivalence. Before enabling semantic reuse, require a measured false-positive policy for the use case.
5. On a miss, call the provider through a bounded, authenticated adapter with cancellation, timeout, and rate-limit handling. Cache only an eligible complete successful response with a defined expiry.
6. Return the provider-compatible response and record hit/miss metrics without logging sensitive prompts. Never expose a cached response across tenant or authorization scopes.

## Required integration work

The reference cache is single-threaded and in memory. A production adapter still needs concurrency control, byte limits, persistence and encryption policy, invalidation, provider response schemas, per-tenant quotas, safe telemetry, and integration tests for changing credentials and context. HTTP freshness semantics and model-output semantic equivalence are different requirements and must be evaluated separately.

## Primary references

- [RFC 9111 — HTTP Caching](https://www.rfc-editor.org/rfc/rfc9111), including cache keys, authorization, and cache-control requirements.
- [GPTCache official documentation](https://gptcache.readthedocs.io/en/stable/usage.html) for the embedding, similarity-evaluation, storage, and eviction interfaces.
