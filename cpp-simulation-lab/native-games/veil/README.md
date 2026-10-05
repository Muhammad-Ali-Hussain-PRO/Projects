# Veil Council

A native C++17 social-deduction prototype with six independently seeded heuristic agents, private roles, evolving suspicion, messages, coalition offers, voting, and sealed eliminations. It is a local simulation: no live LLM, API credentials, provider integration, or generated reasoning is claimed.

From `native-games`:

```sh
make build/veil build/veil-tests
./build/veil --seed 42 --steps 12
./build/veil --seed 42 --steps 12 --json
./build/veil --seed 42 --observation 0
./build/veil-tests
```

Four councilors and two traitors start alive. Every round has talk and ballot phases. Agents propose a target, offer a coalition to a delegate they trust, and cast one vote. Elimination requires a strict majority. After every third unresolved round, one living traitor performs sabotage using only its own partial view; traitors do not know each other's identity and can target one another. Eliminated roles remain sealed. The council wins when no traitors remain; traitors win at parity. A steps limit can leave the result `ongoing`.

Each agent maintains its own suspicion array and RNG. Councilors target the most suspicious delegate; traitors target the least suspicious delegate. Accepted offers can redirect a vote. Isolated voting and votes against the observer change that observer's suspicion. These probabilities are heuristic scores, not calibrated estimates.

The trajectory exposes published suspicion scores, public messages, coalition proposals, ballots, living delegates, and eliminations. Private message text and every private role are excluded. The observation boundary exposes only the receiving agent's role and filters private messages to sender/recipient.

## External action boundary

Actions can be submitted without any model integration. Fetch a private observation, then submit a strict JSON object. A single action starts at round 0:

```sh
./build/veil --seed 42 --observation 0
./build/veil --seed 42 --action '{"agent":0,"round":0,"type":"message","target":-1,"text":"I want evidence before voting."}'
./build/veil --seed 42 --phase ballot --action '{"agent":0,"round":0,"type":"vote","target":1}'
```

Use `--actions actions.jsonl` to override a policy action in a seeded simulation. Each line supplies agent, type, and round (starting at 1). Unspecified actions retain the heuristic policy. A malformed or invalid override stops the run with an error; it never silently replaces an invalid input. A vote, message, offer, or kill override is selected by matching round/agent/type; duplicate matching overrides are rejected. Unreached overrides (for an eliminated actor or after the game ends) have no effect; a kill override applies only if that actor is selected for sabotage. Offer `target` means the proposed elimination target; the engine selects a recipient from the sender's permitted trust scores.

`API.md` defines exact input/output shapes. `TECHNICAL_NOTES.md` explains the privacy boundary and prototype limits.

For a live external decision process, `--external-agent ID --steps N --json` uses a JSONL protocol on stdin/stdout. Before that delegate's message, offer, vote, or selected sabotage action, stdout emits one private request. Read it, then return exactly one action line matching the requested agent/type/round. Other delegates retain their independent heuristics. After the run, stdout emits the ordinary trajectory envelope. A rejected response ends the run with a stderr error; EOF is an error rather than a fabricated agent answer. This is a generic process boundary with no LLM/provider connection.
