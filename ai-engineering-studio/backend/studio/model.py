"""Inference and honest evaluation for the actually trained SQL intent baseline."""
from __future__ import annotations

from collections import Counter
from functools import lru_cache
import json
from pathlib import Path
import re

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
ARTIFACTS = ROOT / "artifacts/model"
TOKEN_RE = re.compile(r"\b\w\w+\b", re.UNICODE)


@lru_cache(maxsize=1)
def load_model():
    path = ARTIFACTS / "adapted_model.json"
    if not path.exists():
        raise RuntimeError("Train the local model first: python training/train_sql_intent.py")
    result = json.loads(path.read_text(encoding="utf-8"))
    result["coef"] = np.asarray(result["coef"], dtype=np.float64)
    result["intercept"] = np.asarray(result["intercept"], dtype=np.float64)
    result["idf"] = np.asarray(result["idf"], dtype=np.float64)
    return result


def vectorize(text: str, model: dict):
    words = TOKEN_RE.findall(text.lower())
    features = words + [" ".join(pair) for pair in zip(words, words[1:])]
    vector = np.zeros(len(model["vocabulary"]), dtype=np.float64)
    matched = 0
    for term, count in Counter(features).items():
        index = model["vocabulary"].get(term)
        if index is not None:
            vector[index] = (1 + np.log(count)) * model["idf"][index]
            matched += 1
    norm = np.linalg.norm(vector)
    if norm:
        vector /= norm
    return vector, matched


def predict(text: str):
    model = load_model()
    vector, matched = vectorize(text, model)
    scores = model["coef"] @ vector + model["intercept"]
    probs = 1 / (1 + np.exp(-np.clip(scores, -700, 700)))
    probs /= probs.sum()
    ranked = sorted(zip(model["classes"], probs.tolist()), key=lambda pair: pair[1], reverse=True)
    return dict(intent=ranked[0][0], confidence=ranked[0][1], matched_features=matched,
                ranking=[dict(intent=label, probability=probability) for label, probability in ranked])


def run(payload: dict) -> dict:
    text = payload.get("request", payload.get("prompt", payload.get("text", payload.get("query", "count all customers"))))
    if not isinstance(text, str) or not text.strip():
        raise ValueError("request must be a non-empty string")
    if len(text) > 4000:
        raise ValueError("request is limited to 4000 characters")
    result = predict(text)
    model = load_model()
    unknown = result["matched_features"] == 0
    low_confidence = result["confidence"] < 0.4
    report = json.loads((ARTIFACTS / "evaluation.json").read_text(encoding="utf-8"))
    baseline_metrics = {name: values["test"] for name, values in report["metrics"].items()}
    return {
        "status": "needs_review" if unknown or low_confidence else "ok",
        "mode": "local_trained_classifier",
        "request": text,
        "model": "SQL intent domain adaptation baseline",
        "model_type": "TF-IDF + SGD logistic classifier; eight fixed SQL templates",
        "training_status": "trained_and_evaluated",
        "causal_lm_status": "unrun_optional_entrypoint",
        "prediction": result,
        "intent": None if unknown else result["intent"],
        "sql_template": None if unknown else model["sql_templates"][result["intent"]],
        "parameters": re.findall(r":(\w+)", model["sql_templates"][result["intent"]]) if not unknown else [],
        "executed": False,
        "review_required": True,
        "notes": model["limitations"] + ["Classifier confidence is not calibrated. Bind and validate all parameters before use.", "The optional Hugging Face causal-LM script has not been run; this environment has no torch or transformers."],
        "evaluation": {"split_counts": report["split_counts"], "labels": report["labels"], "test_baselines": baseline_metrics,
                       "split_policy": report["split_policy"], "dataset_sha256": report["dataset_sha256"]},
        "artifacts": [
            {"label": "Curated dataset", "path": "data/model/sql_intent_dataset.jsonl"},
            {"label": "Adapted model", "path": "artifacts/model/adapted_model.json"},
            {"label": "Weights", "path": "artifacts/model/adapted_weights.npz"},
            {"label": "Held-out evaluation", "path": "artifacts/model/evaluation.json"},
            {"label": "Case predictions", "path": "artifacts/model/held_out_predictions.csv"},
            {"label": "Reproduce training", "path": "training/train_sql_intent.py"},
            {"label": "Optional causal-LM training (unrun)", "path": "training/finetune_causal_lm.py"},
        ],
    }
