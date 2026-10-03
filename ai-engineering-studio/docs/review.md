# Multi-agent code review and refactoring

`backend/studio/review.py` exposes `run(payload: dict) -> dict`. It accepts Python
source as data and never imports, executes, writes, or applies that source. All
patches and proposed tests require human review. It does not send or merge them.

## Request contract

```json
{
  "code": "def collect(items=[]):\n    return items\n",
  "filename": "submitted.py",
  "tests": "",
  "mode": "local",
  "max_revisions": 1
}
```

`code` is required and nonempty. `tests` is optional Python test source. Each is
limited to 80,000 UTF-8 bytes. `filename` is a simple `.py` name; directories are
rejected. `mode` is `local` (default) or `live`; `model` aliases `live` and
`use_model: true` also opts in. `max_revisions` is an integer from 0 to 2.
Parsing is bounded to 20,000 AST nodes, and each report returns at most 80 findings.

## Local inspection

`inspect_python(source, filename="submitted.py", tests="")` uses Python's AST to
check mutable literal defaults, duplicate module definitions, duplicate constant
dictionary keys, and bare exception handlers. The security role inspects explicit
dynamic execution, shell execution, unsafe deserialization, suspicious YAML
loaders, disabled TLS verification, dynamic query construction, and candidate
literal credentials. Credential evidence is redacted. Import aliases are
recognized, but shadowing and reassignment can produce false positives.

The test role derives targets from public functions and syntax features: branch
paths, iteration boundaries, error handling, and asynchronous cancellation. A
name referenced in supplied test source is only a reference; it is never labeled
as behavioral coverage. Neither source nor tests are executed.

The response includes `local_analysis`, three separate `roles`, a bounded `trace`,
`source_sha256`, `limitations`, `human_review_required: true`, and `applied: false`.
Local reports have `source: "local_ast"`, so they cannot be mistaken for model
reviews. Syntax errors have line/column evidence. Security results are heuristic
review findings and do not constitute a security certification.

## Optional model workflow

Live mode requires `.provider.is_configured()` and
`.provider.generate_json(prompt, schema)`. The provider is imported lazily.
Missing configuration produces `status: "blocked_provider"` while preserving
local evidence. No fabricated live results are returned.

The orchestrator requests reviewer, security, and test role outputs separately.
The role prompts frame submitted source as untrusted data and request evidence.
When actionable findings exist, a revision role proposes complete Python source
and separate test text. Both are parsed without execution. Syntax-invalid
proposals remain in `revisions` with `status: "rejected_syntax"`; they never become
the final `proposed_patch`. An accepted proposal is reviewed by all three roles
again. At most two proposals and eleven provider calls are made. Model errors
return a bounded error category rather than transport exception text.

`proposed_patch`, `proposed_code`, and `proposed_test_text` are proposals, even when
they parse successfully. The generated tests have not passed, and syntax
validation does not prove behavioral correctness or safety. The provider prompt
is not a security boundary; all output still needs human review.

## Dependencies and verification

The module uses the Python standard library. Live mode delegates network and
credential handling to the shared provider; it has no credential storage.

From the project root:

```bash
python -m unittest discover -s tests -p test_review.py -v
```

Fixtures check AST evidence, safe call variants, redacted evidence, source and
findings limits, no arbitrary execution, separate provider roles, two-revision
call bounds, malformed proposal rejection, and provider-error redaction. They do
not require a live account.
