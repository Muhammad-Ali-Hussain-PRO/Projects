# Tempo technical notes

## State and observation boundary

`Engine` owns the full world: units, action queues, resource stores, production reservations, terrain, and visibility. `Observation` is a copied side-specific value. `HeuristicPolicy::decide` only accepts that value and its configured budget; it cannot access the engine's hidden state.

Vision is the union of Manhattan-radius diamonds around friendly units. Terrain is public, including the central river banks and traversable bridge. The opposing starting coordinate is public map information, so a policy may scout it before seeing the opposing base. Resource nodes become known when observed; their remembered amount changes only while visible.

Enemy intelligence stores the last observed position, HP, and tick. It does not track enemies through fog. A visible old location that no longer contains the remembered enemy invalidates that old snapshot. Otherwise a sighting expires after 60 ticks. A retreating scout therefore leaves useful but stale intelligence behind. Enemy actions and build reservations are never copied into `Observation`; visible enemy records deliberately contain idle/zero queue metadata.

The JSON frames serialize this observation boundary. The post-run summary is an evaluator report, so it can contain both sides' scores and remaining resources.

## Tick order

Each nonterminal update performs these stages:

1. Independently validate both sides' submitted commands; reserve accepted costs immediately.
2. Advance persistent unit orders. Attack-move engagement uses a starting-position snapshot so one side's movement update cannot give the other side an asymmetric engagement check.
3. Advance every ready production or construction reservation, then spawn completed units.
4. Recompute visibility, accumulate damage for all attackers, and apply damage simultaneously.
5. Remove destroyed units, cancel their unfinished reservations, advance the tick, and refresh observations and intelligence.
6. Check world invariants.

A base advances both of its production slots each tick. A tower task advances only when its worker is within one tile of the construction site; travel time is separate from build time. Workers and base tasks run concurrently in simulation time; the implementation does not require background threads.

Destroying a builder cancels its unfinished tasks and refunds `reservedCost * remaining / (2 * total)` with integer truncation. Construction workers cannot be reassigned while their paid reservation is active. Population capacity counts mobile units and reserved mobile-unit production; the cap is 24 per side. Structures do not consume population.

## Unit and economy rules

| Kind | HP | Sight | Attack range | Damage/tick | Cost | Build ticks |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Base | 240 | 6 | 4 | 8 | Initial only | — |
| Worker | 35 | 4 | 1 | 2 | 40 | 8 |
| Soldier | 60 | 5 | 1 | 8 | 60 | 12 |
| Scout | 30 | 8 | 1 | 2 | 35 | 6 |
| Tower | 100 | 6 | 5 | 10 | 90 | 18 |

Mobile units move one tile per tick; scouts move up to two. A fixed-neighbor-order breadth-first search chooses a traversable shortest path. Units may share a tile; collision avoidance and line-of-sight occlusion are outside this simulator's scope. Terrain affects movement but not the vision diamond or attack line. Each attacker prioritizes soldiers/towers, then workers/scouts, then bases, with HP and ID tie breaks.

Workers harvest up to four units of a finite mine each tick, carry at most 12, and deposit within one tile of their living base. Spendable resources are not created until deposit. The score is a transparent demonstration metric:

```text
resources + deposited / 2 + enemy units destroyed * 30
          + surviving unit costs + active reservation costs
```

Integer arithmetic is used throughout. A score lead is not a victory; only base destruction ends a match. Reaching the requested step limit while both bases live returns `ongoing`.

## Planning budget and adversary

`DecisionBudget` limits explicitly charged operations. Initial setup costs eight work units; own-unit and queued-build inspection costs two per record; mine inspection costs three; worker assignment costs three; enemy inspection costs four; memory inspection costs two; production planning costs eight per slot; scout planning costs eight; and soldier planning costs seven. A failed charge sets `missed=true`, keeps `used <= budget`, and returns the commands already produced. Previously queued unit actions and accepted construction work remain active.

This makes deadline experiments reproducible across machines. It does not claim a real-time operating-system guarantee or report a wall-clock performance measurement. The engine trusts externally supplied `Decision` objects; custom controllers must enforce their own budget accounting as the bundled heuristic does.

The three adversary styles change worker targets and army attack thresholds. Rush targets four workers and attacks with three soldiers; balanced targets five workers and attacks with five; macro targets seven workers and attacks with seven. Rush delays scouting until tick 24. Visible threats override these plans with defensive construction and local attack-move orders. A surviving army may push after tick 95 even below its normal threshold.

The seed chooses one of three symmetric home-mine offsets and changes scout flank cadence. Combat itself has no random damage. Identical seed, style, budget, and command sequence produce identical states; the test suite compares replay-state digests across two independent runs.

## Verification and limits

The six test suites exercise hidden-state isolation, scouts revealing and then losing sight of an enemy base, stale memory, persistent exploration, simultaneous production, reservation costs, capped action queues, deposits, concurrent tower construction, protection of active builders, mutual lethal attacks, terminal-state behavior, useful partial decisions, zero budgets, deterministic replay, and multi-seed nonnegative economies.

Runtime checks execute at construction and after each tick. They validate unique unit/task IDs, traversable unit positions, HP and cargo bounds, queue bounds, valid live builders, valid task progress, nonnegative resources/mines, and population capacity.

The implementation is intentionally small: two sides, one base per standard side, one resource type, fixed terrain, four finite mines, and heuristic policies. It provides no online matchmaking, learned policy, network synchronization, graphical native renderer, production RTS ruleset, or measured claim about strategic strength. The replay interface is sufficient for a separate browser viewer and for controlled local policy experiments.
