# Chronicle Engine

A native C++17 text RPG with a small, persistent world and two NPC story arcs. Chronicle keeps location, health, gold, inventory, discoveries, quest ownership, and random state in a structured simulation. A deterministic rule layer decides every outcome. A separate template narrator describes the result.

The playable loop is complete: gather a remedy for Mira, earn Orin's trust, clear the ruined watchtower, recover and return the archive relic, buy a brass key, then explore and trade cavern ore. Six connected locations, an eight-slot inventory, bounded health and gold, and explicit NPC state prevent the narration from inventing game facts.

## Build and play

From `expansion/native-games`:

```sh
make build/chronicle build/chronicle-tests
./build/chronicle --seed 42 --steps 48
./build/chronicle --interactive --seed 42
./build/chronicle-tests
```

From this project directory, `make` and `make test` use the same compiler flags and shared `../build` directory. Only a C++17 compiler and the standard library are required. Linux builds use POSIX file syncing for durable saves.

Interactive examples:

```text
move woods
gather herb
talk orin
move village
talk mira
move woods
move ruins
explore
explore
save my-chronicle.save
quit
```

Use `inspect` to see the current location. `rest` in the village or shrine restores full health for free. Elsewhere, guarded camping costs two coins and restores up to six health. The interactive `save PATH` command writes the current chronicle without advancing a turn.

## Save, resume, and replay

```sh
./build/chronicle --seed 42 --steps 400 --save build/run.save --json
./build/chronicle --load build/run.save --steps 50 --json
./build/chronicle --replay build/run.save --json
```

Saves include the seed, full state, RNG state, and accepted and rejected semantic commands. A version tag, corruption checksum, and verified replay protect consistency. Linux saves use a unique temporary file, file `fsync`, atomic rename, and directory `fsync`. Loading refuses unsupported versions, corrupted data, invalid states, broken event chains, and states that cannot be reproduced from their event log.

## JSON demonstration

```sh
./build/chronicle --seed 42 --steps 300 --json
```

The top-level object contains `project`, `seed`, `mode`, `summary`, and `frames`. Each frame contains `turn`, `location`, `hp`, `gold`, `inventory`, `npcArc`, `objective`, and `text`. `--steps` counts attempted actions; rejected actions do not advance the game turn. The initial frame plus at most 100 sampled frames keeps output compact and always includes the final state. The pilot completes both NPC arcs before continuing exploration, and occasionally attempts an invalid move to demonstrate rejection.

The test program emits one JSON result. It covers eight seeds with 600 actions each, requiring at least 300 committed turns in every run, as well as deterministic replay, resumed continuation, corruption and version checks, atomic rejection, inventory bounds, and completed quest ownership.

## Scope

This is template narration, not an LLM integration. No model, API key, network service, external runtime, or generated prose is involved. A future language-model adapter can describe committed results, but it must submit actions through the existing rules and cannot mutate world state. The current game has a bounded authored graph and finite NPC arcs; it does not claim an unlimited generated world.

See [API.md](API.md) for commands and the native API, and [TECHNICAL_NOTES.md](TECHNICAL_NOTES.md) for persistence and determinism details.
