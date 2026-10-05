# Native simulation prototypes

Three self-contained C++17 prototypes. The native cores are primary; a browser mirror is an independently implemented preview unless explicitly connected to a compiled native artifact.

| Slug | Project | Focus |
| --- | --- | --- |
| veil | Veil Council | Six private-role heuristic agents, beliefs, coalition offers, validated external actions |
| chronicle | Chronicle Engine | Structured tabletop state, bounded map, NPC arcs, event replay, durable saves |
| tempo | Tempo RTS | Fog of war, queued orders, resource economy, concurrent builds, deadline budgets |

## Build and run

Only a C++17 compiler and GNU Make are needed for the native programs. No third-party libraries or network calls are required.

```sh
make
make test
make samples
python3 verify.py
./build/veil --seed 42 --steps 12 --json
./build/chronicle --seed 42 --steps 24 --json
./build/tempo --seed 42 --steps 96 --json
```

Each project also has its own Makefile, README, API contract, and technical notes. `make -C veil test`, for example, builds and runs only Veil's native suite. All executables are written under the shared `build/` directory.

Each JSON command emits `{project,seed,mode,summary,frames:[...]}`. Frames are compact immutable snapshots; exact schemas and private-observation boundaries are documented in each project's `API.md`. Seeds and transitions are deterministic within the documented toolchain. Fixed narration and heuristic policies are identified as such. These prototypes do not connect to LLM providers.

The reports under `build/*-tests.json` and `verification-report.json` describe tests that were actually executed. Assertion counts are not performance measurements. No throughput, latency, intelligence, calibration, WebAssembly build, or model-quality claim is inferred from them.

WebAssembly compiler tooling is unavailable in this environment. Native C++ sources and executable tests are supplied; no compiled WASM binary is claimed. The local browser preview can demonstrate mechanics independently while remaining clearly labeled.
