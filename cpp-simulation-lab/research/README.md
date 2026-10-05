# Research foundations

**Status: In progress for all five projects.**

Five small C++17 reference implementations establish tested starting points for future research work. They are not complete model-training, alignment, serving or proxy systems. Each directory contains real source code, numerical or deterministic tests, a Makefile, a generated verification report, remaining milestones and primary references.

| Project | Implemented foundation | Remaining system work |
| --- | --- | --- |
| Transformer Forge | RoPE, SwiGLU, causal grouped-query attention, residual forward block | Full model, training, checkpoint compatibility, kernels |
| ProcessLens PRM | Exact structured arithmetic step validation | Dataset, learned step scorer, general reasoning evaluation |
| GroupAlign GRPO | Group normalization and clipped objective | Policy rollouts, gradients, optimizer, alignment evaluation |
| SpecServe | Exact rejection/correction sampling rule on toy distributions | Real models, batched verification, KV rollback, serving measurements |
| VectorGate Cache | Cosine lookup, exact metadata isolation, LRU and expiry | Authenticated proxy adapter, concurrency, byte quotas, quality calibration |

Run all reference checks:

```sh
make test
```

`catalog.json` describes implemented behavior and next milestones for display in a research lab. `verification.json` records the fixture scope and aggregate result. Each project generates its own `verification.json` from a successful test run. The projects share `test_support.hpp`. No external dependencies are required beyond Make and a C++17 compiler.
