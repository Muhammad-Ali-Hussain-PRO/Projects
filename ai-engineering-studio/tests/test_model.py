import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "backend"))
from studio import model


class ModelTests(unittest.TestCase):
    def test_split_uniqueness_and_balanced_counts(self):
        rows = [json.loads(line) for line in (ROOT / "data/model/sql_intent_dataset.jsonl").read_text().splitlines()]
        texts = [r["text"].strip().lower() for r in rows]
        self.assertEqual(len(texts), len(set(texts)))
        self.assertEqual(len({r["id"] for r in rows}), len(rows))
        expected = {"pretrain": 256, "train": 288, "dev": 96, "test": 128}
        for split, count in expected.items():
            self.assertEqual(sum(r["split"] == split for r in rows), count)
            for label in model.load_model()["classes"]:
                self.assertEqual(sum(r["split"] == split and r["intent"] == label for r in rows), count // 8)

    def test_runtime_inference_reproduces_all_held_out_predictions(self):
        report = json.loads((ROOT / "artifacts/model/evaluation.json").read_text())
        correct = 0
        for case in report["held_out_cases"]:
            prediction = model.predict(case["text"])
            self.assertEqual(prediction["intent"], case["predicted"])
            self.assertAlmostEqual(prediction["confidence"], case["confidence"], places=12)
            correct += prediction["intent"] == case["expected"]
        self.assertEqual(correct, report["metrics"]["sql_adapted"]["test"]["correct"])
        self.assertEqual(correct, 107)

    def test_returns_template_without_executing(self):
        result = model.run({"request": "count all customers"})
        self.assertEqual(result["intent"], "count")
        self.assertEqual(result["sql_template"], "SELECT COUNT(*) AS customer_count FROM customers;")
        self.assertFalse(result["executed"])
        self.assertTrue(result["review_required"])
        self.assertEqual(result["training_status"], "trained_and_evaluated")
        self.assertEqual(result["causal_lm_status"], "unrun_optional_entrypoint")

    def test_out_of_vocabulary_abstains(self):
        result = model.run({"request": "zzxypl qqqwwwrrr"})
        self.assertEqual(result["status"], "needs_review")
        self.assertIsNone(result["intent"])
        self.assertIsNone(result["sql_template"])

    def test_invalid_requests(self):
        for invalid in ["", None, 42, "a" * 4001]:
            with self.assertRaises(ValueError):
                model.run({"request": invalid})


if __name__ == "__main__":
    unittest.main()
