# Chronicle Engine API

## Native header

Include `core.hpp` and use the `chronicle` namespace. The implementation is header-only and requires C++17.

```cpp
chronicle::Engine game(42);
chronicle::Result result = game.apply(chronicle::parseCommand("move woods"));
chronicle::Snapshot frame = game.snapshot(result.text);
game.save("run.save");
chronicle::Engine resumed = chronicle::Engine::load("run.save");
chronicle::Engine replayed = chronicle::Engine::replay(42, game.eventLog());
```

| API | Contract |
| --- | --- |
| `Engine(uint64_t seed = 1)` | Starts in village with 20 HP, 10 coins, and one potion. Every seed, including zero, is valid. |
| `apply(const Command&) -> Result` | Validates and commits one action, or returns a rejection without changing game state. Semantic attempts are journaled. |
| `state() const -> const State&` | Read-only structured game state. |
| `eventLog() const -> const vector<Event>&` | Read-only ordered history of accepted and rejected semantic commands. |
| `validateState(string* error = nullptr) const -> bool` | Checks all world, resource, discovery, and quest invariants; optionally explains a failure. |
| `stateHash() const -> string` | Sixteen-character lowercase hexadecimal FNV-1a-64 hash of canonical full state. |
| `snapshot(const string& text = "") const -> Snapshot` | JSON-friendly values plus derived NPC arcs, objective, and narration. Empty text uses a location template. |
| `save(const string& path) const` | Writes a checksummed, versioned, durable save; throws on I/O failure. |
| `Engine::load(const string& path) -> Engine` | Validates and reconstructs the entire journal, then verifies its exact final state. Throws on invalid data or I/O failure. |
| `Engine::replay(uint64_t seed, const vector<Event>&) -> Engine` | Reapplies a history and verifies all sequences, result codes, outcomes, and state hashes. Throws on mismatch. |
| `parseCommand(const string&) -> Command` | Parses a known action with zero or one whitespace-delimited argument. Throws for unknown verbs, empty input, or extra tokens. |
| `commandText(const Command&) -> string` | Renders the canonical action and argument for terminal output. |
| `routeTo(const Engine&, Location) -> Command` | Returns the next legal move on a breadth-first route, or `inspect` if already there or unreachable. |
| `demoCommand(const Engine&, uint64_t step) -> Command` | Deterministic demonstration pilot; does not mutate the game. |

`Command` contains `Action action` and `string argument`. `Result` contains `bool accepted`, `string code`, and `string text`. Invalid enum values or arguments longer than 64 bytes, containing NUL, CR, or LF, return `malformed_command` without entering the journal. Other rejected actions are logged. Exceptions from resource limits or I/O should be handled by the caller.

## Commands

| Command | Rules and outcome |
| --- | --- |
| `move village|woods|ruins|market|shrine|cavern` | Must follow one graph edge; entering cavern requires a key. Marks the destination discovered. |
| `gather herb` | Woods only; adds one herb if an inventory slot is free. |
| `talk mira` | Village only. A first herb is consumed for six coins and starts the relic quest. The relic is consumed for twenty coins and completes the arc. Hints otherwise. |
| `talk orin` | Woods only. First meeting unlocks ruins exploration. Reporting the cleared sentinel pays eight coins once and completes the arc. |
| `trade potion` | Market only; four coins and one free slot. |
| `trade key` | Market only; eight coins, one free slot, and no existing key. |
| `trade sell-herb` | Market only; consumes one herb for two coins. |
| `trade sell-ore` | Market only; consumes one ore for five coins. |
| `rest` | Village/shrine: restore HP to 20 for free. Elsewhere: spend two coins and restore up to six HP. |
| `explore` | Woods: herb. Ruins: require Orin and HP ≥6; first clears sentinel for five coins and 2–4 damage, next retrieves requested relic for one damage, later finds 2–4 coins for one damage. Cavern: require HP ≥6 and one free slot; ore with 1–3 damage. Other locations: survey. |
| `use potion` | Consume one potion to restore up to eight HP. Rejected at full health or without a potion. |
| `inspect` | Read location narration as a committed turn. |

Rewards that would overflow the 999-coin bound are rejected. Gathering, buying, mining, and relic retrieval are rejected when inventory is full. Command names and arguments are case-sensitive. `rest`, `explore`, and `inspect` take no argument. Hint conversations and surveys count as turns; saves do not.

## CLI and JSON

```text
chronicle [--seed N] [--steps N] [--json] [--save PATH] [--load PATH]
chronicle --replay PATH [--json]
chronicle [--seed N] --command 'move woods' --command 'talk orin'
chronicle --interactive [--load PATH] [--save PATH]
```

CLI defaults are seed 42 and 48 attempted actions. `--steps` accepts 0–10000. Loaded saves determine the seed and starting event offset. Repeated `--command` options run exactly those commands instead of the pilot. `--replay` verifies a save and reports its final state without further turns. `--interactive` accepts normal commands, `save PATH`, and `quit`; it cannot be combined with JSON or scripted commands. Errors go to stderr and produce exit status 1.

The JSON contract is:

```json
{
  "project": "Chronicle Engine",
  "seed": 42,
  "mode": "deterministic-demo",
  "summary": {
    "turns": 0,
    "accepted": 0,
    "rejected": 0,
    "stateHash": "16 hexadecimal characters",
    "completedArcs": 0
  },
  "frames": [
    {
      "turn": 0,
      "location": "village",
      "hp": 20,
      "gold": 10,
      "inventory": ["potion"],
      "npcArc": "Mira: herbal remedy; Orin: stranger",
      "objective": "Bring a herb from the woods to Mira in the village.",
      "text": "Lanterns hang above the village archive. Mira is waiting by its door."
    }
  ]
}
```

Modes are `deterministic-demo`, `scripted-commands`, `interactive-template`, and `event-replay`. JSON output uses the first, second, or fourth. `summary.accepted` and `summary.rejected` count the complete journal, including loaded history; `turns` counts accepted actions. Frames cover the current invocation, are sampled to at most 101 entries, and always include the initial and final state. Inventory repeats names for multiple items. Rejected-action narration begins `Rejected (CODE):` and may share the previous frame's turn number.

The test executable prints a single JSON object with `project`, `passed`, `checks`, `suites`, and `consistency`. Its consistency object reports seeds, actions per seed, total committed turns, and the minimum committed turns in any run. A failing suite prints an error to stderr and exits 1.
