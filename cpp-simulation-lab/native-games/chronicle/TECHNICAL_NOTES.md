# Chronicle Engine technical notes

## State and rule separation

`State` stores the seed, RNG state, committed-turn counter, location enum, health, coins, five item counts, two NPC arc indices, sentinel and relic flags, and a six-bit discovery mask. Location prose and objectives are derived from these values by `TemplateNarrator`. The narrator has no mutable state reference and cannot create inventory, rewards, paths, or NPC progress.

`Engine::apply` works against a copy of the state. Preconditions run before random draws or rewards. An accepted action advances exactly one turn, runs the global invariant validator, appends an event, and commits the copy. A semantic rejection appends an event whose before and after hashes are identical; gameplay state and RNG remain unchanged. Appending the event happens before state assignment, so an allocation failure cannot commit an unjournaled transition. Malformed API commands are rejected before journaling.

The global validator enforces:

- Six legal locations and discoveries contained in six bits.
- Health in `[1,20]`, coins in `[0,999]`, inventory at most eight slots, and nonnegative item counts.
- At most one brass key and one relic.
- NPC arc indices in `[0,2]` and a sentinel cleared only after meeting Orin.
- A relic obtained only after Mira requests it and the sentinel is cleared.
- Exact relic ownership during Mira's quest, and no retained relic after its return.
- A discovered current location and a brass key whenever the player is in the cavern.

The undirected adjacency graph is fixed: village–woods, village–market, village–shrine, woods–ruins, ruins–cavern, shrine–cavern. Entering the cavern requires the key. Walking is allowed only along a direct graph edge. Exploration refuses dangerously low health instead of permitting an invalid dead state.

## Determinism

The engine uses a specified SplitMix64 step implemented with unsigned 64-bit arithmetic. Only ruins combat/salvage and cavern mining draw from it. There is no dependence on `std::random_device`, system time, unordered-container iteration, locale-sensitive ordering, or a language model. Each seeded run starts from the same canonical state.

Canonical state serialization has a fixed numeric field order. FNV-1a-64 hashes it for journal continuity and hashes the save payload for accidental corruption detection. FNV is not cryptographic authentication; an attacker who can replace a save can recompute its checksum. Even a checksum-valid state must pass invariant validation and reconstruct exactly through replay.

The demonstration pilot is a deterministic helper outside the rule layer. It routes through a breadth-first search, completes the NPC arcs, manages dangerous health and full inventory, then chooses exploration and movement using a seed-and-step hash. A run resumed with the same event offset continues the same pilot sequence. The pilot may deliberately issue an invalid location; rules reject it without changing state.

## Journal and persistence

Each event stores a one-based sequence, action and argument, acceptance flag, stable result code, and before/after state hashes. Narrative text is regenerated from rules rather than saved as authoritative game facts. Replay validates the sequence and before hash, applies the command, then checks its outcome, result code, and after hash. It cannot silently accept a different action history.

The text save format starts with `CHRONICLE_SAVE 1`, followed by `CHECKSUM` and a fixed-width hexadecimal checksum of the exact remaining payload. The payload has a `STATE` line, an `EVENTS` count, and quoted command arguments/result codes on `EVENT` lines. Version 1 is the only supported version; there is no implicit migration. Files are capped at 32 MiB, journals at 100,000 events, and arguments at 64 bytes without embedded NUL or line breaks.

On Linux/POSIX, saving creates a unique `mkstemp` sibling with owner-only permissions, writes every byte, syncs and closes it, atomically renames it to the destination, and syncs the parent directory. A write or rename failure leaves the old destination intact. A directory-sync error is reported even though rename may already have succeeded. The non-POSIX fallback flushes a standard stream and renames a temporary file, but does not claim power-loss durability. Multiple writers to one save path are last-writer-wins; no locking or distributed conflict resolution is implemented.

Loading checks file size, version, checksum, field bounds, state invariants, and trailing data. It replays the entire event log from the original seed, then compares the complete reconstructed state, including RNG and turn, with the serialized state. No state setter or unvalidated mutable state accessor is exposed.

## Verification

The native test executable uses checks that throw on failure and exits nonzero; checks stay enabled regardless of `NDEBUG`. It validates graph symmetry and boundedness, command parsing, malformed inputs, atomic rejection, key gates, inventory capacity, trade prices and unique items, combat bounds, consumption, NPC completion, and one-time rewards.

Eight seeds, including zero and the maximum unsigned 64-bit seed, each receive 600 actions with deliberate rejection attempts. Every action validates state, compares an identical seeded engine, and verifies either a one-turn commit or an unchanged hash. Every run must commit at least 300 turns and complete both NPC arcs, then replay to the same hash. Persistence checks cover replacement of an existing save, identical continuation after loading, checksum corruption, unsupported version, truncated data, a checksum-valid RNG mismatch, and tampered event sequence and hashes.
