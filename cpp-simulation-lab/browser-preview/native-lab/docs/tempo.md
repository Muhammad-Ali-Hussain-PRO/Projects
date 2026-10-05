# Tempo RTS

Tempo is a self-contained C++17 strategy simulator for studying decisions under partial observation. Two heuristic agents gather resources, reserve production capacity, scout, build defenses, and fight on a 32 × 18 map. The native executable emits an observation replay that a separate viewer can inspect.

This implementation runs locally with the C++ standard library. It is an educational RTS environment, not a connection to an external game or a trained-agent benchmark.

## Build and run

From this directory:

```sh
make
make test
../build/tempo --seed 42 --steps 96 --json
../build/tempo --seed 42 --steps 220 --style macro --opponent rush
../build/tempo --seed 42 --steps 96 --budget 28 --json
```

The shared `../Makefile` also builds this executable and its test program. Without Make, compile `main.cpp` and `test.cpp` separately with `g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic`.

| Option | Default | Behavior |
| --- | --- | --- |
| `--seed N` | `1` | Unsigned 32-bit seed controlling mine placement and scout routes |
| `--steps N` | `180` | Maximum ticks; `0` emits the initial observation; range `0..10000` |
| `--budget N` | `240` | Decision work units available to each agent each tick; range `0..1000000` |
| `--style NAME` | `balanced` | Blue policy: `balanced`, `rush`, or `macro` |
| `--opponent NAME` | `rush` | Red policy using the same three choices |
| `--frame-stride N` | `1` | Sample every N ticks, always keeping the initial and final frames |
| `--json` | Off | Emit one JSON replay on stdout |

## Implemented behavior

- **Fog of war:** each side sees its own units and only enemies within its units' sight radii. Previously seen enemies become timestamped intelligence. Exploration remains known after vision disappears. Enemy queues, cargo, resources, and production tasks are not included in the observation.
- **Queued orders:** mobile units accept up to four `move`, `gather`, or `attack-move` orders. Existing orders continue when no replacement arrives. A gather order cycles through mining and depositing until its mine is exhausted.
- **Resource economy:** workers carry at most 12 resources, harvest up to four per tick, and must return within one tile of a living base to deposit. Costs are reserved when a production or construction command is accepted.
- **Concurrent construction:** each base has two production slots. Different workers can build towers while both slots train units. All ready tasks advance during the same tick.
- **Scouting and adaptation:** the policies scout public spawn areas and alternate flanks, select known mines, react to visible attackers with towers, and vary their attack thresholds and worker targets by build style.
- **Tick budgets:** every policy charges explicit work units for inspection and planning. Reaching the budget returns useful partial commands; units continue their earlier orders. This is a deterministic planning ceiling, not a measured wall-clock deadline.
- **Combat:** attacks resolve simultaneously after movement and production. Both bases dying in the same tick produces a draw.

## Check results

`make test` emits a single JSON report. The implementation was compiled with the flags above and passed **226 checks** in six suites: observation isolation, scout memory, queues and reservations, economy and parallel construction, simultaneous combat, and deadlines and determinism. A multi-seed invariant exercise is included in those suites.

The seed-42, 96-tick sample was parsed as valid JSON with 97 observation frames, 97 runtime invariant checks, zero rejected blue orders, and zero default-budget deadline misses. Its outcome was `ongoing`; the reported scores were blue 777 and red 554. These are outcomes of this small simulator and are not comparative performance claims.

See [API.md](API.md) for interfaces and replay fields, and [TECHNICAL_NOTES.md](TECHNICAL_NOTES.md) for timing and observation semantics.
