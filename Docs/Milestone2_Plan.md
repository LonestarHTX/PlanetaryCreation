# Milestone 2 – Tectonic Simulation Core (Refined Plan)

## Context Recap
- Milestone 1 delivered the editor module, tectonic subsystem scaffold, RealtimeMesh smoke path, and automation harness.
- The original Milestone 2 breakdown (14 tasks) was at risk of overreach. Feedback focused on narrowing scope to deterministic plate bootstrap, minimal integration, and validation plumbing while deferring thermodynamics/visual polish.
- This document synthesises the critique, incorporates pushback where needed, and captures the actionable plan plus open decisions awaiting confirmation.

## Key Agreements
1. **Scope Focus** – Stage delivery: deterministic bootstrap → minimal integration → metrics & UI plumbing. Defer Lloyd relaxation, heat diffusion, and advanced visualization to later milestones.
2. **Boundary Data Model** – Store boundary metadata in a dedicated adjacency map keyed by `(PlateA, PlateB)` to keep plate structs lean and enable future parallelisation.
3. **Determinism Validation** – Use snapshot hashing / toleranced comparisons rather than raw double equality to avoid FP noise.
4. **Icosphere First** – Seed plates from a deterministic icosphere subdivision for Milestone 2; defer Lloyd relaxation refinement until after baseline correctness.

## Clarifications & Adjustments
- **Crust Thickness** – Maintain static crust thickness per plate (oceanic vs continental) as part of initialisation so boundary polarity logic works. Dynamic erosion/accretion rules stay deferred.
- **Euler Poles** – Assign one deterministic Euler pole (axis + angular speed) per plate; keep them constant during Milestone 2, with migration fed by stress feedback deferred.
- **Visualization** – Minimal enhancement only: colour existing preview mesh by plate ID to confirm tessellation; defer velocity heatmaps, boundary edges, and async reconstructions.
- **Parameter Storage** – Start with a USTRUCT-owned parameter block inside the subsystem. Promote to a dedicated asset in later milestones if preset management becomes a requirement.
- **Boundary Topology** – For the icosphere approach, precompute adjacency once during initialisation and treat it as static for Milestone 2. Dynamic re-tessellation waits until Lloyd relaxation work.
- **Solid-Angle Drift** – Log total solid-angle deviation per step; treat deviations beyond epsilon as warnings for now, reserving auto-correction for Milestone 3.

## Refined Task Breakdown (10 Tasks)

### Phase 1 – Deterministic Plate Bootstrap
1. **Icosphere Initialisation**
   - Subdivide an icosahedron to achieve ~12–20 plates (configurable).
   - Assign one plate per face: store centroid, vertex indices, static crust type (70% oceanic / 30% continental via seeded RNG).
   - Validate coverage ≈ 4π steradians and log epsilon.
2. **Euler Pole Setup**
   - Generate deterministic per-plate Euler pole (axis + angular velocity) from the seed.
   - Persist in double precision within `FTectonicPlate`; hold constant during Milestone 2.
3. **Boundary Adjacency Map**
   - Build `TMap<TPair<int32, int32>, FPlateBoundary>` from icosphere edge topology.
   - Store shared edge vertices plus static crust metadata on the boundary for polarity decisions.

### Phase 2 – Minimal Integration Step
4. **Centroid Migration**
   - Apply Euler pole rotation per step (2 My) to update plate centroids.
   - Log displacement magnitudes for debugging.
5. **Boundary Velocity Tagging**
   - Compute relative edge velocity using plate poles.
   - Tag boundaries as divergent / convergent / transform; no crust growth/erosion yet.
   - Emit per-step counts for diagnostics.

### Phase 3 – Parameter Plumbing & UI
6. **Simulation Parameters Block**
   - Introduce `FTectonicSimulationParameters` (Seed, PlateCount, Viscosity, DiffusionConstant, etc.).
   - Store as `UPROPERTY` within the subsystem; add `ResetFromParameters()` entry point.
7. **UI Integration**
   - Extend `SPTectonicToolPanel` with seed input, “Regenerate” button, plate count & average velocity readouts.
   - Group advanced parameters in a collapsible section.

### Phase 4 – Determinism & Metrics
8. **Automation Tests**
   - Same-seed determinism (hash of sorted centroid/type tuples).
   - Time accumulation check (N steps ⇒ N × 2 My).
   - Plate count conservation & solid-angle within epsilon.
9. **Metrics Logging**
   - CSV export per step (step index, plate ID, centroid, velocity, crust type).
   - Total kinetic energy & boundary classification histogram per step.

### Phase 5 – Minimal Visual Feedback
10. **Plate ID Colouring**
    - Map plates to stable colours and update the preview mesh after each step to visualise motion.
    - Leave heatmaps and boundary overlays for Milestone 3.

## Outstanding Questions (Need Confirmation)
1. **Fixed Topology for Milestone 2?** – Proceed under the assumption that we are not re-tessellating Voronoi each step; adjacency remains static until Lloyd refinement. Please confirm.
2. **Lloyd Relaxation Timing** – Should icosphere → Lloyd upgrade be Milestone 3 core scope, or treated as stretch goal if Milestone 2 finishes early?
3. **Metrics Export Location** – Prefer `Saved/TectonicMetrics/` (keeps editor project clean) unless planning doc’s `Content/PlanetaryCreation/SimSnapshots/` directory should host CSVs. Need a decision to wire paths.
4. **Milestone Acceptance Criteria** – Proposed definition: (a) 100-step deterministic run with consistent hashes, (b) all automation tests green, (c) plate ID colouring visible/updates in editor. Confirm or adjust.

## Next Steps
1. Await confirmation on the outstanding questions above.
2. Once confirmed, begin executing tasks in order, keeping deferred items (Lloyd, crust exchange, heat diffusion) clearly annotated as future work.
3. Maintain `Docs/Milestone2_Feedback.md` as the living critique log; update this plan if scope shifts mid-milestone.

---
*Plan pending sign-off – please review the outstanding questions and acceptance criteria before implementation begins.*

