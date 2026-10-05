# Technical notes

The native core is the primary implementation. `core.hpp` contains game state, transitions, validation, observation filtering, independent policies, and JSON serialization. `json.hpp` is a bounded parser for the external JSON action boundary. `main.cpp` supplies CLI orchestration; `test.cpp` exercises state transitions and information boundaries.

The secret faction array exists only inside `Engine`. Policies receive a `View`, never an engine or full role array. Each view contains only its own private role; traitors do not receive partner identities. Private message filtering happens at observation construction and at public trajectory serialization. Global faction counts are used only to resolve termination. Winning outcomes necessarily convey aggregate information, but no opponent's role is published.

There are six policy memories and RNGs. Replaying the same seed and action sequence under the same C++ standard-library implementation yields byte-identical JSON. `std::shuffle` and `mt19937` distributions can differ between standard-library implementations, so cross-toolchain trajectory identity is not promised. Scores are deliberately transparent heuristics rather than learned or calibrated beliefs. Offers use local trust; external providers could supply actions through the validated boundary, but no provider adapter is bundled.

This is a compact council variant: strictly-majority voting can deadlock; sabotage occurs every third round; sealed roles make evidence intentionally incomplete. Coalition recipient selection is deterministic and internal. A limited run can end unresolved. Numeric belief publication is an explicit instrumentation convention for the public viewer.

The tests run 100 seeds with 12 round calls each, validate byte-identical trajectories, check bounded finite suspicion, parse generated JSON, verify filtered messages, validate rejected-action rollback and phase restrictions, and compare initial observations across all alternate two-traitor assignments with the observer's role fixed. Calls after termination are intentionally checked for stable behavior. The machine-readable test report records actual assertion calls, not game skill or a benchmark. No wall-clock or model intelligence performance is claimed.

No WebAssembly binary has been built in this environment. The C++17 core avoids platform dependencies, but a browser mirror is a separate preview implementation unless linked to an explicitly compiled native/WASM artifact.
