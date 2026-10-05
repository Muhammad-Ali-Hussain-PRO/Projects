# Veil Council API contract (version 1)

All IDs are integers 0..5. Seeds are unsigned 32-bit values; steps is 0..10000. Exit status is 0 on success, 1 on malformed input/runtime error, and 2 for a well-formed rejected single action.

## Native boundary

`Engine::observe(id)` returns `View`. Only `ownRole` has role information. `Engine::apply(action,error)` validates before mutation and returns a boolean. Invalid actions preserve state. `Engine::runRound(overrides)` advances one full round; completed games are unchanged. `heuristic(View,AgentMemory,type)` sees no global role array.

Action JSON:

```json
{"agent":0,"round":1,"type":"vote","target":2}
```

Allowed fields are exactly `agent`, `round`, `type`, `target`, `text`. `agent` and `type` are required; round defaults to 0, target to -1. Type is `message|offer|vote|kill`. Text is required for a message and must be 1..180 bytes. Broadcast target is -1; targeted messages require a living recipient. Other actions require another living target. Offers/messages require `talk`, votes require `ballot`, kills require `night` and the acting agent's own traitor role. Only one vote per agent per round. Stale rounds, dead actors, unknown fields, fractional IDs, duplicate JSON keys, oversized JSON, unsupported types, and wrong phases are rejected. `--phase` selects a round-zero boundary sandbox; it does not resume a running game. The full simulation alone schedules the third-round sabotage. Single-action response is:

```json
{"accepted":true,"error":"","observation":{"agent":0,"round":0,"ownRole":"councilor","phase":"talk","alive":[0,1,2,3,4,5],"suspects":[{"id":1,"p":0.31}],"messages":[],"votes":[],"offers":[]}}
```

Invalid out-of-range actor IDs produce `observation:null`. A malformed input produces a stderr error. The JSON parser handles escapes and non-surrogate Unicode escapes, caps input at 65536 bytes and nesting at 32, and rejects duplicate fields.

## Trajectory

`--seed N --steps N --json` emits one object:

```json
{"project":"Veil Council","seed":42,"mode":"native heuristic simulation","summary":{"winner":"traitors","rounds":9,"alive":2},"frames":[]}
```

Winner is `council|traitors|ongoing`. Frames contain exactly:

| Field | Type | Meaning |
| --- | --- | --- |
| round | integer | 0 is initialization; rounds start at 1 |
| phase | string | `talk`, `ballot`, or `night` |
| alive | integer[] | Living delegate IDs |
| beliefs | `{id,suspects:[{id,p}]}`[] | Published suspicion scores for living delegates, excluding self |
| messages | `{from,to,text}`[] | Broadcast messages only; `to:-1` |
| votes | `{from,target}`[] | Ballots cast this round |
| offers | `{from,to,target,accepted}`[] | Public coalition proposals and heuristic response |
| eliminated | integer[] | This round's sealed eliminations |
| text | string | Fixed template narration of the transition |

Frames are captured after initialization, discussion, voting, and every third-round sabotage. Consequently frame count is not identical to round count. The frame following sabotage contains the council elimination too, if any. No private role array is emitted.

## Private observations

`--observation ID` emits `{agent,round,ownRole,phase,alive,suspects,messages,votes,offers}`. Own role is `councilor|traitor`; suspects contain only other living delegates. Messages are broadcast plus messages sent or received by this ID. Votes are public; offers in this private view involve this ID. Published trajectory beliefs do not reveal ground-truth faction labels.

## Streaming external delegate

`--external-agent ID --steps N --json` emits newline-delimited requests before the selected delegate acts:

```json
{"kind":"request","requestedType":"vote","observation":{"agent":0,"round":1,"ownRole":"councilor","phase":"ballot","alive":[0,1,2,3,4,5],"suspects":[],"messages":[],"votes":[],"offers":[]}}
```

Return one action JSON line through stdin. Agent/type/round must exactly match the request; ordinary target, role, phase, and text rules also apply. The request contains only that delegate's private observation. Other delegates use heuristics. The final stdout line is the standard trajectory envelope. Input EOF or an invalid response terminates with status 1. A delegate that dies or is not selected for sabotage receives no corresponding request.

The core equivalent is `runRound(overrides, callback)`, where callback has type `std::function<std::optional<Action>(const View&,const std::string& requestedType)>`. Return `std::nullopt` to retain the heuristic action. Clients can route each private View independently; there is no built-in network transport or provider integration.
