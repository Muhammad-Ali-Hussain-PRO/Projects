# Document RAG module

`backend/studio/rag.py` performs actual PDF/text extraction, page-aware overlapping word chunking, BM25 lexical retrieval, cosine vector retrieval, weighted reciprocal-rank fusion, optional local cross-encoder reranking, and labeled retrieval evaluation. The baseline works offline without an AI API key. Its normalized 1,024-dimension hashed word/bigram vectors are lexical feature vectors, **not semantic embeddings**. `embedding_backend: "sentence_transformers"` enables real learned semantic embeddings when the optional dependency and model are installed.

Default answers are extractive: up to three exact source passages with document/page/chunk citations. It abstains when the default offline retriever has no matching content words. Citations include a `quote` that is an exact substring of the retrieved chunk. A related excerpt is evidence to inspect, not proof that every possible interpretation of the question is answered. Optional live synthesis is explicitly labeled as generated and does not claim guaranteed factuality.

## Main contract

All entrypoints are synchronous and return JSON-serializable dictionaries (or a list of dictionaries for `retrieve`). Invalid inputs raise `ValueError` for the API layer to map to a client error.

```python
from studio.rag import run

result = run({
    "documents": [{
        "id": "handbook", "name": "Engineering Handbook",
        "pages": [
            {"page": 1, "text": "Lunch begins at noon."},
            {"page": 2, "text": "Production API keys expire after ninety days."}
        ]
    }],
    "query": "When do API keys expire?",
    "top_k": 5,
    "lexical_weight": 0.55,
    "reranker": "none"
})
```

Each document accepts `text`, `pages`, `base64`/`content_base64`, or `path`. Use `name`/`filename` for uploaded files; a PDF header also selects PDF extraction. Text must be UTF-8. Form-feed characters in text establish explicit page boundaries. PDF citations use physical 1-based pages. Scanned/image-only pages require upstream OCR; the module reports empty pages instead of inventing extracted text.

Path uploads are confined to the operator-configured `STUDIO_UPLOAD_DIR` (default `uploads` relative to the process working directory). Clients should normally upload file bytes through `base64`; arbitrary filesystem reads are refused. No indexes or source documents are implicitly persisted.

`action: "index"` returns a portable JSON index, which may be supplied as `index` in future calls. `build_index(payload)`, `retrieve(index, query, options)`, and `evaluate(index, payload)` are also public. Indexes retain exact text and source offsets. `action: "query"` is the default; `action: "evaluate"` runs labeled evaluation without generating an answer.

## Optional live answer synthesis

Set `mode: "live"` on a query to call `.provider.generate_json(prompt, schema)` using the configured server-side model provider. `use_provider: true` selects live mode when `mode` is omitted. Retrieval continues to run locally; `mode: "local"` remains the default and does not call a model. Index construction and retrieval evaluation always stay local.

The generated-output schema requires an answer and citation objects whose IDs are restricted to retrieved hits included in the prompt. Context is bounded to 30,000 source characters, with at most 4,000 characters per hit. Each returned quote must be a literal substring of its matching retrieved chunk, every reference in the answer must resolve to a returned verified citation, and every answer paragraph must contain a citation. Unknown IDs, invented or paraphrased quotes, missing references, and uncited paragraphs reject the generated output. Citations are enriched with the actual document, page, and chunk metadata after validation.

Successful live responses have `answer_mode: "generated"`, `factuality_guaranteed: false`, and an explicit `validation_scope`. Citation and quote validation **does not prove that a generated claim follows from the quoted evidence**, and the answer is not fact-checked by this module. `supported` indicates available verified source citations. Review the cited passages for substantive correctness.

A provider outage or failed source validation returns `status: "blocked_provider"`, `answer_mode: "unavailable"`, and a sanitized `provider_error`, while preserving retrieved hits for inspection. It does not silently substitute a local answer as successful generation. If retrieval finds no evidence, generation is skipped and the module abstains without making a model request. The model provider controls its own request timeout and credentials; no keys are returned to clients.

## Evaluation

Supply genuine ground-truth labels; these metrics are not model judgments or a made-up quality score. Labels can be a `chunk_id`, a `document_id`, or a `document_id` plus `page`.

```python
report = run({
    "action": "evaluate", "index": result_index, "top_k": 3,
    "questions": [{
        "query": "When do API keys expire?",
        "relevant": [{"document_id": "handbook", "page": 2}]
    }]
})
```

The report includes recall@k, precision@k, mean reciprocal rank, nDCG@k, hit rate, and retrieval latency for every query and in aggregate. Precision divides distinct matched labels by requested k, including empty result positions. Each label contributes at most one gain, preventing overlapping chunks from inflating page-level results. Labels must resolve to indexed content; invalid gold labels produce an error. These scores evaluate retrieval only, not generated-answer factuality. Document/page labels should not overlap for the same question; chunk labels are the most precise evaluation unit.

## Optional models and operating bounds

Install `sentence-transformers` separately to use `embedding_backend: "sentence_transformers"` or `reranker: "cross_encoder"`. Configure models through `RAG_EMBEDDING_MODEL` and `RAG_CROSS_ENCODER_MODEL`. Defaults are `sentence-transformers/all-MiniLM-L6-v2` and `cross-encoder/ms-marco-MiniLM-L6-v2`. Models use `local_files_only=True` and `trust_remote_code=False`; an operator can explicitly enable downloads with `RAG_ALLOW_MODEL_DOWNLOAD=1`. Missing dependencies or cached models fail clearly and do not silently pretend to rerank. No credentials are stored in this module.

Bounds: 30 documents, 300 pages per document, 15 MiB per file, 1,000,000 extracted corpus characters, 5,000 chunks, 4,000 query characters, 30 final hits, 100 reranking candidates, and 50 evaluation questions. Chunk size is 40–1,000 words and overlap is at most half the chunk size. Default models use a fixed operator-selected name, not a user-supplied remote-code model. The portable JSON index is a data interchange format, not a multi-user persistent vector database.

Dependencies: `numpy`, `pypdf`; optionally `sentence-transformers`. Tests use standard-library `unittest` and actual small PDFs constructed with `pypdf`; mocked cross-encoder scores are explicitly a test fixture.

```bash
python -m unittest discover -s tests -p 'test_rag.py' -v
```

Implementation references: [pypdf text extraction](https://pypdf.readthedocs.io/en/stable/user/extract-text.html), [Sentence Transformer API](https://www.sbert.net/docs/package_reference/sentence_transformer/model.html), [Cross Encoder API](https://www.sbert.net/docs/package_reference/cross_encoder/model.html).
