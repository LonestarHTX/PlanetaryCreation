# Procedural Tectonic Plan – Editor Tool Implementation

## Mission Overview
- Deliver an editor-only Unreal Engine 5.5.4 tool that reproduces the Procedural Tectonic Planets paper using `RealtimeMeshComponent`.
- Focus on deterministic simulation, high-fidelity mesh visualization, and researcher-friendly UX without relying on PIE.
- Coordinate multidisciplinary roles (Simulation, Rendering, Tools, QA) with clear deliverables and validation gates.

### Documentation Responsibilities
- `Docs/MilestoneSummary.md` records milestone-defining wins, scope shifts, and high-risk fixes that we would surface in a milestone review.
- `Docs/Milestone6_Plan.md` (or the active milestone’s plan) captures task breakdowns, scope decisions, risks, and schedule adjustments.
- `Docs/PlanningAgentPlan.md` tracks day-to-day follow-ups, watch items, and reminders for the planning agent.
- Individual fix/feature docs (e.g., `Docs/ridge_cache_fix_plan.md`) hold the detailed investigations and implementation notes that inform the entries above.

## Milestone Timeline

### Milestone 0 – Pre-Production Alignment (Week 0)
- **Owner:** Technical Art Director
- **Deliverables:** Annotated paper review, shared terminology glossary, feasibility memo.
- **Dependencies:** `ProceduralTectonicPlanetsPaper` images, `RealtimeMeshComponent_HowTo.md`.
- **Risks:** Misinterpreting math; mitigate with peer review.
- **Validation:** Kickoff review, captured Q&A log.

### Milestone 1 – Tooling & Infrastructure Setup (Week 1)
- **Owners:** Tools Engineer (lead), Build Engineer
- **Tasks:**
  - Create `PlanetaryCreationEditor` module, toolbar tab, and spawnable utility widget with a 2 My `Step` button (batch stepping deferred).
  - Define simulation scaffolding (`UTectonicSimulationService`, `FTectonicSimulationController`) using double-precision state and float conversion helpers for RealtimeMesh.
  - Enable `RealtimeMeshComponent` in the editor target; prototype a placeholder sphere update via `UpdateSectionGroup`/`UpdateSectionRange` to prove topology swapping works.
  - Seed exemplar infrastructure: create heightfield asset folders, stub `UHeightfieldExemplarLibrary`, and register an empty metadata `UDataTable`.
  - Set up automation test harness in `Source/PlanetaryCreationEditor/Private/Tests` with perf-report TODO hooks for future Insights captures.
- **Dependencies:** UE 5.5.4 SDK, Visual Studio, RMC plugin docs.
- **Risks:** Editor build linkage conflicts; isolate plugin usage.
- **Validation:** Compile editor target, run editor automation smoke tests, verify tab loads sans PIE, confirm placeholder mesh updates after pressing Step.

### Milestone 2 – Data & Simulation Core (Weeks 2–4)
- **Owners:** Simulation Engineer, Tools Engineer
- **Tasks:**
  - Implement `UTectonicSimulationService` with plate init, velocity fields, divergence rules.
  - Build deterministic tick loop using editor delegates (no PIE).
  - Expose parameters (seed, timestep, viscosity, plate count) through details customization.
- **Dependencies:** Paper equations, UE math libraries, RealtimeMesh data buffers.
- **Risks:** Floating-point drift; use double-precision accumulators and regression logs.
- **Validation:** Automation unit tests, CSV export of plate states, researcher sign-off.

### Milestone 3 – Geometry Generation & Realtime Mesh Integration (Weeks 4–6)
- **Owners:** Rendering Engineer (lead), Simulation Engineer
- **Tasks:**
  - Build spherical scaffold with RealtimeMesh section groups (quad-sphere/icosphere options).
  - Map tectonic scalar fields to vertex streams; update via `FRealtimeMeshStreamSet`.
  - Implement LOD tiers matching paper resolution; expose performance toggles.
- **Dependencies:** `RealtimeMeshComponent` APIs, paper geometry specs, UE material system.
- **Risks:** Runtime mesh update cost; optimize with shared buffers and async tasks.
- **Validation:** Heatmap/elevation overlays, `stat unit`/`stat RHI` captures, screenshot comparisons.

### Milestone 4 – Editor Experience & UX Polish (Weeks 6–7)
- **Owners:** Tools Engineer, UX Designer
- **Tasks:**
  - Create control panel (Slate/UMG) for lifecycle (Init, Step, Bake, Export).
  - Add timeline scrubber and snapshot archive in `Content/PlanetaryCreation/SimSnapshots`.
  - Attach tooltips linking paper sections.
- **Dependencies:** Slate/UMG frameworks, localization assets.
- **Risks:** UI thread stalls; move heavy ops to background tasks with progress dialogs.
- **Validation:** Usability walkthrough, persistence check, no PIE launch required.

### Milestone 5 – Validation, Optimization, Packaging (Weeks 7–8)
- **Owners:** QA Lead, Performance Engineer
- **Tasks:**
  - Expand automation suite for determinism, mesh integrity, UI regression.
  - Profile with Unreal Insights; document memory/CPU budgets.
  - Ship lightweight orbital inspection camera (orbit + smooth zoom) for reviewer workflows.
  - Expose undo/redo controls in the editor (toolbar buttons + `Ctrl+Z`/`Ctrl+Y`) using the rollback snapshots.
  - Implement continuous playback controls (Play/Pause, step rate selector) that respect determinism & rollback history.
  - Author user guide and release notes in `Docs/`.
- **Dependencies:** Automation framework, profiling tools, AGENTS.md workflow.
- **Risks:** Hardware variability; lock seeds and tolerances.
- **Validation:** Green automation runs, profiling reports, final editor demo.

### Milestone 6 – Amplification & Performance Parity (Weeks 9–11)
- **Owners:** Simulation Engineer, Rendering Engineer, Performance Engineer
- **Tasks:**
  - Implement Stage B terrain amplification (procedural noise + exemplar blending at ~100 m scale).
  - Add terrane extraction/reattachment and localized uplift during continental collisions.
  - Implement continental erosion & sediment transport tied to stress/elevation history.
  - ✅ Applied ParallelFor multi-threading to sediment/dampening/mesh build (achieved 6.32ms vs 110ms budget).
  - ✅ Implemented GPU compute shaders for Stage B oceanic/continental amplification (steady-state ≈33–34 ms per step at L7: Oceanic GPU ≈8 ms, Continental GPU ≈23 ms, CPU bookkeeping ≈3 ms; first warm-up replay ≈65 ms).
  - ⏸️ GPU thermal/velocity fields deferred (0.6ms CPU cost not worth transfer overhead).
  - Update automation/CSV exports to include new amplification metrics; reproduce paper figures at Level 7 for parity.
- **Dependencies:** Milestone 5 data exports, exemplar datasets *(✅ Stage B SRTM catalog & cutter ready)*, profiling harness.
- **Risks:** Amplification instability, GPU integration complexity; mitigate with staged rollouts and regression tests.
- **Validation:** Side-by-side parity screenshots vs paper, Level 7 performance within budget (<120 ms), amplification regression suite.
- **Immediate Actions (PRO Review – 2025-10-07):**
  - Consolidate performance capture to a single Unreal Insights + CSV flow and refresh every doc/table with the same baseline; include subduction/collision/elevation timers to mirror the paper.
    - Harness and latest metrics live in `Docs/Performance_M6.md`; treat it as the canonical source for Stage B/L3 measurements.
    - Run the Stage B parity harness with `-SetCVar="r.PlanetaryCreation.StageBProfiling=1"` **and** `r.PlanetaryCreation.StageBProfiling 1` in `-ExecCmds` so the logs always emit `[StageB][Profile]` / `[StageB][CacheProfile]`.
  - Leverage the continental exemplar blend cache so CPU fallbacks reuse the first pass; the `PlanetaryCreation.Milestone6.ContinentalBlendCache` automation guards the serial sync (add it to the same CI run that executes the GPU parity suites) while Stage B logs should show only the initial `[ContinentalGPUReadback] Overrides=…` line.
  - Surface the `[ContinentalGPU] Hash check … Match=1` log lines in CI output (Oceanic + Continental parity suites) and fail the run if the match rate drops—this keeps the snapshot hash fix exercised.
  - Keep the `RefreshRidgeDirectionsIfNeeded()` flow using the render-vertex cache (no per-step recompute) and keep `RidgeDirectionCacheTest` green in CI.
  - Capture steady-state timings with the new Stage B in-place mesh refresh path (no rebuild) and document the savings; fall back to rebuild only when topology stamps diverge.
  - Use the expanded `[StepTiming]` logging (Voronoi reassignment counts plus ridge dirty/update/cache stats) to enforce ridge-cache health thresholds during undo/redo and parity automation, keep `CachedVoronoiAssignments` in sync across undo/retess paths, and confirm the adaptive ring-depth fallback (default depth 1) keeps ridge recomputes near 0 ms.
  - Keep an eye on the navigation-system warning (`NavigationSystem.cpp:3808`) now suppressed by the module ensure handler and confirm no new ensures slip through automation logs.
  - Raise `RetessellationThresholdDegrees` default to 45° with a “High Accuracy (30°)” toggle for review builds so perf measurements match the documented quick win.
  - Normalize sediment/dampening loops to rely on cached adjacency weights to cut repeated neighbor scans.
  - Profile sediment (~14–19 ms) and dampening (~24–25 ms) passes with Stage B enabled and queue follow-up SoA/ParallelFor work so they become the next optimization focus now that Stage B sits at ~33 ms steady-state.
  - Draft `Docs/ReleaseNotes_M6.md` and `Docs/PaperParityReport_M6.md` (or equivalent sections) with the updated Stage B timings (65 ms warm-up / 33–34 ms steady-state / 44 ms parity undo) so hand-off collateral stays consistent.
  - Treat Level 7 as a validation tier until async GPU amplification lands; document this expectation in milestone hand-off notes.

- **New:** M6 Task 2.3.1 (Stage B Perf Profiling) instruments amplification loops to get us under 1s per step at L7; required before we flip Stage B on by default. Once CPU cost is under control, revisit GPU offload and UI polish in M7.
- **Update 2025-10-06:** GPU oceanic amplification now matches CPU within 0.0025 m but still incurs blocking readback cost. Keep the CPU implementation in the simulation loop while the SoA/LOD refactor lands; schedule the async GPU pipeline (persistent buffers + fence sync) for the Milestone 7 rendering polish window.

### Milestone 7 – Presentation & UX (Weeks 11–13)
- **Owners:** Tools Engineer, Rendering Engineer, UX Designer
- **Phase 1 – Materials & Heightmap Visualizer:** Restore the deferred material/lighting stack (ocean vs continental crust, volcanic emissive, day/night lighting) so the heightmap visualizer reflects Stage B data accurately from day one.
- **Phase 2 – Camera & World Navigation:** Implement the Spore-style multi-band camera transitions with collision easing and surface glide.
- **Phase 3 – UI & Analytics Enhancements:** Extend the tool panel with the timeline scrubber, snapshot browser, analytics overlays, and continuous playback refinements (recording, speed modulation).
- **Phase 4 – Presentation Collateral:** Refresh validation gallery/tutorials and prepare milestone demo collateral showcasing the amplified terrain.
- **Dependencies:** Milestone 6 amplified terrain, camera design doc, UI framework.
- **Risks:** UX scope creep; mitigate with phased rollout and usability reviews.
- **Validation:** UX sign-off, updated gallery, playtest feedback.

### Milestone 8 – Climate & Hydrosphere Coupling (Weeks 13–15)
- **Owners:** Simulation Engineer, Rendering Engineer, Tools Engineer
- **Tasks:**
  - Implement ocean temperature/height feedback driven by tectonic activity.
  - Couple procedural climate zones (temperature/precipitation) to thermal field.
  - Visualize climate overlays, vector fields, and integrate into export pipeline.
- **Dependencies:** Milestones 4–7 data exports, environment materials.
- **Risks:** Performance impact from layered simulation; mitigate with toggles.
- **Validation:** Climate visualization walkthrough, automation tests for climate data.

### Milestone 9 – Shipping Readiness & Cinematic Polish (Weeks 15–17)
- **Owners:** Tools Engineer, Rendering Engineer, QA Lead
- **Tasks:**
  - Final optimization pass (profiling, GPU offloads, streaming budgets, console-class targets).
  - Implement cinematic materials, screenshot/video tooling, release packaging, documentation.
- **Dependencies:** All prior milestones, marketing assets.
- **Risks:** Scope creep; lock feature set.
- **Validation:** Final QA sign-off, release checklist, documentation handoff.

## Cross-Cutting Workstreams
- Weekly integration sync between Simulation and Rendering leads to track data contracts.
- Maintain a risk register highlighting RealtimeMesh API gaps or math uncertainties.
- Track migration plan for the `StructUtils` plugin dependency (RealtimeMeshComponent) now flagged for removal in UE 5.5+, and secure replacement before engine upgrade.
- Document deviations from the paper in `ProceduralTectonicPlanetsPaper/README_addendum` (create if needed).
- Ensure plugin updates mirror source control and CI builds the editor target.
- Monitor Stage B LOD swaps post-cascade; if hitches surface, schedule incremental GPU stream uploads before Milestone 6 locks.

## Validation Summary
- **Profiling:** Use `stat unit`, `stat RHI`, and Unreal Insights at each milestone.
- **Automation:** Run editor automation suites (`UnrealEditor.exe PlanetaryCreation.uproject -run=Automation -Test=PlanetaryCreation`).
- **Visualization:** Capture comparison screenshots versus paper figures for elevation, stress, and temperature maps.
- **Sign-off:** Require Simulation + Rendering lead approval per milestone before moving forward.
