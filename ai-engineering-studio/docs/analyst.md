# QueryLens analytics

The Python service accepts UTF-8 CSV (2MB, 10000 rows, 40 columns), infers numeric columns, creates an in-memory SQLite table `data`, and drafts aggregate queries through a constrained grammar or an optional configured model. Supply `csv`, `question`, optional `sql` and `mode` in `/api/run/analyst`.

Only one SELECT/read-only WITH statement is accepted. SQLite query-only mode and an authorizer deny writes, other tables and non-allowlisted functions. A progress callback enforces a cooperative 500ms execution deadline; output is capped at 200 rows. Query plans and chart data are returned. These bounds support inspection, not a claim of a hardened arbitrary-code sandbox. CSV values use bound insertion parameters; identifiers are quoted.

Browser previews run their own constrained aggregate planner, not SQLite. Arbitrary SQL is sent only to an explicitly connected backend. Open-ended natural-language SQL requires server-side provider configuration and remains subject to the same authorizer. No private database integration is claimed.

Run `PYTHONPATH=backend python -m unittest discover -s tests -p test_analyst.py -v`.
