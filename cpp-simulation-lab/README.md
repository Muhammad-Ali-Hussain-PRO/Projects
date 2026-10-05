# C++ Simulation and Research Lab

Five C++17 native prototypes and five research foundations in progress. AI-assisted implementations, runnable locally with g++17 support.

## Run

- Games: `make -C native-games test`; `python3 native-games/verify.py`
- World: `make -C native-world test`
- RL: `make -C native-rl test`; `make -C native-rl parity`
- Research: `make -C research test`
- Browser tests: `npm install` then `npm run test:browser`

To try the included browser preview, run `python3 -m http.server 8080 --directory browser-preview` and open `http://localhost:8080/native-lab/`. It is a JavaScript preview, with native C++ recorded runs available in the mode selector.

Each folder documents its commands, output schema, verification and operating limits. Browser previews are JavaScript implementations; the native replay mode displays recorded JSON from the real C++ executables. WindRunner uses exported learned weights and a physics mirror verified against native trajectories. No hosted C++ execution or WebAssembly binary is claimed.

## Explore

[Public lab](https://muhammad-ali-hussain-engineering.muhammadalifarhanhus.chatgpt.site/native-lab/)

| Prototype | Scope |
| --- | --- |
| Veil Council | Hidden roles, beliefs, voting, private external-agent boundary |
| AetherWorld | Bounded prompt grammar, procedural geometry, rigid-body and water rules |
| WindRunner RL | C++ PPO with GAE and Adam in a continuous-control toy environment |
| Chronicle Engine | Deterministic RPG rules and durable event replay |
| Tempo RTS | Fog of war, build queues, resource planning and deterministic work budget |

## Research in progress

Transformer Forge, ProcessLens PRM, GroupAlign GRPO, SpecServe and VectorGate Cache include tested foundations. See research/catalog.json for implemented work, limitations and next milestones. They are not completed training/serving systems.
