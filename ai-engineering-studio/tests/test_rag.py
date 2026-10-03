"""Offline behavioral tests: genuine extraction, retrieval, and labeled metrics."""
import base64
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
from pypdf import PdfWriter
from pypdf.generic import ArrayObject, DecodedStreamObject, DictionaryObject, NameObject

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))
from studio import rag


DOCUMENTS = [
    {"id": "handbook", "name": "Engineering Handbook", "pages": [
        {"page": 1, "text": "The office cafeteria serves vegetarian meals. Lunch starts at noon. Food orders close at ten."},
        {"page": 2, "text": "Security rotation: production API keys expire after ninety days. The platform team owns key rotation. Keys must never be committed to source control."},
        {"page": 3, "text": "Vacation requests require manager approval. The annual leave allowance is twenty days."},
    ]},
    {"id": "incident", "name": "Incident Procedure", "text": "An incident commander coordinates production outages. Escalations go to the on-call engineer."},
]


def _pdf_with_text(pages):
    writer = PdfWriter()
    for text in pages:
        page = writer.add_blank_page(width=612, height=792)
        font = DictionaryObject({NameObject("/Type"): NameObject("/Font"), NameObject("/Subtype"): NameObject("/Type1"), NameObject("/BaseFont"): NameObject("/Helvetica")})
        page[NameObject("/Resources")] = DictionaryObject({NameObject("/Font"): DictionaryObject({NameObject("/F1"): font})})
        stream = DecodedStreamObject()
        stream.set_data(f"BT /F1 12 Tf 30 720 Td ({text}) Tj ET".encode("ascii"))
        page[NameObject("/Contents")] = writer._add_object(stream)
    buffer = io.BytesIO()
    writer.write(buffer)
    return base64.b64encode(buffer.getvalue()).decode("ascii")


class RagTests(unittest.TestCase):
    def setUp(self):
        self.index = rag.build_index({"documents": DOCUMENTS})

    def test_hybrid_retrieval_finds_ground_truth_page(self):
        hits = rag.retrieve(self.index, "When do production API keys expire?", {"top_k": 2})
        self.assertEqual((hits[0]["document_id"], hits[0]["page"]), ("handbook", 2))
        self.assertGreater(hits[0]["bm25_score"], 0)
        self.assertGreater(hits[0]["vector_score"], 0)

    def test_lexical_and_vector_extremes_both_retrieve(self):
        for weight in (0, 1):
            hit = rag.retrieve(self.index, "API keys expire ninety days", {"top_k": 1, "lexical_weight": weight})[0]
            self.assertEqual(hit["page"], 2)

    def test_quotes_are_verbatim_and_page_citations_resolve(self):
        result = rag.run({"documents": DOCUMENTS, "query": "API keys expire", "top_k": 1})
        self.assertEqual(result["answer_mode"], "extractive")
        self.assertTrue(result["supported"])
        citation = result["citations"][0]
        self.assertEqual(citation["page"], 2)
        self.assertIn(citation["quote"], result["hits"][0]["text"])
        self.assertIn(f"[{citation['citation_id']}]", result["answer"])
        json.dumps(result, allow_nan=False)

    def test_no_evidence_abstains(self):
        result = rag.run({"documents": DOCUMENTS, "query": "interplanetary banana teleportation"})
        self.assertFalse(result["supported"])
        self.assertEqual(result["citations"], [])
        self.assertEqual(result["hits"], [])

    def test_default_extractive_mode_never_calls_model_provider(self):
        with patch.object(rag, "_provider_json", side_effect=AssertionError("model must not run")):
            result = rag.run({"index": self.index, "query": "API keys expire"})
        self.assertEqual(result["mode"], "local")
        self.assertEqual(result["answer_mode"], "extractive")

    def test_live_generation_validates_quote_and_allowlisted_citation(self):
        hit = rag.retrieve(self.index, "API keys expire", {"top_k": 1})[0]
        quote = "Security rotation: production API keys expire after ninety days."
        model_result = {"answer": f"Production API keys expire after ninety days. [{hit['citation_id']}]",
                        "citations": [{"citation_id": hit["citation_id"], "quote": quote}]}
        with patch.object(rag, "_provider_json", return_value=model_result) as model:
            result = rag.run({"index": self.index, "query": "API keys expire", "mode": "live", "top_k": 1})
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["mode"], "live")
        self.assertEqual(result["answer_mode"], "generated")
        self.assertFalse(result["factuality_guaranteed"])
        self.assertIn("not fact-checked", result["validation_scope"])
        self.assertEqual(result["citations"][0]["page"], 2)
        self.assertIn(result["citations"][0]["quote"], result["hits"][0]["text"])
        prompt, schema = model.call_args.args
        self.assertIn("untrusted data", prompt)
        self.assertEqual(schema["properties"]["citations"]["items"]["properties"]["citation_id"]["enum"], [hit["citation_id"]])
        json.dumps(result, allow_nan=False)

    def test_live_generation_rejects_unknown_ids_and_unverified_quotes(self):
        hit = rag.retrieve(self.index, "API keys expire", {"top_k": 1})[0]
        valid_quote = "Security rotation: production API keys expire after ninety days."
        invalid_results = [
            {"answer": "Invented answer. [D99-P1-C1]", "citations": [{"citation_id": "D99-P1-C1", "quote": valid_quote}]},
            {"answer": f"Invented answer. [{hit['citation_id']}]", "citations": [{"citation_id": hit["citation_id"], "quote": "Production API keys expire every ten days."}]},
            {"answer": "The answer has no citation.", "citations": [{"citation_id": hit["citation_id"], "quote": valid_quote}]},
            {"answer": f"A cited paragraph. [{hit['citation_id']}]\n\nAn uncited invented paragraph.", "citations": [{"citation_id": hit["citation_id"], "quote": valid_quote}]},
            {"answer": f"A citation mismatch. [{hit['citation_id']}] [D99-P1-C1]", "citations": [{"citation_id": hit["citation_id"], "quote": valid_quote}]},
        ]
        for model_result in invalid_results:
            with self.subTest(model_result=model_result), patch.object(rag, "_provider_json", return_value=model_result):
                result = rag.run({"index": self.index, "query": "API keys expire", "mode": "live", "top_k": 1})
            self.assertEqual(result["status"], "blocked_provider")
            self.assertEqual(result["answer_mode"], "unavailable")
            self.assertEqual(result["citations"], [])
            self.assertFalse(result["supported"])
            self.assertNotIn("Invented answer", result["answer"])
            self.assertTrue(result["hits"])

    def test_live_provider_failure_is_explicit_and_redacted(self):
        with patch.object(rag, "_provider_json", side_effect=RuntimeError("private-key-and-document-data")):
            result = rag.run({"index": self.index, "query": "API keys expire", "use_provider": True})
        self.assertEqual(result["mode"], "live")
        self.assertEqual(result["status"], "blocked_provider")
        self.assertEqual(result["answer_mode"], "unavailable")
        self.assertNotIn("private-key", json.dumps(result))
        self.assertTrue(result["hits"])

    def test_live_without_retrieved_evidence_abstains_without_model(self):
        with patch.object(rag, "_provider_json", side_effect=AssertionError("model must not run")):
            result = rag.run({"index": self.index, "query": "interplanetary banana teleportation", "mode": "live"})
        self.assertFalse(result["supported"])
        self.assertEqual(result["citations"], [])
        self.assertIn("No retrieved evidence", result["generation_skipped"])

    def test_live_option_does_not_generate_during_retrieval_evaluation(self):
        with patch.object(rag, "_provider_json", side_effect=AssertionError("model must not run")):
            result = rag.run({"action": "evaluate", "mode": "live", "index": self.index, "top_k": 1,
                              "questions": [{"query": "API keys expire", "relevant": [{"document_id": "handbook", "page": 2}]}]})
        self.assertEqual(result["mode"], "local")
        self.assertEqual(result["evaluation"]["metrics"]["mrr"], 1)

    def test_actual_pdf_extraction_keeps_physical_pages(self):
        encoded = _pdf_with_text(["Cafeteria meals are served at noon.", "Production API keys expire after ninety days."])
        result = rag.run({"documents": [{"id": "pdf", "name": "policy.pdf", "base64": encoded}], "query": "API keys expire", "top_k": 1})
        self.assertEqual(result["citations"][0]["page"], 2)
        self.assertIn("ninety days", result["answer"])

    def test_form_feed_is_explicit_text_page_boundary(self):
        result = rag.run({"text": "Coffee is available.\fMercury is the closest planet.", "query": "Mercury"})
        self.assertEqual(result["hits"][0]["page"], 2)

    def test_chunk_spans_and_ids_are_stable(self):
        text = " ".join(f"word{i}" for i in range(125))
        payload = {"documents": [{"id": "long", "text": text}], "chunk_size": 40, "chunk_overlap": 10}
        first = rag.build_index(payload)
        second = rag.build_index(payload)
        self.assertEqual(first["chunks"], second["chunks"])
        self.assertEqual(first["chunks"][1]["word_start"], 30)
        for chunk in first["chunks"]:
            self.assertEqual(chunk["text"], text[chunk["char_start"]:chunk["char_end"]])

    def test_labeled_evaluation_reports_real_metrics(self):
        result = rag.run({"action": "evaluate", "index": self.index, "top_k": 1, "questions": [
            {"query": "API keys expire", "relevant": [{"document_id": "handbook", "page": 2}]},
            {"query": "vegetarian cafeteria meals", "relevant": [{"document_id": "handbook", "page": 1}]},
        ]})
        metrics = result["evaluation"]["metrics"]
        self.assertEqual(metrics["mrr"], 1)
        self.assertEqual(metrics["recall_at_k"], 1)
        self.assertEqual(metrics["ndcg_at_k"], 1)
        self.assertEqual(metrics["precision_at_k"], 1)

    def test_evaluation_failure_is_measured_not_hidden(self):
        evaluation = rag.evaluate(self.index, {"top_k": 1, "questions": [{"query": "vegetarian meals", "relevant": [{"document_id": "handbook", "page": 2}]}]})
        self.assertEqual(evaluation["metrics"]["recall_at_k"], 0)
        self.assertEqual(evaluation["metrics"]["mrr"], 0)

    def test_overlapping_hits_do_not_inflate_page_metrics(self):
        index = rag.build_index({"text": " ".join(["rotation production keys"] * 60), "chunk_size": 40, "chunk_overlap": 20})
        evaluation = rag.evaluate(index, {"top_k": 3, "questions": [{"query": "rotation keys", "relevant": [{"document_id": "document-1", "page": 1}]}]})
        self.assertEqual(evaluation["metrics"]["recall_at_k"], 1)
        self.assertAlmostEqual(evaluation["metrics"]["precision_at_k"], 1 / 3, places=5)
        self.assertLessEqual(evaluation["metrics"]["ndcg_at_k"], 1)

    def test_invalid_ground_truth_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "does not match"):
            rag.evaluate(self.index, {"questions": [{"query": "keys", "relevant": [{"document_id": "missing"}]}]})

    def test_optional_cross_encoder_changes_order_with_actual_scores(self):
        class TestScorer:
            def predict(self, pairs, **kwargs):
                return [100.0 if "incident commander" in text.lower() else -100.0 for _, text in pairs]
        with patch.object(rag, "_load_cross_encoder", return_value=TestScorer()):
            hits = rag.retrieve(self.index, "production incident", {"reranker": "cross_encoder", "top_k": 2})
        self.assertEqual(hits[0]["document_id"], "incident")
        self.assertEqual(hits[0]["rerank_score"], 100.0)

    def test_missing_cross_encoder_fails_clearly(self):
        with patch.object(rag, "_load_cross_encoder", side_effect=ValueError("Cross-encoder model is unavailable")):
            with self.assertRaisesRegex(ValueError, "unavailable"):
                rag.retrieve(self.index, "API keys", {"reranker": "cross_encoder"})

    def test_path_upload_is_confined_to_upload_directory(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "document.txt"
            path.write_text("Production API key rotation.", encoding="utf-8")
            with patch.dict(os.environ, {"STUDIO_UPLOAD_DIR": folder}):
                index = rag.build_index({"documents": [{"path": str(path)}]})
                self.assertTrue(index["chunks"])
                with self.assertRaisesRegex(ValueError, "STUDIO_UPLOAD_DIR"):
                    rag.build_index({"documents": [{"path": str(Path(folder).parent / "outside.txt")}]})

    def test_blank_scanned_pdf_is_not_faked(self):
        writer = PdfWriter()
        writer.add_blank_page(width=612, height=792)
        stream = io.BytesIO()
        writer.write(stream)
        with self.assertRaisesRegex(ValueError, "OCR"):
            rag.build_index({"documents": [{"name": "scan.pdf", "base64": base64.b64encode(stream.getvalue()).decode()}]})

    def test_bad_input_and_limits(self):
        invalid = [
            {"documents": []}, {"text": "hello", "chunk_size": 10},
            {"text": "hello", "chunk_overlap": 180},
            {"documents": [{"id": "same", "text": "a"}, {"id": "same", "text": "b"}]},
            {"documents": [{"base64": "not base64!"}]},
            {"documents": [{"pages": [{"page": 1, "text": "a"}, {"page": 1, "text": "b"}]}]},
        ]
        for payload in invalid:
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                rag.build_index(payload)
        for options in ({"top_k": 0}, {"lexical_weight": float("nan")}, {"reranker": "magic"}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                rag.retrieve(self.index, "keys", options)

    def test_portable_index_can_be_json_round_tripped(self):
        index = json.loads(json.dumps(self.index))
        result = rag.run({"index": index, "query": "vacation approval", "top_k": 1})
        self.assertEqual(result["hits"][0]["page"], 3)


if __name__ == "__main__":
    unittest.main()
