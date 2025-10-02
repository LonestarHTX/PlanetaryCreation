# Procedural Tectonic Plan – Editor Tool Implementation

## Mission Overview
- Deliver an editor-only Unreal Engine 5.5.4 tool that reproduces the Procedural Tectonic Planets paper using `RealtimeMeshComponent`.
- Focus on deterministic simulation, high-fidelity mesh visualization, and researcher-friendly UX without relying on PIE.
- Coordinate multidisciplinary roles (Simulation, Rendering, Tools, QA) with clear deliverables and validation gates.

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
  - Author user guide and release notes in `Docs/`.
- **Dependencies:** Automation framework, profiling tools, AGENTS.md workflow.
- **Risks:** Hardware variability; lock seeds and tolerances.
- **Validation:** Green automation runs, profiling reports, final editor demo.

## Cross-Cutting Workstreams
- Weekly integration sync between Simulation and Rendering leads to track data contracts.
- Maintain a risk register highlighting RealtimeMesh API gaps or math uncertainties.
- Document deviations from the paper in `ProceduralTectonicPlanetsPaper/README_addendum` (create if needed).
- Ensure plugin updates mirror source control and CI builds the editor target.

## Validation Summary
- **Profiling:** Use `stat unit`, `stat RHI`, and Unreal Insights at each milestone.
- **Automation:** Run editor automation suites (`UnrealEditor.exe PlanetaryCreation.uproject -run=Automation -Test=PlanetaryCreation`).
- **Visualization:** Capture comparison screenshots versus paper figures for elevation, stress, and temperature maps.
- **Sign-off:** Require Simulation + Rendering lead approval per milestone before moving forward.
