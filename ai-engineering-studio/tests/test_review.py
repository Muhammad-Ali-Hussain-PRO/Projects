"""Correctness tests for bounded AST inspection and model orchestration."""

import json
import sys
import types
import unittest
from unittest.mock import patch

from backend.studio import review


class LocalReviewTests(unittest.TestCase):
    def categories(self, code):
        return {item["category"] for item in review.inspect_python(code)["findings"]}

    def test_mutable_defaults_duplicate_definitions_and_keys(self):
        source = "def collect(items=[]):\n    return items\ndef collect(items={}):\n    return items\nvalues = {'same': 1, 'same': 2}\n"
        self.assertTrue({"mutable_default", "duplicate_definition", "duplicate_dictionary_key"} <= self.categories(source))

    def test_aliases_and_security_evidence(self):
        source = "import subprocess as sp\nfrom pickle import loads as deserialize\nimport requests as r\nsp.run(['echo', data], shell=True)\ndeserialize(data)\nr.get(url, verify=False)\n"
        result = review.inspect_python(source)
        found = {item["category"]: item for item in result["findings"]}
        self.assertEqual(found["shell_execution"]["line"], 4)
        self.assertEqual(found["unsafe_deserialization"]["line"], 5)
        self.assertIn("tls_verification_disabled", found)
        self.assertEqual(found["shell_execution"]["origin"], "ast")

    def test_safe_variants_do_not_trigger_risky_call_findings(self):
        source = "import subprocess\nimport yaml\nimport requests\nsubprocess.run(['echo', data], shell=False)\nyaml.load(data, Loader=yaml.SafeLoader)\nrequests.get(url, verify=True)\ncursor.execute('select * from t where id = ?', (value,))\n"
        categories = self.categories(source)
        self.assertTrue({"shell_execution", "yaml_loader_review", "tls_verification_disabled", "dynamic_query_candidate"}.isdisjoint(categories))

    def test_dynamic_query_is_candidate_and_secret_evidence_is_redacted(self):
        source = 'api_key = "sensitive-value-fixture"\ncursor.execute(f"SELECT * FROM t WHERE id={external}")\n'
        result = review.inspect_python(source)
        secret = next(item for item in result["findings"] if item["category"] == "literal_secret_candidate")
        self.assertNotIn("sensitive-value-fixture", json.dumps(secret))
        query = next(item for item in result["findings"] if item["category"] == "dynamic_query_candidate")
        self.assertIn("inspect whether", query["message"])

    def test_syntax_invalid_reports_location(self):
        result = review.inspect_python("def broken(:\n  pass")
        self.assertFalse(result["syntax_valid"])
        self.assertEqual(result["syntax_error"]["line"], 1)

    def test_test_references_are_not_claimed_as_coverage(self):
        code = "def choose(values):\n    if values:\n        return [v for v in values]\n    return []\n"
        result = review.inspect_python(code, tests="def test_choose():\n    assert module.choose([]) == []\n")
        target = result["test_plan"][0]
        self.assertTrue(target["name_referenced_in_supplied_tests"])
        self.assertTrue(any("conditional" in case for case in target["cases"]))
        self.assertTrue(any("iteration" in case for case in target["cases"]))
        self.assertTrue(any("not a measurement" in limit for limit in result["limitations"]))

    def test_submitted_source_never_runs(self):
        code = "raise RuntimeError('would fail if executed')\nimport pathlib\npathlib.Path('/tmp/never_create_review_test').write_text('bad')\n"
        with patch("builtins.exec", side_effect=AssertionError("submitted source executed")):
            result = review.run({"code": code})
        self.assertEqual(result["status"], "complete")
        self.assertFalse(result["submitted_code_executed"])
        self.assertFalse(result["applied"])

    def test_input_limits_and_path_rejection(self):
        for payload in ({"code": "x=1", "filename": "../data.py"}, {"code": "x" * (review.MAX_SOURCE_BYTES + 1)}, {"code": "x=1", "max_revisions": 3}, {"code": "x=1", "max_revisions": True}, {"code": "x=1", "mode": "unknown"}, None):
            with self.subTest(payload_type=type(payload).__name__):
                self.assertEqual(review.run(payload)["status"], "invalid_input")

    def test_findings_are_bounded(self):
        source = "\n".join(f"eval({i})" for i in range(review.MAX_FINDINGS + 20))
        result = review.inspect_python(source)
        self.assertEqual(len(result["findings"]), review.MAX_FINDINGS)
        self.assertTrue(any("truncated" in item for item in result["limitations"]))

    def test_ast_nodes_are_bounded_before_checks(self):
        result = review.inspect_python("x\n" * 7000)
        self.assertFalse(result["syntax_valid"])
        self.assertIn("AST exceeds", result["syntax_error"]["message"])

    def test_diff_has_valid_no_newline_markers(self):
        diff = review.proposed_diff("x = 1", "x = 2", "source.py")
        self.assertIn("-x = 1\n\\ No newline at end of file\n", diff)
        self.assertIn("+x = 2\n\\ No newline at end of file\n", diff)


class ModelReviewTests(unittest.TestCase):
    def provider(self, configured=True, responder=None):
        provider = types.ModuleType("backend.studio.provider")
        provider.is_configured = lambda: configured
        provider.prompts = []

        def generate(prompt, schema):
            provider.prompts.append((prompt, schema))
            if responder:
                return responder(prompt, schema)
            return {"summary": "Deterministic fixture", "findings": [], "suggested_tests": []}

        provider.generate_json = generate
        return provider

    def test_unconfigured_live_is_blocked_and_local_results_survive(self):
        provider = self.provider(configured=False)
        with patch.dict(sys.modules, {"backend.studio.provider": provider}):
            result = review.run({"code": "eval(user_input)", "mode": "live"})
        self.assertEqual(result["status"], "blocked_provider")
        self.assertEqual(provider.prompts, [])
        self.assertIn("dynamic_execution", {item["category"] for item in result["local_analysis"]["findings"]})

    def test_live_roles_are_separate_and_model_alias_works(self):
        provider = self.provider()
        with patch.dict(sys.modules, {"backend.studio.provider": provider}):
            result = review.run({"code": "x = 1", "mode": "model", "max_revisions": 0})
        self.assertEqual(result["provider_calls"], 3)
        self.assertEqual([role["role"] for role in result["roles"]], list(review.ROLES))
        self.assertTrue(all(role["source"] == "model" for role in result["roles"]))
        self.assertTrue(all(f"the {role} role" in provider.prompts[index][0] for index, role in enumerate(review.ROLES)))

    def test_revision_loop_is_bounded_and_returns_human_proposals(self):
        revision_count = [0]

        def responder(prompt, schema):
            if schema is review.REVISION_SCHEMA:
                revision_count[0] += 1
                return {"summary": "Replace unsafe default", "revised_code": f"def collect(items=None):\n    return items or [{revision_count[0]}]\n", "proposed_test_text": "def test_collect():\n    assert collect([]) == []\n"}
            return {"summary": "Fixture finding", "findings": [{"category": "behavior", "severity": "medium", "line": 1, "message": "Inspect behavior", "recommendation": "Keep public contract"}], "suggested_tests": ["Test empty input"]}

        provider = self.provider(responder=responder)
        with patch.dict(sys.modules, {"backend.studio.provider": provider}):
            result = review.run({"code": "def collect(items=[]):\n    return items\n", "mode": "live", "max_revisions": 2})
        self.assertEqual(result["provider_calls"], review.MAX_MODEL_CALLS)
        self.assertEqual(len(result["revisions"]), 2)
        self.assertIn("--- a/submitted.py", result["proposed_patch"])
        self.assertTrue(result["human_review_required"])
        self.assertFalse(result["applied"])
        self.assertTrue(all(not item["executed"] for item in result["revisions"]))
        json.dumps(result)

    def test_invalid_generated_source_is_rejected(self):
        def responder(prompt, schema):
            if schema is review.REVISION_SCHEMA:
                return {"summary": "Broken fixture", "revised_code": "def broken(:", "proposed_test_text": "assert True"}
            return {"summary": "Review fixture", "findings": [{"category": "risk", "severity": "high", "line": 1, "message": "risk", "recommendation": "fix"}], "suggested_tests": []}

        provider = self.provider(responder=responder)
        with patch.dict(sys.modules, {"backend.studio.provider": provider}):
            result = review.run({"code": "eval(data)", "mode": "live"})
        self.assertEqual(result["status"], "revision_rejected")
        self.assertEqual(result["revisions"][0]["status"], "rejected_syntax")
        self.assertEqual(result["proposed_patch"], "")
        self.assertEqual(result["proposed_code"], "")

    def test_invalid_generated_test_source_is_rejected(self):
        def responder(prompt, schema):
            if schema is review.REVISION_SCHEMA:
                return {"summary": "Broken tests", "revised_code": "x = 1", "proposed_test_text": "def test_a(:"}
            return {"summary": "Review fixture", "findings": [], "suggested_tests": []}

        provider = self.provider(responder=responder)
        with patch.dict(sys.modules, {"backend.studio.provider": provider}):
            result = review.run({"code": "eval(data)", "mode": "live"})
        self.assertEqual(result["status"], "revision_rejected")
        self.assertFalse(result["revisions"][0]["tests_syntax_valid"])

    def test_provider_failure_is_bounded_and_never_returns_raw_exception(self):
        def fail(prompt, schema):
            raise RuntimeError("fixture-sensitive-transport-data")

        provider = self.provider(responder=fail)
        with patch.dict(sys.modules, {"backend.studio.provider": provider}):
            result = review.run({"code": "x = 1", "mode": "live"})
        self.assertEqual(result["status"], "provider_error")
        self.assertNotIn("fixture-sensitive-transport-data", json.dumps(result))
        self.assertEqual(result["provider_calls"], 1)

    def test_provider_findings_outside_source_rejected(self):
        provider = self.provider(responder=lambda prompt, schema: {"summary": "Fixture", "findings": [{"category": "risk", "severity": "medium", "line": 999, "message": "risk", "recommendation": "fix"}], "suggested_tests": []})
        with patch.dict(sys.modules, {"backend.studio.provider": provider}):
            result = review.run({"code": "x = 1", "mode": "live"})
        self.assertEqual(result["status"], "provider_error")


if __name__ == "__main__":
    unittest.main()
