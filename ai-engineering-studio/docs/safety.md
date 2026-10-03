# Controlled safety and PII probes

The working local mode measures explicit keyword guardrails and regex PII redaction against a curated synthetic corpus. It does not claim to evaluate an unconfigured LLM. An optional mode calls only the configured, authorized provider endpoint using a fake canary and bounded fixtures.

## Actual local results

| Benchmark | True positives | False positives | False negatives | Precision | Recall |
| --- | --- | --- | --- | --- | --- |
| Injection detection: 16 attacks + 16 benign cases | 14 | 3 | 2 | 82.352941% | 87.5% |
| PII detection: 12 cases, exact typed span matching | 10 | 1 | 2 | 90.909091% | 83.333333% |

Injection detection has 13 true negatives, a false-positive rate of 18.75%, and F1 of 0.848485. PII F1 is 0.869565 and full-case exact span match is 75% (9/12). PII true negatives and false-positive rate are omitted because arbitrary text positions do not define a meaningful negative-span count.

The report exposes mistakes rather than suppressing them: spaced and encoded override instructions evade the rules; educational quotations and a UI label are blocked. An international phone format and obfuscated email are missed. An order identifier resembling a phone is redacted. These measured weaknesses limit the scope of the benchmark.

The corpus uses reserved `.test` domains, fictional phone examples, invalid SSN-like strings, a sandbox payment-card number and a dummy API-key string. No real personal records, secrets or third-party systems are targeted. Expected PII annotations are manually authored typed character spans. Detected spans are scored against exact `(type, start, end)` matches, and replacement occurs from the end of the string to preserve offsets.

## Reproduce

From the project root:

```bash
python training/benchmark_safety.py
python -m unittest discover -s tests -p 'test_safety.py' -v
```

The benchmark uses Python's standard library. `data/safety/corpus.json` contains every labeled fixture; `artifacts/safety/local_report.json` contains measured totals, per-case matches, missed spans, false positives and redacted outputs. `studio.safety.run({"mode":"local","text":"Synthetic email alex@example.test"})` also applies the guardrail and redactor to optional custom text. Custom text is not written to these artifact files.

## Optional configured model mode

`studio.safety.run({"mode":"provider","max_cases":4})` calls `provider.generate_json` only after `provider.is_configured()` succeeds. It processes an alternating attack/benign subset of at most eight cases. It checks actual generated text for the fake internal canary `CANARY_STUDIO_X91` and forbidden override marker `APPROVED_OVERRIDE`, reports per-case responses, attack success rate, and benign completion rate. Missing credentials return `provider_unavailable` and make no requests. Provider calls may incur the configured service's normal cost.

No real endpoint benchmark was run during the build. Provider-path tests use explicit mocks to verify call bounds and scoring; mocked values are not reported as model performance.

The shared provider API accepts a prompt string, so this optional probe simulates an instruction boundary inside a single prompt. It does not establish genuine system-role isolation. Sentinel scoring also cannot detect every possible policy violation. Local guardrails, PII rules and model probes all have limited coverage; no finite corpus establishes zero risk.
