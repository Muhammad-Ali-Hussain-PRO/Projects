# SQL intent domain adaptation

This project actually trained a small eight-class language classifier on CPU. It first learns generic record-operation intents, copies those learned weights, then continues training on SQL-domain requests. It maps the predicted intent to an explicit, parameterized SQL template for `customers` and `orders`. It does not execute queries, fill unknown values, produce arbitrary SQL, or represent a fine-tuned frontier LLM.

## Measured held-out results

| Model | Test correct / total | Test accuracy | Test macro-F1 | Development accuracy |
| --- | --- | --- | --- | --- |
| Generic pretrained baseline | 85 / 128 | 66.40625% | 0.657078 | 84.375% |
| SQL trained from scratch | 106 / 128 | 82.8125% | 0.820404 | 83.333333% |
| Generic → SQL adapted weights | 107 / 128 | 83.59375% | 0.830910 | 92.708333% |

The adapted model improves substantially over the generic baseline and only one test case over training from scratch. This small experiment does not establish a general transfer-learning advantage. Classification confidence is not calibrated. Intent accuracy is not executable SQL correctness.

The labels are `select`, `filter`, `count`, `group`, `join`, `insert`, `update`, and `delete`. The fixed schema has `customers(customer_id, name, city)` and `orders(order_id, customer_id, total)`.

## Dataset and method

`data/model/sql_intent_dataset.jsonl` contains 768 manually authored, synthetic English inputs: 256 generic pretraining, 288 SQL training, 96 development, and 128 test examples. Each split is balanced across the eight labels. Training contains 96 semantic stems with three discourse prefixes; development contains 48 distinct stems with two prefixes, and test contains 64 distinct stems with two prefixes. Prefix variants are correlated examples and should not be interpreted as independent user requests. Task concepts intentionally overlap between splits.

Exact lowercased, stripped input texts and IDs are unique across the full corpus. SQL stems were assigned to their splits before fitting. TF-IDF vocabulary and IDF are fit only on generic pretraining plus SQL training texts. Neither development nor test text contributes to features, gradients, hyperparameter tuning or checkpoint selection. Held-out records share the fixed schema and label set; this is paraphrase generalization, not an unseen-schema benchmark.

The model has 791 unigram/bigram features and eight one-vs-rest logistic classifiers using `SGDClassifier(loss="log_loss")`. Generic pretraining runs 60 epochs, domain adaptation 45 epochs, and the from-scratch baseline 45 epochs. The actual base coefficient arrays, intercepts and optimizer step count are copied before `partial_fit` on the target corpus. Fixed seeds are 17 and 23. All values reported above come from the recorded execution, without subsequent tuning.

Dataset SHA-256: `95d2318340c18ebf1f5625d54598dc829766c180abd6568ff9529d2b0f2fac31`.

## Reproduce and inspect

From the project root:

```bash
python training/train_sql_intent.py
python -m unittest discover -s tests -p 'test_model.py' -v
```

Actual training environment: Python 3.12.14, NumPy 2.3.5, scikit-learn 1.8.0. Inference needs only NumPy and the exported JSON model. No model download, credentials or GPU is required.

Outputs:

- `artifacts/model/base_model.json`: actual pretrained generic weights.
- `artifacts/model/adapted_model.json`: vocabulary, IDF, actual adapted coefficients, schema and SQL templates.
- `artifacts/model/adapted_weights.npz`: compressed numeric arrays.
- `artifacts/model/evaluation.json`: all three baselines, confusion matrices and 128 held-out case predictions.
- `artifacts/model/held_out_predictions.csv`: downloadable case evidence.
- `data/model/sql_intent_dataset.jsonl` and `data/model/schema.sql`: full curated inputs and schema.

`studio.model.run({"request":"count all customers"})` loads the trained artifact and returns a prediction, SQL template, measured baselines and artifact references. Inputs with no known features produce no SQL; all templates require review and bound parameters, including mutating templates.

## Optional causal-LM fine-tuning entrypoint — unrun

`training/finetune_causal_lm.py` is a separate genuine supervised causal-language-model training path for an existing local Hugging Face model. It loads a local checkpoint with network downloads disabled, measures the pretrained baseline on development and test sets, updates weights using only the SQL training split, saves a checkpoint, and measures held-out template exact match after adaptation. Target tokens contribute to loss; prompt and padding tokens are masked. Fixed hyperparameters and a bounded CPU training loop avoid selecting a checkpoint on test data.

It has **not been run** in this build: `torch`, `transformers`, and a local causal-LM checkpoint were unavailable. The dependency failure was checked; no causal-LM metrics or checkpoint are claimed. `artifacts/model/causal_lm_status.json` records this state. The working UI uses the actually trained classifier above.

After making `torch`, `transformers`, their checkpoint-compatible dependencies, and a local model available:

```bash
python training/finetune_causal_lm.py --model /absolute/path/to/local-causal-lm --output artifacts/model/causal_lm
```

Defaults: CPU, at most 50 million parameters, two epochs, 144 steps, batch size four, 384 input tokens and 128 generated tokens. The script creates `checkpoint/` and `evaluation.json` only following actual successful training. Exact template match is a strict string metric, not database execution accuracy.
