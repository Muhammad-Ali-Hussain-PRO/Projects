"""Optional local Hugging Face causal-LM adaptation and held-out evaluation.

UNRUN in the delivered environment: torch/transformers and model weights absent.
This script never downloads weights or calls an account. Supply a local model.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import random
import re

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "customers(customer_id, name, city); orders(order_id, customer_id, total)"


def prompt_for(row):
    return f"Convert the request into one SQL template. Use named parameters for unknown values.\nSchema: {SCHEMA}\nRequest: {row['text']}\nSQL:\n"


def normalized_sql(text):
    return re.sub(r"\s+", " ", text).strip().rstrip(";").lower()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, required=True, help="Existing local Hugging Face causal-LM checkpoint directory")
    parser.add_argument("--dataset", type=Path, default=ROOT / "data/model/sql_intent_dataset.jsonl")
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/model/causal_lm")
    parser.add_argument("--epochs", type=int, default=2)
    parser.add_argument("--batch-size", type=int, default=4)
    parser.add_argument("--learning-rate", type=float, default=5e-5)
    parser.add_argument("--max-steps", type=int, default=144)
    parser.add_argument("--max-parameters", type=int, default=50_000_000)
    args = parser.parse_args()
    try:
        import torch
        import transformers
        from transformers import AutoModelForCausalLM, AutoTokenizer
    except ImportError as exc:
        raise SystemExit("Optional causal-LM training requires torch and transformers; no training was performed.") from exc
    if not args.model.is_dir():
        raise SystemExit("--model must point to existing local checkpoint weights. No weights are downloaded.")
    if min(args.epochs, args.batch_size, args.max_steps, args.max_parameters) <= 0:
        raise SystemExit("Training bounds must be positive")
    torch.manual_seed(17)
    random.seed(17)
    torch.set_num_threads(min(4, torch.get_num_threads()))
    rows = [json.loads(line) for line in args.dataset.read_text(encoding="utf-8").splitlines() if line]
    text_keys = [r["text"].lower().strip() for r in rows]
    if len(text_keys) != len(set(text_keys)):
        raise SystemExit("Duplicate normalized input texts: aborting to prevent exact-text split leakage")
    splits = {s: [r for r in rows if r["domain"] == "sql" and r["split"] == s] for s in ["train", "dev", "test"]}
    if any(not value for value in splits.values()):
        raise SystemExit("Require nonempty train, dev and test splits")
    tokenizer = AutoTokenizer.from_pretrained(str(args.model), local_files_only=True)
    model = AutoModelForCausalLM.from_pretrained(str(args.model), local_files_only=True).to("cpu")
    parameters = sum(p.numel() for p in model.parameters())
    if parameters > args.max_parameters:
        raise SystemExit(f"Checkpoint has {parameters} parameters, above explicit bound {args.max_parameters}")
    if tokenizer.pad_token_id is None:
        if tokenizer.eos_token_id is None:
            raise SystemExit("Tokenizer needs an EOS or PAD token")
        tokenizer.pad_token = tokenizer.eos_token

    def evaluate(phase):
        model.eval()
        result = {}
        for split in ["dev", "test"]:
            evidence = []
            for row in splits[split]:
                ids = tokenizer(prompt_for(row), return_tensors="pt", truncation=True, max_length=256)
                with torch.no_grad():
                    generated = model.generate(**ids, do_sample=False, max_new_tokens=128,
                                               pad_token_id=tokenizer.pad_token_id, eos_token_id=tokenizer.eos_token_id)
                completion = tokenizer.decode(generated[0, ids["input_ids"].shape[1]:], skip_special_tokens=True).strip()
                evidence.append(dict(id=row["id"], request=row["text"], expected=row["sql_template"],
                                     generated=completion, exact_match=normalized_sql(completion) == normalized_sql(row["sql_template"])))
            matches = sum(case["exact_match"] for case in evidence)
            result[split] = dict(correct=matches, total=len(evidence), exact_match=matches / len(evidence), cases=evidence)
        print(json.dumps({"phase": phase, "dev_exact_match": result["dev"]["exact_match"], "test_exact_match": result["test"]["exact_match"]}))
        return result

    baseline = evaluate("pretrained_baseline")
    encoded = []
    for row in splits["train"]:
        prefix = prompt_for(row)
        target = row["sql_template"] + (tokenizer.eos_token or "")
        ids = tokenizer(prefix + target, truncation=True, max_length=384)["input_ids"]
        prefix_length = min(len(tokenizer(prefix)["input_ids"]), len(ids))
        labels = [-100] * prefix_length + ids[prefix_length:]
        if all(label == -100 for label in labels):
            raise SystemExit("A training target was truncated; shorten prompts or raise the explicit token bound")
        encoded.append((ids, labels))
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.learning_rate)
    steps, losses = 0, []
    rng = random.Random(17)
    for _ in range(args.epochs):
        model.train()
        order = list(range(len(encoded)))
        rng.shuffle(order)
        for start in range(0, len(order), args.batch_size):
            batch = [encoded[i] for i in order[start:start + args.batch_size]]
            width = max(len(ids) for ids, _ in batch)
            ids = torch.tensor([a + [tokenizer.pad_token_id] * (width - len(a)) for a, _ in batch])
            mask = torch.tensor([[1] * len(a) + [0] * (width - len(a)) for a, _ in batch])
            labels = torch.tensor([b + [-100] * (width - len(b)) for _, b in batch])
            optimizer.zero_grad(set_to_none=True)
            loss = model(input_ids=ids, attention_mask=mask, labels=labels).loss
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()
            losses.append(float(loss.detach()))
            steps += 1
            if steps % 16 == 0:
                print(json.dumps({"training_step": steps, "loss": losses[-1]}), flush=True)
            if steps >= args.max_steps:
                break
        if steps >= args.max_steps:
            break
    adapted = evaluate("sql_finetuned")
    args.output.mkdir(parents=True, exist_ok=True)
    checkpoint = args.output / "checkpoint"
    model.save_pretrained(checkpoint, safe_serialization=True)
    tokenizer.save_pretrained(checkpoint)
    report = dict(status="trained_and_evaluated", model_source=str(args.model), parameters=parameters, training_steps=steps,
                  seed=17, train_count=len(splits["train"]), dev_count=len(splits["dev"]), test_count=len(splits["test"]),
                  dataset_sha256=hashlib.sha256(args.dataset.read_bytes()).hexdigest(),
                  baseline=baseline, adapted=adapted, training_losses=losses,
                  versions={"torch": torch.__version__, "transformers": transformers.__version__},
                  split_policy="Only train targets contribute to gradients; dev/test evaluate fixed hyperparameters, without checkpoint selection.",
                  limitations=["Exact template match is a strict string metric, not execution accuracy.", "Synthetic English dataset and two-table schema; arbitrary SQL is not covered."])
    (args.output / "evaluation.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
