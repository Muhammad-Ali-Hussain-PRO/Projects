# Native replay contract

`docs/output-schema.json` specifies each emitted JSON field. The schema version is identified by the project contract in this source release; the top-level `project` value is `aether` and `mode` is `native`.

- World coordinates use **Y up**. Distances, time, and volume use internally consistent simulation units; 1 step is 1/120 second.
- Geometry `vertices` is a flat XYZ array in **local coordinates**. Add `position` to place the mesh; `indices` references vertices, with three indices per triangle. `size` gives AABB dimensions. `solid` distinguishes collision geometry from decorative meshes.
- Box triangle winding is outward-facing. Decorative grass is thin, so use a two-sided material for grass.
- Bodies are axis-aligned boxes centered at the frame's `position`; `size` is full width/height/depth. Their final state is represented in `frames`, not in static `geometry`.
- Reservoir `position.x/z` is its horizontal center; reservoir `position.y` is unused and zero. `base_y` is the interior floor elevation. Render water centered at `[position.x, base_y + level/2, position.z]` with size `[size.x, level, size.z]`. Omit the water mesh when level is zero.
- Reservoir `capacity = size.x * size.y * size.z`, `level = volume / (size.x * size.z)`. The container's `size.y` is its maximum depth.
- Frames always contain the initial frame and final frame. Intermediate frames occur every `--sample-every` steps; default 30 means 4 snapshots/second. Interpolation makes a replay smooth but does not produce additional native measurements.
- `contacts` is a solver-contact count for the final fixed step of that snapshot. It can include repeated constraint projection; it is not a count of unique touching pairs.
- `max_penetration` is maximum overlap depth across dynamic/static and dynamic/dynamic AABBs at the snapshot. `summary` reports the **final** state's invariants, not worst-case statistics over every step.
- `summary.validation.checks` is the number of invariant checks applied to the final scene; it is not the number of unit-test assertions.
- `puzzle_solved` means the goal's depth is at least one unit at that snapshot. Initial `feed` is open, initial `gate` is closed unless an explicit action changes them.

A browser can load a verified JSON snapshot and replay it. If a browser separately simulates or regenerates the scene, label that behavior as a browser mirror rather than native execution. Do not infer compile-time, performance, machine-learning, CFD, WASM, or rendering claims from these data files.
