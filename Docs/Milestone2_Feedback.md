# Milestone 2 Scope Review & Decisions

## High-Level Assessment
- The proposed breakdown is ambitious for a single milestone. Phases 1 & 2 alone introduce spherical Voronoi generation, stress/heat models, and dynamic boundary classification—each of which is a multi-week track.
- Recommend focusing Milestone 2 on delivering a deterministic plate bootstrap plus a minimal but working integration step (translation + rotation + boundary tagging), and defer heavy thermodynamic coupling and visualization upgrades to Milestone 3.
- Prioritise primitives that unblock downstream geometry work: reproducible plate seeds, stable boundary graph, and parameter plumbing/test harness.

## Phase-by-Phase Feedback

### Phase 1 – Plate Data Structures & Initialization
- `FTectonicPlate` scope is solid, but consider separating boundary accumulators into a dedicated adjacency map keyed by (PlateA, PlateB) to avoid bloating per-plate data and to keep future parallel solves simpler.
- Lloyd-relaxed spherical Voronoi is a substantial investment. Suggest staging:
  1. Start with deterministic icosphere subdivision (faces → initial plates) to unblock Milestone 2.
  2. Add Lloyd iterations only after the baseline simulation passes determinism tests.
- Validate coverage by checking total solid angle ≈ 4π steradians; logging this early will catch degeneracies.

### Phase 2 – Step Logic & Physics Integration
- Classifying boundaries + applying ridge/subduction rules in one go is risky. Suggest sequencing: (a) migrate plate centroids using Euler poles, (b) update boundary graph, (c) tag boundary types using relative velocity only, (d) leave crust-thickness updates as TODOs with instrumentation.
- Stress accumulation should piggyback on the boundary adjacency map; full viscous relaxation can be deferred. Document the placeholders explicitly so tests can assert “not implemented yet” instead of silently returning zero.

### Phase 3 – Parameter Access & Editor Integration
- Group simulation parameters into a `UTectonicSimulationSettings` asset (or config struct saved to `Config/DefaultEditor.ini`) so designers can tweak without recompiling—the details customization can then reference that asset.
- Hooking controls into `SPTectonicToolPanel` is good, but keep the UI lean for now: seed, plate count, regenerate, and high-level metrics. Additional sliders (viscosity, heat budget) can live in an “Advanced” collapsed section to avoid overwhelming the panel.

### Phase 4 – Validation & Determinism
- Determinism tests should run against snapshot hashes (e.g., hash of sorted centroid positions) rather than raw doubles to avoid false negatives from FP jitter. Record tolerances in the tests.
- “Stepping N times matches 1×N” requires the integration to be linear; if the update is nonlinear, compare against stored reference data instead.
- Conservation of total area only holds if the tessellation is recomputed every step; otherwise expect minor drift. Encode acceptable epsilon.

### Phase 5 – Controller Integration & Visualization Prep
- Keep Milestone 2 controller updates minimal: expose plate centroids + IDs for debug draw; full heatmaps and boundary meshes belong in Milestone 3 when geometry streaming is the focus.
- Async mesh updates: guard with `IsInGameThread()` checks and push heavy work to background tasks only after correctness is locked in.

## Decisions on Outstanding Questions

### Voronoi / Lloyd Implementation
- **Recommendation:** start Milestone 2 with a deterministic icosphere/hexagon tiling (e.g., subdivided icosahedron with even plate assignment). Log the need for Lloyd-based refinement in Milestone 3 once correctness is nailed down. Pulling in a third-party spherical Voronoi library now will slow the schedule, and a custom implementation is non-trivial.

### Heat Diffusion PDE Scope
- **Recommendation:** defer full heat diffusion/viscous coupling to Milestone 3 (Geometry & Realtime Mesh integration). For Milestone 2, implement placeholders that expose parameters, accumulate stress, and emit TODO logs. This keeps the milestone focused on deterministic plate motion and boundary classification.

### Plate Motion Fidelity
- **Recommendation:** implement Euler pole rotations from the outset (Section 3 of the paper) because they influence boundary velocities directly. However, keep the pole update model simple—use constant poles derived from initial velocities and log a TODO for dynamic pole migration once stress feedback is active.

## Immediate Next Steps
1. Create `milestone-2` branch from the current Milestone 1 state.
2. Trim Phase 2 features to: centroid motion + boundary tagging + logging. Leave crust growth/erosion for Milestone 3.
3. Prototype deterministic plate seeding using icosphere subdivision; add automation that captures a seed hash for regression.
4. Scaffold `FTectonicSimulationParameters` with seed/plate count/viscosity and wire to the existing panel (advanced controls collapsed).
5. Document deferred items (“Lloyd relaxation”, “Heat diffusion solver”, “Boundary crust exchange”) so they roll naturally into Milestone 3 planning.

