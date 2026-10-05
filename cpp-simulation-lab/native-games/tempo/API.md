# Tempo API

All simulation types live in namespace `tempo` in `core.hpp`. The header has no external dependencies and can be included in a separate C++17 program.

## Engine and policy

```cpp
#include "core.hpp"

tempo::Engine world(42);
tempo::HeuristicPolicy blue("balanced", 42);
tempo::HeuristicPolicy red("rush", 123);

while (!world.terminal() && world.tick() < 96) {
    // Policies receive side-specific values, not the world object.
    auto a = blue.decide(world.observe(tempo::Side::Blue), 240);
    auto b = red.decide(world.observe(tempo::Side::Red), 240);
    world.step(a, b);
}
```

| Interface | Result |
| --- | --- |
| `Engine(uint32_t seed = 1)` | Create the standard map, two bases, three workers and one soldier per side |
| `Engine(uint32_t seed, const Scenario&)` | Create an explicit test/research scenario on the same terrain |
| `observe(Side) const` | Return a copied `Observation` containing only that side's permitted information |
| `step(const Decision& blue, const Decision& red)` | Validate commands, advance one tick, return accepted/rejected orders and destroyed-unit counts |
| `tick()`, `terminal()`, `winner()` | Return progress and base-destruction outcome; winner is `blue`, `red`, `draw`, or `ongoing` |
| `resources(Side)`, `score(Side)` | Match evaluation metrics; policies should obtain these from their own observation |
| `checkInvariants()` | Throw `logic_error` on invalid units, build reservations, population, resource counts, or terrain positions |
| `HeuristicPolicy(style, seed)` | Create `balanced`, `rush`, or `macro` policy; unsupported styles throw |
| `decide(const Observation&, int limit = 240)` | Return commands and deterministic budget metadata |

`Scenario` accepts `units`, `mines`, and per-side starting `resources`. `UnitSetup` contains side, kind, position, and an optional initial HP; negative HP requests the normal maximum. Invalid scenario state throws during construction.

## Commands

```cpp
auto gather = tempo::Command::order(workerId, tempo::ActionType::Gather,
                                    knownMinePosition, knownMineId);
auto first = tempo::Command::order(soldierId, tempo::ActionType::Move, {12, 9});
auto queued = tempo::Command::order(soldierId, tempo::ActionType::AttackMove,
                                    {28, 9}, -1, true);
auto train = tempo::Command::train(baseId, tempo::Kind::Soldier);
auto build = tempo::Command::construct(workerId, {6, 7});
```

Orders replace the current queue unless `append=true`. A fifth queued order is rejected. `Stop` clears a mobile unit's queue; it cannot interrupt a paid tower reservation. `Gather` requires a worker and a mine previously observed by that side. `Train` accepts workers, scouts, and soldiers, requires a base with a free slot, and reserves population capacity. `Construct` requires a worker, a visible traversable tile, and available funds. Commands referring to another side's units or invalid targets are rejected independently; valid earlier commands remain accepted.

`AttackMove` targets a coordinate, not a secret enemy unit ID. It moves toward that coordinate and engages enemies encountered within attack range. Automatic attacks cannot reach beyond the attacker's sight range.

## JSON replay

`--json` emits one object, with no diagnostic output on stdout:

```json
{
  "project": "tempo",
  "seed": 42,
  "mode": "heuristic-vs-heuristic",
  "summary": {
    "ticks": 96,
    "winner": "ongoing",
    "blueScore": 777,
    "redScore": 554,
    "deadlineMisses": 0,
    "invariantChecks": 97
  },
  "frames": []
}
```

The example abbreviates summary fields and omits frames for readability. Actual output contains at least the initial frame and the fields below.

| Frame field | Contents |
| --- | --- |
| `tick` | State tick, starting at zero |
| `resources`, `score` | Blue's spendable resources and current score |
| `own` | Blue units: `id`, `kind`, `x`, `y`, `hp`, `maxHp`, `cargo`, `busy`, `queued`, `action` |
| `visibleEnemy` | Only currently visible red units; enemy cargo and orders are omitted through zero/idle values |
| `builds` | Blue reservations: `id`, `kind`, `remaining`, `total`, `cost`, `builder`, `x`, `y` |
| `deadline` | Blue decision's `budget`, `used`, and `missed`; tick-zero usage is zero |
| `text` | Short description of the preceding update |
| `map` | Public `width` 32, `height` 18, and `blocked` tile indices |
| `visible`, `explored` | Currently visible and ever-seen tile indices |
| `intel` | Enemy snapshots: `id`, `kind`, `x`, `y`, `hp`, `lastSeen` |
| `mines` | Observed resource snapshots: `id`, `x`, `y`, `remaining`, `lastSeen` |

Tile indices are `y * 32 + x`, with `(0, 0)` at the upper left. `kind` is `base`, `worker`, `soldier`, `scout`, or `tower`. `action` is `idle`, `move`, `gather`, `attack-move`, or `build`.

The full summary additionally contains `requestedSteps`, `blueStyle`, `redStyle`, `blueResources`, `redResources`, `blueDeadlineMisses`, `redDeadlineMisses`, `acceptedBlueOrders`, and `rejectedBlueOrders`. These post-run evaluation metrics may include both sides. Frame content and policy input remain blue-only observations. Deadline metadata in frame N describes the decision that advanced state N−1 to state N; a stride can omit intermediate states.

The process exits zero on success, or two for invalid arguments or simulation errors. Errors go to stderr. `tempo-tests` exits zero with a JSON `{project, passed, checks, suites}` report when all suites pass; a failed assertion exits one and reports its explanation on stderr.
